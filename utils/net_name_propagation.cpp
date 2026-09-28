#include "net_name_propagation.h"

#include "cell_content.h"
#include "coord_scale.h"
#include "room_paths.h"
#include "database.h"
#include "layer_spec.h"
#include "pin_retarget.h"
#include "property.h"
#include "shape.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace room {
namespace {

const std::string *findProperty(const std::vector<Property> &props, const std::string &name)
{
    for (const Property &prop : props) {
        if (prop.name == name) {
            return &prop.value;
        }
    }
    return nullptr;
}

void setProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    for (Property &prop : props) {
        if (prop.name == name) {
            prop.value = value;
            return;
        }
    }
    props.push_back({name, value});
}

std::string cellStem(const std::string &cellName)
{
    std::string type = cellName;
    const auto slash = type.find_last_of("/\\");
    if (slash != std::string::npos) {
        type = type.substr(slash + 1);
    }
    if (type.size() > 4 && type.compare(type.size() - 4, 4, ".sym") == 0) {
        type.resize(type.size() - 4);
    }
    return type;
}

std::string logicalType(const Instance &inst)
{
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && !qt->empty()) {
        return *qt;
    }
    return cellStem(inst.cellName());
}

std::string pinLabelFromProperties(const std::vector<Property> &props)
{
    if (const std::string *lab = findProperty(props, "lab")) {
        return *lab;
    }
    if (const std::string *name = findProperty(props, "name")) {
        return *name;
    }
    if (const std::string *pinName = findProperty(props, "pinName")) {
        return *pinName;
    }
    return {};
}

Point applyOrient(Point p, Orient orient)
{
    switch (orient) {
    case Orient::R90:
        return Point{-p.y, p.x};
    case Orient::R180:
        return Point{-p.x, -p.y};
    case Orient::R270:
        return Point{p.y, -p.x};
    case Orient::MX:
        return Point{p.x, -p.y};
    case Orient::MX90:
        return Point{p.y, p.x};
    case Orient::MY:
        return Point{-p.x, p.y};
    case Orient::MY90:
        return Point{-p.y, -p.x};
    case Orient::R0:
    default:
        return p;
    }
}

Point transformLocal(Point local, const Transform &xf)
{
    const Point rotated = applyOrient(local, xf.orient);
    return Point{xf.x + rotated.x, xf.y + rotated.y};
}

bool pointNear(const Point &a, const Point &b, std::int64_t tol)
{
    return std::llabs(a.x - b.x) <= tol && std::llabs(a.y - b.y) <= tol;
}

bool pointOnWireSegment(Point p, Point a, Point b, std::int64_t tol)
{
    if (pointNear(p, a, tol) || pointNear(p, b, tol)) {
        return true;
    }
    if (std::llabs(a.x - b.x) <= tol) {
        if (std::llabs(p.x - a.x) > tol) {
            return false;
        }
        const std::int64_t lo = std::min(a.y, b.y) - tol;
        const std::int64_t hi = std::max(a.y, b.y) + tol;
        return p.y >= lo && p.y <= hi;
    }
    if (std::llabs(a.y - b.y) <= tol) {
        if (std::llabs(p.y - a.y) > tol) {
            return false;
        }
        const std::int64_t lo = std::min(a.x, b.x) - tol;
        const std::int64_t hi = std::max(a.x, b.x) + tol;
        return p.x >= lo && p.x <= hi;
    }
    return false;
}

LayerPurpose layerPurpose(const std::vector<LayerSpec> &layers, std::uint32_t layerId)
{
    for (const LayerSpec &layer : layers) {
        if (layer.layerNum == layerId) {
            return layer.purpose;
        }
    }
    return LayerPurpose::Drawing;
}

std::string wireLabelFromProps(const std::vector<Property> &props)
{
    if (const std::string *label = findProperty(props, "label")) {
        return *label;
    }
    if (const std::string *lab = findProperty(props, "lab")) {
        return *lab;
    }
    return {};
}

void setWireLabel(std::vector<Property> &props, const std::string &label, const std::vector<Point> &pts,
                  double dbuPerEditorUnit)
{
    if (label.empty() || pts.size() < 2) {
        return;
    }
    if (findProperty(props, "label") != nullptr) {
        setProperty(props, "label", label);
    }
    setProperty(props, "lab", label);

    const std::int64_t lx = findProperty(props, "labelX") ? std::stoll(*findProperty(props, "labelX")) : 0;
    const std::int64_t ly = findProperty(props, "labelY") ? std::stoll(*findProperty(props, "labelY")) : 0;
    if (lx != 0 || ly != 0) {
        return;
    }
    const std::int64_t scale = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::llround(dbuPerEditorUnit > 0.0 ? dbuPerEditorUnit : 1.0)));
    const std::int64_t midX = (pts.front().x + pts.back().x) / 2;
    const std::int64_t midY = (pts.front().y + pts.back().y) / 2;
    setProperty(props, "labelX", std::to_string(midX + 10 * scale));
    setProperty(props, "labelY", std::to_string(midY - 10 * scale));
}

std::string symbolCorePathFromAnyCorePath(const std::string &roomPath)
{
    const std::size_t schematic = roomPath.find(".schematic.room");
    if (schematic != std::string::npos) {
        return roomPath.substr(0, schematic) + ".symbol.room";
    }
    return roomPath;
}

std::string resolveSymbolCorePath(const Instance &inst, const PrimitiveResolver &resolver)
{
    if (const std::string *primitive = findProperty(inst.properties(), "core.primitive"); primitive && !primitive->empty()) {
        const ResolvedPrimitive resolved = resolver.resolveReference(*primitive);
        if (resolved.found && !resolved.roomPath.empty()) {
            return symbolCorePathFromAnyCorePath(resolved.roomPath);
        }
    }

    if (const std::string *p0 = findProperty(inst.properties(), "param.0")) {
        if (const std::string *p1 = findProperty(inst.properties(), "param.1"); p1 && !p0->empty() && !p1->empty()) {
            const ResolvedPrimitive resolved = resolver.resolveReference(*p0 + "/" + *p1);
            if (resolved.found && !resolved.roomPath.empty()) {
                return symbolCorePathFromAnyCorePath(resolved.roomPath);
            }
        }
    }

    const std::string model = pinRetargetModelName(inst);
    if (!model.empty()) {
        const ResolvedPrimitive resolved = resolver.resolveReference(model);
        if (resolved.found && !resolved.roomPath.empty()) {
            return symbolCorePathFromAnyCorePath(resolved.roomPath);
        }
    }
    return {};
}

bool isPortLikeInstance(const Instance &inst)
{
    const std::string type = logicalType(inst);
    const std::string stem = cellStem(inst.cellName());
    return type == "Port" || stem == "lab_pin" || stem == "iopin" || stem == "ipin" || stem == "opin";
}

std::string portNetName(const Instance &inst)
{
    if (const std::string *lab = findProperty(inst.properties(), "lab"); lab && !lab->empty()) {
        return *lab;
    }
    if (const std::string *p0 = findProperty(inst.properties(), "param.0"); p0 && !p0->empty()) {
        return *p0;
    }
    return {};
}

bool isSupplyInstance(const Instance &inst)
{
    const std::string type = logicalType(inst);
    return type == "Vdc" || type == "Vpulse" || type == "Vac" || type == "Vexp" || type == "Vrect" || type == "Vfile"
        || type == "Vpwl" || type == "Varith" || cellStem(inst.cellName()) == "vsource";
}

bool isGroundInstance(const Instance &inst)
{
    const std::string type = logicalType(inst);
    const std::string stem = cellStem(inst.cellName());
    return type == "GND" || stem == "gnd";
}

bool isHierarchyInstance(const Instance &inst)
{
    const std::string stem = cellStem(inst.cellName());
    if (stem == "Lib" || stem == "qucs_blackbox") {
        return true;
    }
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && *qt == "Lib") {
        return true;
    }
    return false;
}

struct NetAnchor {
    Point position;
    std::string name;
};

void appendSymbolPinAnchors(const Instance &inst, const PrimitiveResolver &resolver, double dbuPerEditorUnit,
                            std::vector<NetAnchor> &anchors)
{
    if (!isHierarchyInstance(inst)) {
        return;
    }
    const std::string symbolPath = resolveSymbolCorePath(inst, resolver);
    if (symbolPath.empty()) {
        return;
    }

    std::ifstream probe(symbolPath, std::ios::binary);
    if (!probe) {
        return;
    }
    probe.close();

    try {
        const Database db = Database::loadFromFile(symbolPath);
        const ParsedRoomPath parsed = parseRoomFilePath(symbolPath);
        if (!parsed.valid) {
            return;
        }
        const Cell *cell = db.lib().findCell(parsed.cellName);
        if (cell == nullptr) {
            return;
        }
        const CellContent *content = cell->findContent(ViewType::Symbol);
        if (content == nullptr) {
            return;
        }

        for (const Shape &shape : content->block().shapes()) {
            if (shape.type() != Shape::Type::Rect) {
                continue;
            }
            const Shape::RectData *rect = shape.rect();
            if (rect == nullptr) {
                continue;
            }
            if (layerPurpose(content->layers(), rect->layerId) != LayerPurpose::Pin) {
                continue;
            }
            const std::string pinName = pinLabelFromProperties(shape.properties());
            if (pinName.empty() || isAnonymousNetLabel(pinName)) {
                continue;
            }
            const Point local{(rect->box.llx + rect->box.urx) / 2, (rect->box.lly + rect->box.ury) / 2};
            anchors.push_back(NetAnchor{transformLocal(local, inst.transform()), pinName});
        }
    } catch (...) {
        return;
    }
}

std::vector<NetAnchor> collectNetAnchors(const Block &block, const PrimitiveResolver *resolver, double dbuPerEditorUnit)
{
    std::vector<NetAnchor> anchors;
    for (const Instance &inst : block.instances()) {
        if (isPortLikeInstance(inst)) {
            const std::string name = portNetName(inst);
            if (!name.empty() && !isAnonymousNetLabel(name)) {
                anchors.push_back(NetAnchor{Point{inst.transform().x, inst.transform().y}, name});
            }
            continue;
        }
        if (isSupplyInstance(inst)) {
            if (const std::string *compName = findProperty(inst.properties(), "name");
                compName != nullptr && !compName->empty() && !isAnonymousNetLabel(*compName)) {
                Point pos{inst.transform().x, inst.transform().y};
                const double scale = dbuPerEditorUnit > 0.0 ? dbuPerEditorUnit : 1.0;
                const std::int64_t offset = static_cast<std::int64_t>(std::llround(30.0 * scale));
                int rotate = 0;
                if (const std::string *rot = findProperty(inst.properties(), "rotate"); rot != nullptr && !rot->empty()) {
                    rotate = std::stoi(*rot);
                } else if (const std::string *qucsRot = findProperty(inst.properties(), "qucs.rotate");
                           qucsRot != nullptr && !qucsRot->empty()) {
                    rotate = std::stoi(*qucsRot);
                }
                switch ((rotate % 4 + 4) % 4) {
                case 1:
                    pos.x += offset;
                    break;
                case 2:
                    pos.y -= offset;
                    break;
                case 3:
                    pos.x -= offset;
                    break;
                default:
                    pos.y += offset;
                    break;
                }
                anchors.push_back(NetAnchor{pos, *compName});
            }
            continue;
        }
        if (isGroundInstance(inst)) {
            anchors.push_back(NetAnchor{Point{inst.transform().x, inst.transform().y}, "GND"});
            continue;
        }
        if (resolver != nullptr && isHierarchyInstance(inst)) {
            appendSymbolPinAnchors(inst, *resolver, dbuPerEditorUnit, anchors);
        }
    }
    return anchors;
}

void applyAnchorsToWireProps(const std::vector<NetAnchor> &anchors, double dbuPerEditorUnit,
                             std::vector<Shape::PathData> &wirePaths,
                             std::vector<std::vector<Property>> &wireProps)
{
    const std::int64_t snapTol = editorUnitsToDbu(3.0, dbuPerEditorUnit);
    const std::int64_t reachTol = editorUnitsToDbu(40.0, dbuPerEditorUnit);

    for (std::size_t i = 0; i < wirePaths.size() && i < wireProps.size(); ++i) {
        const std::vector<Point> &pts = wirePaths[i].points;
        if (pts.size() < 2) {
            continue;
        }
        const std::string existing = wireLabelFromProps(wireProps[i]);
        if (!existing.empty() && !isAnonymousNetLabel(existing)) {
            continue;
        }

        for (const NetAnchor &anchor : anchors) {
            const bool touches = pointNear(pts.front(), anchor.position, snapTol)
                || pointNear(pts.back(), anchor.position, snapTol)
                || pointOnWireSegment(anchor.position, pts.front(), pts.back(), reachTol);
            if (!touches) {
                continue;
            }
            setWireLabel(wireProps[i], anchor.name, pts, dbuPerEditorUnit);
            break;
        }
    }
}

struct EndpointKey {
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const EndpointKey &other) const { return x == other.x && y == other.y; }
};

struct EndpointKeyHash {
    std::size_t operator()(const EndpointKey &key) const
    {
        return std::hash<std::int64_t>()(key.x) ^ (std::hash<std::int64_t>()(key.y) << 1);
    }
};

std::int64_t quantizeCoord(std::int64_t value, std::int64_t tol)
{
    if (tol <= 0) {
        return value;
    }
    return (value + (value >= 0 ? tol / 2 : -tol / 2)) / tol * tol;
}

EndpointKey endpointKey(const Point &pt, std::int64_t tol)
{
    return EndpointKey{quantizeCoord(pt.x, tol), quantizeCoord(pt.y, tol)};
}

std::size_t findWireRoot(std::vector<std::size_t> &parent, std::size_t index)
{
    while (parent[index] != index) {
        parent[index] = parent[parent[index]];
        index = parent[index];
    }
    return index;
}

void unionWireRoots(std::vector<std::size_t> &parent, std::size_t a, std::size_t b)
{
    const std::size_t rootA = findWireRoot(parent, a);
    const std::size_t rootB = findWireRoot(parent, b);
    if (rootA != rootB) {
        parent[rootB] = rootA;
    }
}

void floodFillWireNetLabels(const std::vector<Shape::PathData> &wirePaths, std::vector<std::vector<Property>> &wireProps,
                            double dbuPerEditorUnit)
{
    if (wirePaths.empty() || wirePaths.size() != wireProps.size()) {
        return;
    }

    const std::int64_t tol = std::max<std::int64_t>(1, editorUnitsToDbu(2.0, dbuPerEditorUnit));
    std::vector<std::size_t> parent(wirePaths.size());
    std::iota(parent.begin(), parent.end(), 0);

    std::unordered_map<EndpointKey, std::vector<std::size_t>, EndpointKeyHash> endpointToWires;
    endpointToWires.reserve(wirePaths.size() * 2);
    for (std::size_t i = 0; i < wirePaths.size(); ++i) {
        const std::vector<Point> &pts = wirePaths[i].points;
        if (pts.size() < 2) {
            continue;
        }
        endpointToWires[endpointKey(pts.front(), tol)].push_back(i);
        endpointToWires[endpointKey(pts.back(), tol)].push_back(i);
    }
    for (const auto &entry : endpointToWires) {
        const std::vector<std::size_t> &wires = entry.second;
        for (std::size_t j = 1; j < wires.size(); ++j) {
            unionWireRoots(parent, wires.front(), wires[j]);
        }
    }

    std::unordered_map<std::size_t, std::string> rootLabels;
    for (std::size_t i = 0; i < wirePaths.size(); ++i) {
        const std::string label = wireLabelFromProps(wireProps[i]);
        if (label.empty() || isAnonymousNetLabel(label)) {
            continue;
        }
        const std::size_t root = findWireRoot(parent, i);
        const auto existing = rootLabels.find(root);
        if (existing == rootLabels.end() || isAnonymousNetLabel(existing->second)) {
            rootLabels.emplace(root, label);
        }
    }

    for (std::size_t i = 0; i < wirePaths.size(); ++i) {
        const std::size_t root = findWireRoot(parent, i);
        const auto label = rootLabels.find(root);
        if (label == rootLabels.end()) {
            continue;
        }
        const std::vector<Point> &pts = wirePaths[i].points;
        if (pts.size() < 2) {
            continue;
        }
        setWireLabel(wireProps[i], label->second, pts, dbuPerEditorUnit);
    }
}

void propagateBlockNetNamesFromWireLabels(Block &block, const std::vector<Shape::PathData> &wirePaths,
                                          const std::vector<std::vector<Property>> &wireProps, double dbuPerEditorUnit)
{
    const std::int64_t tol = std::max<std::int64_t>(1, editorUnitsToDbu(2.0, dbuPerEditorUnit));
    std::unordered_map<EndpointKey, std::string, EndpointKeyHash> endpointLabels;
    for (std::size_t i = 0; i < wirePaths.size() && i < wireProps.size(); ++i) {
        const std::string label = wireLabelFromProps(wireProps[i]);
        if (label.empty() || isAnonymousNetLabel(label)) {
            continue;
        }
        const std::vector<Point> &pts = wirePaths[i].points;
        if (pts.size() < 2) {
            continue;
        }
        endpointLabels.emplace(endpointKey(pts.front(), tol), label);
        endpointLabels.emplace(endpointKey(pts.back(), tol), label);
    }

    for (Net &net : block.nets()) {
        if (!isAnonymousNetLabel(net.name())) {
            continue;
        }
        for (const Term &term : net.terms()) {
            const auto found = endpointLabels.find(endpointKey(term.position(), tol));
            if (found != endpointLabels.end()) {
                net.setName(found->second);
                break;
            }
        }
    }
}

void propagatePortNamesFromAdjacentWires(Block &block, const std::vector<Shape::PathData> &wirePaths,
                                         const std::vector<std::vector<Property>> &wireProps, double dbuPerEditorUnit)
{
    const std::int64_t snapTol = std::max<std::int64_t>(1, editorUnitsToDbu(5.0, dbuPerEditorUnit));
    for (Instance &inst : block.instances()) {
        if (!isPortLikeInstance(inst)) {
            continue;
        }
        const std::string existing = portNetName(inst);
        if (!existing.empty() && !isAnonymousNetLabel(existing)) {
            continue;
        }
        const Point pos{inst.transform().x, inst.transform().y};
        std::string bestLabel;
        std::int64_t bestDist = std::numeric_limits<std::int64_t>::max();
        for (std::size_t i = 0; i < wirePaths.size() && i < wireProps.size(); ++i) {
            const std::string label = wireLabelFromProps(wireProps[i]);
            if (label.empty() || isAnonymousNetLabel(label)) {
                continue;
            }
            const std::vector<Point> &pts = wirePaths[i].points;
            if (pts.size() < 2) {
                continue;
            }
            for (const Point &endpoint : {pts.front(), pts.back()}) {
                const std::int64_t dx = endpoint.x - pos.x;
                const std::int64_t dy = endpoint.y - pos.y;
                const std::int64_t dist = dx * dx + dy * dy;
                if (dist <= snapTol * snapTol && dist < bestDist) {
                    bestDist = dist;
                    bestLabel = label;
                }
            }
        }
        if (bestLabel.empty()) {
            continue;
        }
        setProperty(inst.properties(), "param.0", bestLabel);
        setProperty(inst.properties(), "lab", bestLabel);
    }
}

std::string canonicalQucsType(const Instance &inst)
{
    const std::string stem = cellStem(inst.cellName());
    if (stem == "res") {
        return "R";
    }
    if (stem == "capa") {
        return "C";
    }
    if (stem == "ind") {
        return "L";
    }
    if (stem == "gnd") {
        return "GND";
    }
    const std::string type = logicalType(inst);
    if (type == "TR") {
        return ".TR";
    }
    return type;
}

bool isCanonicalAnalogPrimitive(const std::string &type)
{
    return type == "R" || type == "C" || type == "L" || type == "GND" || type == "Vdc" || type == "Vac"
        || type == "Vpulse" || type == "Vexp" || type == "Vrect" || type == "Vfile" || type == "Vpwl" || type == "Varith"
        || type == "Idc" || type == "Iac" || type == "Ipulse" || type == "Iexp" || type == "Irect" || type == "Ifile"
        || type == "Ipwl" || type == "Iarith" || type == "INCLSCR" || type == "SpiceLib" || type == ".TR"
        || type == "Port";
}

} // namespace

bool isAnonymousNetLabel(const std::string &label)
{
    if (label.empty() || label == "GND" || label == "gnd") {
        return false;
    }
    if (label.rfind("N$", 0) == 0) {
        return true;
    }
    if (label.size() >= 4 && label.rfind("net", 0) == 0) {
        return std::all_of(label.begin() + 3, label.end(),
                          [](unsigned char ch) { return std::isdigit(ch) != 0; });
    }
    return false;
}

void canonicalizeBlockPrimitives(Block &block, const PrimitiveResolver *resolver)
{
    if (resolver == nullptr) {
        return;
    }
    for (Instance &inst : block.instances()) {
        const std::string canonical = canonicalQucsType(inst);
        if (!isCanonicalAnalogPrimitive(canonical)) {
            continue;
        }
        setProperty(inst.properties(), "qucs.type", canonical);

        std::string ref = "analogLib/" + canonical + ".symbol.room";
        if (canonical == ".TR") {
            ref = "analogLib/TR.symbol.room";
        } else if (canonical == "INCLSCR" || canonical == "SpiceLib") {
            ref = "analogLib/INCLSCR.symbol.room";
        } else if (canonical == "Port") {
            ref = "commonLib/lab_pin.symbol.room";
        } else if (canonical == "GND") {
            ref = "analogLib/GND.symbol.room";
        }

        const ResolvedPrimitive resolved = resolver->resolveReference(ref);
        if (resolved.found) {
            setProperty(inst.properties(), "core.primitive", resolved.logicalRef.empty() ? ref : resolved.logicalRef);
        } else {
            setProperty(inst.properties(), "core.primitive", ref);
        }
    }
}

void propagateNetNames(Block &block, const PrimitiveResolver *resolver, double dbuPerEditorUnit)
{
    std::vector<std::vector<Property>> wireProps;
    std::vector<Shape::PathData> wirePaths;
    std::vector<Shape *> wireShapes;

    for (Shape &shape : block.shapes()) {
        if (shape.type() != Shape::Type::Path) {
            continue;
        }
        const Shape::PathData *path = shape.path();
        if (path == nullptr || path->points.size() < 2) {
            continue;
        }
        wirePaths.push_back(*path);
        wireProps.push_back(shape.properties());
        wireShapes.push_back(&shape);
    }

    if (wirePaths.empty()) {
        return;
    }

    const std::vector<NetAnchor> anchors = collectNetAnchors(block, resolver, dbuPerEditorUnit);
    applyAnchorsToWireProps(anchors, dbuPerEditorUnit, wirePaths, wireProps);
    floodFillWireNetLabels(wirePaths, wireProps, dbuPerEditorUnit);
    propagateBlockNetNamesFromWireLabels(block, wirePaths, wireProps, dbuPerEditorUnit);
    propagatePortNamesFromAdjacentWires(block, wirePaths, wireProps, dbuPerEditorUnit);

    for (std::size_t i = 0; i < wireShapes.size(); ++i) {
        wireShapes[i]->properties() = std::move(wireProps[i]);
    }

    for (Instance &inst : block.instances()) {
        if (!isPortLikeInstance(inst)) {
            continue;
        }
        const std::string desired = portNetName(inst);
        if (desired.empty() || isAnonymousNetLabel(desired)) {
            continue;
        }
        if (const std::string *p0 = findProperty(inst.properties(), "param.0"); p0 == nullptr || *p0 != desired) {
            setProperty(inst.properties(), "param.0", desired);
        }
        if (const std::string *lab = findProperty(inst.properties(), "lab"); lab == nullptr || *lab != desired) {
            setProperty(inst.properties(), "lab", desired);
        }
    }
}

} // namespace room
