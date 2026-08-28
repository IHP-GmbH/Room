/*!****************************************************************************************
 * \file qucs_exporter.cpp
 * \brief Qucs .sch exporter from CORE schematic views.
 *****************************************************************************************/

#include "qucs_exporter.h"

#include "cell_content.h"
#include "coord_scale.h"
#include "enums.h"
#include "layer_spec.h"
#include "net.h"
#include "pin_retarget.h"
#include "primitive_resolver.h"
#include "property.h"
#include "shape.h"

#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace core {
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

std::vector<std::string> collectProperties(const std::vector<Property> &props, const std::string &prefix)
{
    std::vector<std::string> lines;
    for (const Property &prop : props) {
        if (prop.name == prefix) {
            lines.push_back(prop.value);
        }
    }
    return lines;
}

std::string normalizeQucsViewPropertyLine(const std::string &line, const std::string &cellName)
{
    if (cellName.empty()) {
        return line;
    }
    std::string normalized = line;
    const std::string schematicStem = cellName + ".schematic";
    const std::string replacement = cellName;
    for (const char *key : {"DataSet=", "DataDisplay="}) {
        const std::string marker = std::string(key) + schematicStem;
        std::size_t pos = 0;
        while ((pos = normalized.find(marker, pos)) != std::string::npos) {
            normalized.replace(pos, marker.size(), std::string(key) + replacement);
            pos += std::string(key).size() + replacement.size();
        }
    }
    return normalized;
}

int orientToQucsRotate(Orient orient)
{
    int mirror = 0;
    int rotate = 0;
    orientToQucsPlacement(orient, mirror, rotate);
    return rotate;
}

int orientToQucsMirror(Orient orient)
{
    int mirror = 0;
    int rotate = 0;
    orientToQucsPlacement(orient, mirror, rotate);
    return mirror;
}

std::string quote(const std::string &value)
{
    return '"' + value + '"';
}

std::string xschemCellStem(const std::string &cellName)
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

std::string qucsComponentType(const std::string &cellName);

bool shouldUseLibPrimitiveExport(const ResolvedPrimitive &resolved)
{
    const std::string stem = xschemCellStem(resolved.cellName);
    if (stem.rfind("title", 0) == 0) {
        return true;
    }
    if (isQucsSchematicDecoration(resolved.cellName)) {
        return false;
    }
    // Use <Lib> only for cells without a built-in Qucs component mapping (PDK, hierarchy).
    return qucsComponentType(resolved.cellName) == stem;
}

std::string qucsComponentType(const std::string &cellName)
{
    const std::string type = xschemCellStem(cellName);
    // Fallback only when commonLib is not attached: keep native Qucs Port.
    if (type == "iopin" || type == "ipin" || type == "opin" || type == "Port") {
        return "Port";
    }
    if (type == "gnd") {
        return "GND";
    }
    if (type == "vsource") {
        return "Vpulse";
    }
    if (type == "isource") {
        return "Idc";
    }
    if (type == "res") {
        return "R";
    }
    if (type == "capa") {
        return "C";
    }
    if (type == "ind") {
        return "L";
    }
    if (type == "lab_pin") {
        return "Port";
    }
    // Xschem-only decoration; Qucs has no equivalent — load as inert subcircuit placeholder.
    if (type == "lab_wire" || type == "code_shown" || type == "launcher") {
        return "Sub";
    }
    if (type.rfind("title", 0) == 0) {
        return "Sub";
    }
    return type;
}

std::string qucsPortDirection(const std::string &cellName)
{
    std::string type = cellName;
    const auto slash = type.find_last_of("/\\");
    if (slash != std::string::npos) {
        type = type.substr(slash + 1);
    }
    if (type.size() > 4 && type.compare(type.size() - 4, 4, ".sym") == 0) {
        type.resize(type.size() - 4);
    }
    if (type == "ipin") {
        return "in";
    }
    if (type == "opin") {
        return "out";
    }
    return "analog";
}

bool isReservedPropertyName(const std::string &name)
{
    return name == "name" || name == "active" || name == "textX" || name == "textY" || name == "mirror" || name == "rotate"
        || name.rfind("param.", 0) == 0 || name.rfind("visible.", 0) == 0 || name.rfind("core.", 0) == 0
        || name == "qucs.type" || name == "symname" || name == "model" || name == "spiceprefix";
}

std::optional<std::string> formatLibPrimitiveLine(const Instance &inst, double dbuPerEditorUnit,
                                                  const ResolvedPrimitive &resolved, const std::string &qucsLibrary)
{
    const std::string instName = findProperty(inst.properties(), "name") ? *findProperty(inst.properties(), "name")
                                                                         : resolved.cellName;
    const int active = findProperty(inst.properties(), "active") ? std::stoi(*findProperty(inst.properties(), "active")) : 1;
    const std::int64_t textX = findProperty(inst.properties(), "textX")
                                   ? static_cast<std::int64_t>(std::llround(dbuToEditorUnits(
                                         std::stoll(*findProperty(inst.properties(), "textX")), dbuPerEditorUnit)))
                                   : 0;
    const std::int64_t textY = findProperty(inst.properties(), "textY")
                                   ? static_cast<std::int64_t>(std::llround(dbuToEditorUnits(
                                         std::stoll(*findProperty(inst.properties(), "textY")), dbuPerEditorUnit)))
                                   : 0;
    // Prefer CORE transform over possibly stale Qucs mirror/rotate props so LibComp pins
    // align with retargeted wire endpoints (Xschem-origin schematics).
    const int mirror = orientToQucsMirror(inst.transform().orient);
    const int rotate = orientToQucsRotate(inst.transform().orient);

    std::string compName = resolved.cellName;
    if (const std::string *model = findProperty(inst.properties(), "model")) {
        compName = *model;
    }

    const std::string &libName = !resolved.techLibrary.empty() ? resolved.techLibrary : qucsLibrary;

    std::ostringstream oss;
    oss << "<Lib " << instName << ' ' << active << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().y, dbuPerEditorUnit))) << ' '
        << textX << ' ' << textY << ' ' << mirror << ' ' << rotate << ' ' << quote(libName) << " 0 "
        << quote(compName) << " 0";

    static const char *kParamOrder[] = {"l", "w", "ng", "m", "nf", "nr", "lab"};
    for (const char *key : kParamOrder) {
        if (const std::string *val = findProperty(inst.properties(), key)) {
            if (!val->empty()) {
                oss << ' ' << quote(*val) << " 1";
            }
        }
    }
    // Xschem iopin stores net name as "lab"; also accept param.0 from Qucs Port import.
    if (!findProperty(inst.properties(), "lab")) {
        if (const std::string *lab = findProperty(inst.properties(), "param.0")) {
            if (!lab->empty()) {
                oss << ' ' << quote(*lab) << " 1";
            }
        }
    }
    oss << '>';
    return oss.str();
}

std::optional<std::string> maybeFormatPrimitiveLine(const Instance &inst, double dbuPerEditorUnit,
                                                    const PrimitiveResolver &resolver, const std::string &qucsLibrary)
{
    std::string ref = inst.cellName();
    if (const std::string *coreRef = findProperty(inst.properties(), "core.primitive")) {
        ref = *coreRef;
    }

    const ResolvedPrimitive resolved = resolver.resolveReference(ref);
    if (!resolved.found) {
        return std::nullopt;
    }
    // commonLib/analogLib and xschem devices must not become <Lib> in Qucs.
    if (!shouldUseLibPrimitiveExport(resolved)) {
        return std::nullopt;
    }

    return formatLibPrimitiveLine(inst, dbuPerEditorUnit, resolved, qucsLibrary);
}

std::string formatComponentLine(const Instance &inst, double dbuPerEditorUnit, const PrimitiveResolver *resolver,
                                const std::string &qucsLibrary)
{
    if (resolver != nullptr) {
        if (const std::optional<std::string> primitive = maybeFormatPrimitiveLine(inst, dbuPerEditorUnit, *resolver, qucsLibrary)) {
            return *primitive;
        }
    }

    const std::string type = qucsComponentType(inst.cellName());
    const std::string instName = findProperty(inst.properties(), "name") ? *findProperty(inst.properties(), "name")
                                                                         : inst.cellName();
    const int active = findProperty(inst.properties(), "active") ? std::stoi(*findProperty(inst.properties(), "active")) : 1;
    const std::int64_t textX = findProperty(inst.properties(), "textX")
                                   ? static_cast<std::int64_t>(std::llround(dbuToEditorUnits(
                                         std::stoll(*findProperty(inst.properties(), "textX")), dbuPerEditorUnit)))
                                   : 0;
    const std::int64_t textY = findProperty(inst.properties(), "textY")
                                   ? static_cast<std::int64_t>(std::llround(dbuToEditorUnits(
                                         std::stoll(*findProperty(inst.properties(), "textY")), dbuPerEditorUnit)))
                                   : 0;
    const int mirror = findProperty(inst.properties(), "mirror") ? std::stoi(*findProperty(inst.properties(), "mirror"))
                                                                    : orientToQucsMirror(inst.transform().orient);
    const int rotate = findProperty(inst.properties(), "rotate") ? std::stoi(*findProperty(inst.properties(), "rotate"))
                                                                  : orientToQucsRotate(inst.transform().orient);

    if (type == "Port") {
        const std::string portNum = findProperty(inst.properties(), "lab") ? *findProperty(inst.properties(), "lab")
            : (findProperty(inst.properties(), "param.0") ? *findProperty(inst.properties(), "param.0") : instName);
        std::string portType = qucsPortDirection(inst.cellName());
        if (const std::string *t = findProperty(inst.properties(), "param.1"); t && !t->empty()) {
            portType = *t;
        }
        std::ostringstream oss;
        oss << "<Port " << instName << ' ' << active << ' '
            << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().x, dbuPerEditorUnit))) << ' '
            << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().y, dbuPerEditorUnit))) << ' '
            << textX << ' ' << textY << ' ' << mirror << ' ' << rotate << ' ' << quote(portNum) << " 1 "
            << quote(portType) << " 0 " << quote("v") << " 0 \"\" 0>";
        return oss.str();
    }

    std::ostringstream oss;
    oss << '<' << type << ' ' << instName << ' ' << active << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().y, dbuPerEditorUnit)))
        << ' ' << textX << ' ' << textY << ' ' << mirror << ' ' << rotate;

    for (std::size_t i = 0;; ++i) {
        const std::string propKey = "param." + std::to_string(i);
        const std::string visKey = "visible." + std::to_string(i);
        const std::string *prop = findProperty(inst.properties(), propKey);
        const std::string *vis = findProperty(inst.properties(), visKey);
        if (!prop || !vis) {
            break;
        }
        oss << ' ' << quote(*prop) << ' ' << *vis;
    }
    oss << '>';
    return oss.str();
}

void appendWireSegment(std::vector<std::string> &lines, const Point &a, const Point &b, const std::string &label,
                       std::int64_t labelX, std::int64_t labelY, std::int64_t dist, double dbuPerEditorUnit,
                       const std::string &nodeSet = "")
{
    std::ostringstream oss;
    oss << '<' << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(a.x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(a.y, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(b.x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(b.y, dbuPerEditorUnit))) << ' ' << quote(label)
        << ' ' << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(labelX, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(labelY, dbuPerEditorUnit))) << ' ' << dist << ' '
        << quote(nodeSet) << '>';
    lines.push_back(oss.str());
}

void connectPoints(std::vector<std::string> &lines, const Point &a, const Point &b, const std::string &label,
                   double dbuPerEditorUnit)
{
    if (a.x == b.x && a.y == b.y) {
        return;
    }
    if (a.x == b.x || a.y == b.y) {
        appendWireSegment(lines, a, b, label, 0, 0, 0, dbuPerEditorUnit);
        return;
    }
    appendWireSegment(lines, a, {b.x, a.y}, label, 0, 0, 0, dbuPerEditorUnit);
    appendWireSegment(lines, {b.x, a.y}, b, "", 0, 0, 0, dbuPerEditorUnit);
}

std::vector<std::string> formatWiresFromBlock(const Block &block, const std::vector<LayerSpec> &layers,
                                              double dbuPerEditorUnit,
                                              const std::vector<std::pair<Point, Point>> &pinRetargets)
{
    std::vector<std::vector<Point>> polylines;
    std::vector<std::vector<Property>> propSets;

    for (const Shape &shape : block.shapes()) {
        if (shape.type() != Shape::Type::Path) {
            continue;
        }
        const Shape::PathData *path = shape.path();
        if (path == nullptr || path->points.size() < 2) {
            continue;
        }
        const LayerSpec *layer = nullptr;
        if (path->layerId < layers.size()) {
            layer = &layers[path->layerId];
        }
        if (layer != nullptr && layer->purpose != LayerPurpose::Wire) {
            continue;
        }
        polylines.push_back(path->points);
        propSets.push_back(shape.properties());
    }

    if (!pinRetargets.empty() && !polylines.empty()) {
        retargetWirePolylinesQucsToCore(polylines, pinRetargets, dbuPerEditorUnit);
    }

    std::vector<std::string> lines;
    for (std::size_t i = 0; i < polylines.size(); ++i) {
        const auto &pts = polylines[i];
        if (pts.size() < 2) {
            continue;
        }
        const auto &props = propSets[i];
        const std::string label = findProperty(props, "label") ? *findProperty(props, "label") : "";
        const std::int64_t labelX = findProperty(props, "labelX") ? std::stoll(*findProperty(props, "labelX")) : 0;
        const std::int64_t labelY = findProperty(props, "labelY") ? std::stoll(*findProperty(props, "labelY")) : 0;
        const std::int64_t dist = findProperty(props, "dist") ? std::stoll(*findProperty(props, "dist")) : 0;
        const std::string nodeSet = findProperty(props, "nodeSet") ? *findProperty(props, "nodeSet") : "";
        appendWireSegment(lines, pts[0], pts[1], label, labelX, labelY, dist, dbuPerEditorUnit, nodeSet);
    }
    if (!lines.empty()) {
        return lines;
    }

    for (const Net &net : block.nets()) {
        if (net.terms().empty()) {
            continue;
        }
        const std::string label = net.name().rfind("N$", 0) == 0 ? "" : net.name();
        for (std::size_t i = 1; i < net.terms().size(); ++i) {
            Point a = net.terms()[i - 1].position();
            Point b = net.terms()[i].position();
            if (!pinRetargets.empty()) {
                retargetPoint(a, pinRetargets, dbuPerEditorUnit);
                retargetPoint(b, pinRetargets, dbuPerEditorUnit);
            }
            connectPoints(lines, a, b, i == 1 ? label : "", dbuPerEditorUnit);
        }
    }
    return lines;
}

void writeSection(std::ostream &out, const std::string &name, const std::vector<std::string> &lines)
{
    if (lines.empty()) {
        return;
    }
    out << '<' << name << ">\n";
    for (const std::string &line : lines) {
        out << line << '\n';
    }
    out << "</" << name << ">\n";
}

LayerPurpose layerPurpose(const CellContent &content, std::uint32_t layerId)
{
    for (const LayerSpec &layer : content.layers()) {
        if (layer.layerNum == layerId) {
            return layer.purpose;
        }
    }
    return LayerPurpose::Drawing;
}

std::uint32_t shapeLayerId(const Shape &shape)
{
    switch (shape.type()) {
    case Shape::Type::Rect:
        return shape.rect()->layerId;
    case Shape::Type::Polygon:
        return shape.polygon()->layerId;
    case Shape::Type::Path:
        return shape.path()->layerId;
    case Shape::Type::Text:
        return shape.text()->layerId;
    case Shape::Type::Arc:
        return shape.arc()->layerId;
    }
    return 0;
}

std::int64_t toQucsCoord(std::int64_t dbu, double dbuPerEditorUnit)
{
    return static_cast<std::int64_t>(std::llround(dbuToEditorUnits(dbu, dbuPerEditorUnit)));
}

void appendQucsLine(std::vector<std::string> &lines, std::int64_t x1, std::int64_t y1, std::int64_t x2, std::int64_t y2)
{
    std::ostringstream oss;
    oss << "<Line " << x1 << ' ' << y1 << ' ' << (x2 - x1) << ' ' << (y2 - y1) << " #000000 0 1>";
    lines.push_back(oss.str());
}

void arcAnglesToQucsDegrees(const Shape::ArcData &arc, double &startDeg, double &spanDeg)
{
    constexpr double kPi = 3.14159265358979323846;
    // Xschem stores start/span in degrees (endAngle field is the span parameter).
    if (arc.startAngle > 2.0 * kPi || arc.endAngle > 2.0 * kPi) {
        startDeg = arc.startAngle;
        spanDeg = arc.endAngle;
        return;
    }
    // Qucs import stores absolute angles in radians.
    startDeg = arc.startAngle * 180.0 / kPi;
    spanDeg = (arc.endAngle - arc.startAngle) * 180.0 / kPi;
}

void appendQucsArc(std::vector<std::string> &lines, const Shape::ArcData &arc, double dbuPerEditorUnit)
{
    double startDeg = 0.0;
    double spanDeg = 0.0;
    arcAnglesToQucsDegrees(arc, startDeg, spanDeg);

    const double cx = dbuToEditorUnits(arc.center.x, dbuPerEditorUnit);
    const double cy = dbuToEditorUnits(arc.center.y, dbuPerEditorUnit);
    const double diameter = std::max(arc.radius * 2.0, 1.0);
    const std::int64_t x = static_cast<std::int64_t>(std::llround(cx - arc.radius));
    const std::int64_t y = static_cast<std::int64_t>(std::llround(cy - arc.radius));
    const std::int64_t size = static_cast<std::int64_t>(std::llround(diameter));
    const std::int64_t startQt = static_cast<std::int64_t>(std::llround(startDeg * 16.0));
    const std::int64_t spanQt = static_cast<std::int64_t>(std::llround(spanDeg * 16.0));

    std::ostringstream oss;
    oss << "<EArc " << x << ' ' << y << ' ' << size << ' ' << size << ' ' << startQt << ' ' << spanQt
        << " #000000 0 1>";
    lines.push_back(oss.str());
}

bool appendQucsArcFromProperties(std::vector<std::string> &lines, const std::vector<Property> &props,
                                 double dbuPerEditorUnit)
{
    if (const std::string *geometry = findProperty(props, "geometry"); geometry == nullptr || *geometry != "arc") {
        return false;
    }
    Shape::ArcData arc;
    arc.radius = 1.0;
    if (const std::string *radius = findProperty(props, "arc.radius")) {
        arc.radius = std::stod(*radius);
    }
    if (const std::string *start = findProperty(props, "arc.startAngle")) {
        arc.startAngle = std::stod(*start);
    }
    if (const std::string *end = findProperty(props, "arc.endAngle")) {
        arc.endAngle = std::stod(*end);
    }
    if (const std::string *cx = findProperty(props, "arc.centerX")) {
        arc.center.x = std::stoll(*cx);
    }
    if (const std::string *cy = findProperty(props, "arc.centerY")) {
        arc.center.y = std::stoll(*cy);
    }
    appendQucsArc(lines, arc, dbuPerEditorUnit);
    return true;
}

int pinDirectionToAngle(const std::vector<Property> &props)
{
    const std::string *direction = findProperty(props, "direction");
    if (!direction) {
        return 0;
    }
    if (*direction == "0" || *direction == "right" || *direction == "R") {
        return 0;
    }
    if (*direction == "1" || *direction == "up" || *direction == "U") {
        return 90;
    }
    if (*direction == "2" || *direction == "left" || *direction == "L") {
        return 180;
    }
    if (*direction == "3" || *direction == "down" || *direction == "D") {
        return 270;
    }
    return 0;
}

std::string pinNameFromProperties(const std::vector<Property> &props)
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
    return "p";
}

std::string pinNumberFromProperties(const std::vector<Property> &props)
{
    if (const std::string *number = findProperty(props, "pinnumber")) {
        return *number;
    }
    return "1";
}

void appendQucsPort(std::vector<std::string> &lines, std::int64_t cx, std::int64_t cy, const std::vector<Property> &props)
{
    std::ostringstream oss;
    oss << "<.PortSym " << cx << ' ' << cy << ' ' << pinNumberFromProperties(props) << ' '
        << pinDirectionToAngle(props) << ' ' << pinNameFromProperties(props) << '>';
    lines.push_back(oss.str());
}

void appendQucsText(std::vector<std::string> &lines, const Shape::TextData &text, const std::vector<Property> &props,
                    double dbuPerEditorUnit)
{
    if (text.text.empty()) {
        return;
    }
    // Skip Xschem attribute templates (@lab, @spice_get_voltage, …) — they are not Qucs labels.
    if (text.text.front() == '@') {
        return;
    }
    const int rotate = findProperty(props, "rotate") ? std::stoi(*findProperty(props, "rotate")) : 0;
    const std::int64_t x = toQucsCoord(text.position.x, dbuPerEditorUnit);
    const std::int64_t y = toQucsCoord(text.position.y, dbuPerEditorUnit);
    const int fontSize = static_cast<int>(std::llround(dbuToEditorUnits(static_cast<std::int64_t>(text.height), dbuPerEditorUnit)));
    std::ostringstream oss;
    oss << "<Text " << x << ' ' << y << ' ' << std::max(fontSize, 8) << " #000000 " << rotate << " \"" << text.text
        << "\">";
    lines.push_back(oss.str());
}

void appendPolygonLines(std::vector<std::string> &lines, const Shape::PolygonData &polygon, double dbuPerEditorUnit)
{
    if (polygon.points.size() < 2) {
        return;
    }
    for (std::size_t i = 1; i < polygon.points.size(); ++i) {
        appendQucsLine(lines, toQucsCoord(polygon.points[i - 1].x, dbuPerEditorUnit),
                       toQucsCoord(polygon.points[i - 1].y, dbuPerEditorUnit),
                       toQucsCoord(polygon.points[i].x, dbuPerEditorUnit),
                       toQucsCoord(polygon.points[i].y, dbuPerEditorUnit));
    }
    const Point &first = polygon.points.front();
    const Point &last = polygon.points.back();
    if (first.x != last.x || first.y != last.y) {
        appendQucsLine(lines, toQucsCoord(last.x, dbuPerEditorUnit), toQucsCoord(last.y, dbuPerEditorUnit),
                       toQucsCoord(first.x, dbuPerEditorUnit), toQucsCoord(first.y, dbuPerEditorUnit));
    }
}

std::vector<std::string> symbolLinesFromBlock(const CellContent &content, double dbuPerEditorUnit)
{
    std::vector<std::string> lines;
    const Block &block = content.block();
    int portSymCount = 0;

    for (const Shape &shape : block.shapes()) {
        const LayerPurpose purpose = layerPurpose(content, shapeLayerId(shape));
        if (purpose == LayerPurpose::Wire) {
            continue;
        }

        switch (shape.type()) {
        case Shape::Type::Path: {
            const Shape::PathData *path = shape.path();
            if (path == nullptr) {
                break;
            }
            if (appendQucsArcFromProperties(lines, shape.properties(), dbuPerEditorUnit)) {
                break;
            }
            if (path->points.size() < 2) {
                break;
            }
            for (std::size_t i = 1; i < path->points.size(); ++i) {
                appendQucsLine(lines, toQucsCoord(path->points[i - 1].x, dbuPerEditorUnit),
                               toQucsCoord(path->points[i - 1].y, dbuPerEditorUnit),
                               toQucsCoord(path->points[i].x, dbuPerEditorUnit),
                               toQucsCoord(path->points[i].y, dbuPerEditorUnit));
            }
            break;
        }
        case Shape::Type::Polygon: {
            const Shape::PolygonData *polygon = shape.polygon();
            if (polygon != nullptr) {
                appendPolygonLines(lines, *polygon, dbuPerEditorUnit);
            }
            break;
        }
        case Shape::Type::Rect: {
            const Shape::RectData *rect = shape.rect();
            if (rect == nullptr) {
                break;
            }
            if (purpose == LayerPurpose::Pin) {
                const std::int64_t cx = toQucsCoord((rect->box.llx + rect->box.urx) / 2, dbuPerEditorUnit);
                const std::int64_t cy = toQucsCoord((rect->box.lly + rect->box.ury) / 2, dbuPerEditorUnit);
                appendQucsPort(lines, cx, cy, shape.properties());
                ++portSymCount;
                break;
            }
            appendQucsLine(lines, toQucsCoord(rect->box.llx, dbuPerEditorUnit), toQucsCoord(rect->box.lly, dbuPerEditorUnit),
                           toQucsCoord(rect->box.urx, dbuPerEditorUnit), toQucsCoord(rect->box.lly, dbuPerEditorUnit));
            appendQucsLine(lines, toQucsCoord(rect->box.urx, dbuPerEditorUnit), toQucsCoord(rect->box.lly, dbuPerEditorUnit),
                           toQucsCoord(rect->box.urx, dbuPerEditorUnit), toQucsCoord(rect->box.ury, dbuPerEditorUnit));
            appendQucsLine(lines, toQucsCoord(rect->box.urx, dbuPerEditorUnit), toQucsCoord(rect->box.ury, dbuPerEditorUnit),
                           toQucsCoord(rect->box.llx, dbuPerEditorUnit), toQucsCoord(rect->box.ury, dbuPerEditorUnit));
            appendQucsLine(lines, toQucsCoord(rect->box.llx, dbuPerEditorUnit), toQucsCoord(rect->box.ury, dbuPerEditorUnit),
                           toQucsCoord(rect->box.llx, dbuPerEditorUnit), toQucsCoord(rect->box.lly, dbuPerEditorUnit));
            break;
        }
        case Shape::Type::Text:
            appendQucsText(lines, *shape.text(), shape.properties(), dbuPerEditorUnit);
            break;
        case Shape::Type::Arc: {
            const Shape::ArcData *arc = shape.arc();
            if (arc != nullptr) {
                appendQucsArc(lines, *arc, dbuPerEditorUnit);
            }
            break;
        }
        default:
            break;
        }
    }

    // Only fall back to net terms when the symbol has no explicit pin geometry.
    if (portSymCount == 0) {
        for (const Net &net : block.nets()) {
            for (const Term &term : net.terms()) {
                appendQucsPort(lines, toQucsCoord(term.position().x, dbuPerEditorUnit),
                               toQucsCoord(term.position().y, dbuPerEditorUnit), {Property{"lab", net.name()}});
            }
        }
    }

    return lines;
}

} // namespace

bool isQucsSchematicDecoration(const std::string &cellName)
{
    std::string type = cellName;
    const auto slash = type.find_last_of("/\\");
    if (slash != std::string::npos) {
        type = type.substr(slash + 1);
    }
    if (type.size() > 4 && type.compare(type.size() - 4, 4, ".sym") == 0) {
        type.resize(type.size() - 4);
    }
    if (type == "lab_wire" || type == "code_shown" || type == "launcher") {
        return true;
    }
    return false;
}

/*!****************************************************************************************
 * \brief Constructs a QucsExporter with default options.
 *****************************************************************************************/
QucsExporter::QucsExporter() = default;

/*!****************************************************************************************
 * \brief Constructs a QucsExporter with custom export options.
 * \param options    Qucs version string written into the output file.
 *****************************************************************************************/
QucsExporter::QucsExporter(const Options &options) : m_options(options) {}

/*!****************************************************************************************
 * \brief Exports one schematic cell to a Qucs .sch file.
 * \param db         Source database.
 * \param cellName   Name of the cell to export.
 * \param schPath    Output .sch path.
 *****************************************************************************************/
void QucsExporter::exportCell(const Database &db, const std::string &cellName, const std::string &schPath) const
{
    std::ofstream out(schPath);
    if (!out) {
        m_warnings.clear();
        m_errors.clear();
        m_errors.push_back("Cannot open file for writing: " + schPath);
        return;
    }
    exportCell(db, cellName, out);
}

void QucsExporter::exportCell(const Database &db, const std::string &cellName, std::ostream &out) const
{
    m_warnings.clear();
    m_errors.clear();

    const Cell *cell = db.lib().findCell(cellName);
    if (!cell) {
        m_errors.push_back("Cell not found: " + cellName);
        return;
    }

    const CellContent *content = cell->findContent(ViewType::Schematic);
    if (!content) {
        m_errors.push_back("No schematic view for cell: " + cellName);
        return;
    }

    const double dbuPerEditorUnit = effectiveDbuPerEditorUnit(*content);

    PrimitiveResolver resolver;
    resolver.setTechLibrary(m_options.techLibrary);
    if (!m_options.qucsPrimitiveLib.empty()) {
        resolver.setQucsLibrary(m_options.qucsPrimitiveLib);
    }
    for (const std::string &path : m_options.primitiveCorePaths) {
        resolver.addCorePath(path);
    }
    if (m_options.primitiveCorePaths.empty()) {
        resolver.loadFromEnvironment();
    }
    std::string effectiveQucsLib = m_options.qucsPrimitiveLib;
    if (effectiveQucsLib.empty()) {
        effectiveQucsLib = "IHP_PDK_nonlinear_components";
    }

    const auto headers = collectProperties(content->properties(), "schematic.header");
    if (!headers.empty()) {
        out << headers.front() << '\n';
    } else if (!content->sourceInfo().toolVersion().empty()) {
        out << "<Qucs Schematic " << content->sourceInfo().toolVersion() << ">\n";
    } else {
        out << "<Qucs Schematic " << m_options.qucsVersion << ">\n";
    }

    auto propertyLines = collectProperties(content->properties(), "schematic.view");
    if (propertyLines.empty()) {
        propertyLines = {"<View=0,0,800,600,1,0,0>", "<Grid=10,10,1>"};
    }
    for (std::string &line : propertyLines) {
        line = normalizeQucsViewPropertyLine(line, cellName);
    }
    writeSection(out, "Properties", propertyLines);

    for (const char *sectionName : {"Symbol", "Components", "Wires", "Diagrams", "Paintings"}) {
        if (std::string(sectionName) == "Components") {
            std::vector<std::string> lines;
            for (const Instance &inst : content->block().instances()) {
                lines.push_back(formatComponentLine(inst, dbuPerEditorUnit, &resolver, effectiveQucsLib));
            }
            writeSection(out, "Components", lines);
            continue;
        }
        if (std::string(sectionName) == "Wires") {
            // LibComp prefers CORE symbol artwork (commonLib / PDK). Wire endpoints must sit on
            // CORE/Xschem pin centers — NOT on legacy Qucs .lib pin offsets.
            // - xschem-origin: already on CORE pins → no retarget
            // - qucs-origin: wires on .lib pins → map to CORE when opening in Qucs with CORE symbols
            std::vector<std::pair<Point, Point>> pinRetargets;
            if (content->sourceInfo().format() == "qucs") {
                pinRetargets.reserve(content->block().instances().size() * 4);
                for (const Instance &inst : content->block().instances()) {
                    appendQucsToXschemPinRetargets(inst, dbuPerEditorUnit, pinRetargets);
                }
            }
            writeSection(out, "Wires",
                         formatWiresFromBlock(content->block(), content->layers(), dbuPerEditorUnit, pinRetargets));
            continue;
        }
        writeSection(out, sectionName, collectProperties(content->properties(), std::string("section.") + sectionName));
    }
}

void QucsExporter::exportSymbolCell(const Database &db, const std::string &cellName, const std::string &schPath) const
{
    std::ofstream out(schPath);
    if (!out) {
        m_warnings.clear();
        m_errors.clear();
        m_errors.push_back("Cannot open file for writing: " + schPath);
        return;
    }
    exportSymbolCell(db, cellName, out);
}

void QucsExporter::exportSymbolCell(const Database &db, const std::string &cellName, std::ostream &out) const
{
    m_warnings.clear();
    m_errors.clear();

    const std::vector<std::string> symbolLines = symbolLinesForCell(db, cellName);
    if (!m_errors.empty()) {
        return;
    }
    if (symbolLines.empty()) {
        m_errors.push_back("Symbol view has no geometry to export");
        return;
    }

    const Cell *cell = db.lib().findCell(cellName);
    if (!cell) {
        m_errors.push_back("Cell not found: " + cellName);
        return;
    }

    const CellContent *content = cell->findContent(ViewType::Symbol);
    if (!content) {
        m_errors.push_back("No symbol view for cell: " + cellName);
        return;
    }

    const auto headers = collectProperties(content->properties(), "schematic.header");
    if (!headers.empty()) {
        out << headers.front() << '\n';
    } else if (!content->sourceInfo().toolVersion().empty()) {
        out << "<Qucs Schematic " << content->sourceInfo().toolVersion() << ">\n";
    } else {
        out << "<Qucs Schematic " << m_options.qucsVersion << ">\n";
    }

    writeSection(out, "Symbol", symbolLines);
}

std::vector<std::string> QucsExporter::symbolLinesForCell(const Database &db, const std::string &cellName) const
{
    m_warnings.clear();
    m_errors.clear();

    const Cell *cell = db.lib().findCell(cellName);
    if (!cell) {
        m_errors.push_back("Cell not found: " + cellName);
        return {};
    }

    const CellContent *content = cell->findContent(ViewType::Symbol);
    if (!content) {
        m_errors.push_back("No symbol view for cell: " + cellName);
        return {};
    }

    const double dbuPerEditorUnit = effectiveDbuPerEditorUnit(*content);
    auto symbolLines = collectProperties(content->properties(), "section.Symbol");
    if (symbolLines.empty()) {
        symbolLines = symbolLinesFromBlock(*content, dbuPerEditorUnit);
    }
    if (symbolLines.empty()) {
        m_errors.push_back("Symbol view has no geometry to export");
    }
    return symbolLines;
}

std::string QucsExporter::exportCellToString(const Database &db, const std::string &cellName) const
{
    std::ostringstream out;
    exportCell(db, cellName, out);
    return out.str();
}

std::string QucsExporter::exportSymbolCellToString(const Database &db, const std::string &cellName) const
{
    std::ostringstream out;
    exportSymbolCell(db, cellName, out);
    return out.str();
}

std::string QucsExporter::exportInstanceAsLine(const Instance &inst, double dbuPerEditorUnit,
                                               const std::string &sourceFormat) const
{
    PrimitiveResolver resolver;
    resolver.setTechLibrary(m_options.techLibrary);
    if (!m_options.qucsPrimitiveLib.empty()) {
        resolver.setQucsLibrary(m_options.qucsPrimitiveLib);
    }
    for (const std::string &path : m_options.primitiveCorePaths) {
        resolver.addCorePath(path);
    }
    if (m_options.primitiveCorePaths.empty()) {
        resolver.loadFromEnvironment();
    }
    std::string effectiveQucsLib = m_options.qucsPrimitiveLib;
    if (effectiveQucsLib.empty()) {
        effectiveQucsLib = "IHP_PDK_nonlinear_components";
    }
    (void)sourceFormat;
    return formatComponentLine(inst, dbuPerEditorUnit, &resolver, effectiveQucsLib);
}

std::vector<std::string> QucsExporter::exportBlockWiresAsLines(const Block &block,
                                                               const std::vector<LayerSpec> &layers,
                                                               double dbuPerEditorUnit,
                                                               const std::string &sourceFormat) const
{
    std::vector<std::pair<Point, Point>> pinRetargets;
    if (sourceFormat == "qucs") {
        pinRetargets.reserve(block.instances().size() * 4);
        for (const Instance &inst : block.instances()) {
            appendQucsToXschemPinRetargets(inst, dbuPerEditorUnit, pinRetargets);
        }
    }
    return formatWiresFromBlock(block, layers, dbuPerEditorUnit, pinRetargets);
}

} // namespace core
