/*!****************************************************************************************
 * \file qucs_importer.cpp
 * \brief Qucs .sch schematic parser into ROOM schematic views.
 *****************************************************************************************/

#include "qucs_importer.h"

#include "cell.h"
#include "coord_scale.h"
#include "layer_spec.h"
#include "net_name_propagation.h"
#include "primitive_resolver.h"
#include "pulse_params.h"
#include "xschem_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace room {
namespace {

std::string stemFromPath(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    const std::size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    const std::size_t end = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}

std::string trim(const std::string &value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::vector<std::string> splitQucsTokens(const std::string &line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuote = false;

    for (char ch : line) {
        if (ch == '"') {
            inQuote = !inQuote;
            current.push_back(ch);
            if (!inQuote) {
                tokens.push_back(current);
                current.clear();
            }
        } else if (!inQuote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

std::string unescapeCStyle(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            switch (text[i + 1]) {
            case 'n':
                out.push_back('\n');
                ++i;
                break;
            case 't':
                out.push_back('\t');
                ++i;
                break;
            case 'r':
                out.push_back('\r');
                ++i;
                break;
            case '\\':
                out.push_back('\\');
                ++i;
                break;
            case '"':
                out.push_back('"');
                ++i;
                break;
            default:
                out.push_back(text[i]);
                break;
            }
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string unquote(const std::string &token)
{
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        // Qucs writes escapes like \n inside quoted params (e.g. INCLSCR ".LIB … mos_tt\n").
        return unescapeCStyle(token.substr(1, token.size() - 2));
    }
    return token;
}

std::int64_t parseInt64(const std::string &token)
{
    return std::stoll(token);
}

Orient qucsRotateToOrient(int rotate, int mirror)
{
    return orientFromQucsPlacement(mirror, rotate);
}

struct WireRec {
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    std::int64_t x2 = 0;
    std::int64_t y2 = 0;
    std::string label;
    std::int64_t labelX = 0;
    std::int64_t labelY = 0;
    std::int64_t dist = 0;
    std::string nodeSet;
};

struct PointKey {
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const PointKey &other) const { return x == other.x && y == other.y; }
};

struct PointKeyHash {
    std::size_t operator()(const PointKey &key) const
    {
        return std::hash<std::int64_t>{}(key.x) ^ (std::hash<std::int64_t>{}(key.y) << 1);
    }
};

class UnionFind {
public:
    PointKey find(PointKey key)
    {
        auto it = parent_.find(key);
        if (it == parent_.end()) {
            parent_[key] = key;
            return key;
        }
        if (it->second.x != key.x || it->second.y != key.y) {
            it->second = find(it->second);
        }
        return it->second;
    }

    void unite(PointKey a, PointKey b)
    {
        a = find(a);
        b = find(b);
        if (a.x == b.x && a.y == b.y) {
            return;
        }
        parent_[b] = a;
    }

private:
    std::unordered_map<PointKey, PointKey, PointKeyHash> parent_;
};

bool isTopLevelSectionName(const std::string &name)
{
    return name == "Properties" || name == "Symbol" || name == "Components" || name == "Wires" || name == "Diagrams"
        || name == "Paintings";
}

std::unordered_map<std::string, std::vector<std::string>> parseSections(const std::string &text)
{
    std::unordered_map<std::string, std::vector<std::string>> sections;
    std::istringstream stream(text);
    std::string line;
    std::string current;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() != '<' || line.back() != '>') {
            continue;
        }
        const std::string inner = line.substr(1, line.size() - 2);
        if (inner.size() >= 2 && inner[0] == '/') {
            const std::string tagName = inner.substr(1);
            if (!current.empty()) {
                if (tagName == current) {
                    current.clear();
                } else if (!isTopLevelSectionName(tagName)) {
                    sections[current].push_back(line);
                } else {
                    current.clear();
                }
            }
            continue;
        }
        if (inner.rfind("Qucs Schematic", 0) == 0) {
            sections["__header__"].push_back(line);
            continue;
        }
        if (inner.find('=') != std::string::npos || inner.rfind('.', 0) == 0) {
            if (!current.empty()) {
                sections[current].push_back(line);
            } else {
                sections["__preamble__"].push_back(line);
            }
            continue;
        }
        if (inner.find(' ') == std::string::npos) {
            current = inner;
            continue;
        }
        if (!current.empty()) {
            sections[current].push_back(line);
        }
    }
    return sections;
}

std::string readFile(const std::string &path, std::vector<std::string> &errors)
{
    std::ifstream in(path);
    if (!in) {
        errors.push_back("Cannot open Qucs schematic: " + path);
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void addProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    props.push_back({name, value});
}

Instance parseComponentLine(const std::string &line, std::vector<std::string> &warnings)
{
    const std::string inner = trim(line);
    if (inner.size() < 2 || inner.front() != '<' || inner.back() != '>') {
        warnings.push_back("Skipping malformed component line");
        return Instance{"?", Transform{}};
    }
    const auto tokens = splitQucsTokens(inner.substr(1, inner.size() - 2));
    if (tokens.size() < 8) {
        warnings.push_back("Component line has too few fields");
        return Instance{"?", Transform{}};
    }

    const std::string type = tokens[0];
    const std::string instName = tokens[1];
    const int active = std::stoi(tokens[2]);
    const std::int64_t x = parseInt64(tokens[3]);
    const std::int64_t y = parseInt64(tokens[4]);
    const std::int64_t textX = parseInt64(tokens[5]);
    const std::int64_t textY = parseInt64(tokens[6]);
    const int mirror = std::stoi(tokens[7]);
    const int rotateField = (tokens.size() > 8) ? std::stoi(tokens[8]) : 0;
    const Orient orient = hasQucsHistoricalSourceRotate(type)
        ? orientFromQucsSourcePlacement(mirror, rotateField)
        : qucsRotateToOrient(rotateField, mirror);

    Transform xf;
    xf.x = x;
    xf.y = y;
    xf.orient = orient;

    Instance inst(type, xf);
    addProperty(inst.properties(), "name", instName);
    addProperty(inst.properties(), "active", std::to_string(active));
    addProperty(inst.properties(), "textX", std::to_string(textX));
    addProperty(inst.properties(), "textY", std::to_string(textY));
    addProperty(inst.properties(), "mirror", std::to_string(mirror));
    addProperty(inst.properties(), "rotate", std::to_string(rotateField));

    for (std::size_t i = 9; i + 1 < tokens.size(); i += 2) {
        addProperty(inst.properties(), "param." + std::to_string((i - 9) / 2), unquote(tokens[i]));
        addProperty(inst.properties(), "visible." + std::to_string((i - 9) / 2), tokens[i + 1]);
    }

    if (type == "Vpulse" || type == "Ipulse") {
        if (std::optional<std::string> pulse = buildPulseSpiceValue(inst.properties())) {
            addProperty(inst.properties(), "value", std::move(*pulse));
        }
    }

    return inst;
}

WireRec parseWireLine(const std::string &line, std::vector<std::string> &warnings)
{
    WireRec wire;
    const std::string inner = trim(line);
    if (inner.size() < 2 || inner.front() != '<' || inner.back() != '>') {
        warnings.push_back("Skipping malformed wire line");
        return wire;
    }
    const auto tokens = splitQucsTokens(inner.substr(1, inner.size() - 2));
    if (tokens.size() < 9) {
        warnings.push_back("Wire line has too few fields");
        return wire;
    }
    wire.x1 = parseInt64(tokens[0]);
    wire.y1 = parseInt64(tokens[1]);
    wire.x2 = parseInt64(tokens[2]);
    wire.y2 = parseInt64(tokens[3]);
    wire.label = unquote(tokens[4]);
    wire.labelX = parseInt64(tokens[5]);
    wire.labelY = parseInt64(tokens[6]);
    wire.dist = parseInt64(tokens[7]);
    wire.nodeSet = unquote(tokens[8]);
    return wire;
}

void buildNetsFromWires(const std::vector<WireRec> &wires, Block &block)
{
    if (wires.empty()) {
        return;
    }

    UnionFind uf;
    std::unordered_map<PointKey, std::string, PointKeyHash> rootLabels;

    for (const WireRec &wire : wires) {
        const PointKey a{wire.x1, wire.y1};
        const PointKey b{wire.x2, wire.y2};
        uf.unite(a, b);
        if (!wire.label.empty()) {
            rootLabels[uf.find(a)] = wire.label;
        }
    }

    std::unordered_map<PointKey, std::vector<PointKey>, PointKeyHash> groups;
    for (const WireRec &wire : wires) {
        const PointKey root = uf.find({wire.x1, wire.y1});
        groups[root].push_back({wire.x1, wire.y1});
        groups[root].push_back({wire.x2, wire.y2});
    }

    int anonymous = 0;
    for (auto &entry : groups) {
        const PointKey root = entry.first;
        std::string netName = rootLabels.count(root) ? rootLabels[root] : ("N$" + std::to_string(++anonymous));

        Net net(netName);
        std::unordered_map<PointKey, bool, PointKeyHash> seen;
        for (const PointKey &pt : entry.second) {
            if (seen[pt]) {
                continue;
            }
            seen[pt] = true;
            net.terms().emplace_back(netName, 0, Point{pt.x, pt.y});
        }
        block.nets().push_back(std::move(net));
    }
}

std::string symbolTagName(const std::string &inner)
{
    std::size_t end = 0;
    while (end < inner.size() && !std::isspace(static_cast<unsigned char>(inner[end]))) {
        ++end;
    }
    return inner.substr(0, end);
}

std::string angleToDirection(int angle)
{
    const int normalized = ((angle % 360) + 360) % 360;
    if (normalized == 90) {
        return "up";
    }
    if (normalized == 180) {
        return "left";
    }
    if (normalized == 270) {
        return "down";
    }
    return "right";
}

Point rotatePointCw(Point p)
{
    // Same as Qucs Component::rotate: (x,y) -> (y, -x)
    return Point{p.y, -p.x};
}

Box rotateBoxCw(Box box)
{
    const Point a = rotatePointCw(Point{box.llx, box.lly});
    const Point b = rotatePointCw(Point{box.urx, box.ury});
    return Box{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

void rotateBlockShapesCw(Block &block)
{
    std::vector<Shape> rotatedShapes;
    rotatedShapes.reserve(block.shapes().size());
    for (const Shape &shape : block.shapes()) {
        switch (shape.type()) {
        case Shape::Type::Path: {
            Shape::PathData path = *shape.path();
            for (Point &pt : path.points) {
                pt = rotatePointCw(pt);
            }
            Shape out(path);
            out.properties() = shape.properties();
            rotatedShapes.push_back(std::move(out));
            break;
        }
        case Shape::Type::Rect: {
            Shape::RectData rect = *shape.rect();
            rect.box = rotateBoxCw(rect.box);
            Shape out(rect);
            out.properties() = shape.properties();
            for (Property &prop : out.properties()) {
                if (prop.name != "direction") {
                    continue;
                }
                // Direction vector (dx,dy) under Qucs CW (x,y)->(y,-x): right(+x)->up(-y).
                if (prop.value == "right") {
                    prop.value = "up";
                } else if (prop.value == "up") {
                    prop.value = "left";
                } else if (prop.value == "left") {
                    prop.value = "down";
                } else if (prop.value == "down") {
                    prop.value = "right";
                }
            }
            rotatedShapes.push_back(std::move(out));
            break;
        }
        case Shape::Type::Text: {
            Shape::TextData text = *shape.text();
            text.position = rotatePointCw(text.position);
            Shape out(text);
            out.properties() = shape.properties();
            rotatedShapes.push_back(std::move(out));
            break;
        }
        case Shape::Type::Arc: {
            Shape::ArcData arc = *shape.arc();
            arc.center = rotatePointCw(arc.center);
            arc.startAngle += 3.14159265358979323846 / 2.0;
            arc.endAngle += 3.14159265358979323846 / 2.0;
            Shape out(arc);
            out.properties() = shape.properties();
            rotatedShapes.push_back(std::move(out));
            break;
        }
        case Shape::Type::Polygon: {
            Shape::PolygonData poly = *shape.polygon();
            for (Point &pt : poly.points) {
                pt = rotatePointCw(pt);
            }
            Shape out(poly);
            out.properties() = shape.properties();
            rotatedShapes.push_back(std::move(out));
            break;
        }
        }
    }
    block.shapes() = std::move(rotatedShapes);

    std::vector<Net> rotatedNets;
    rotatedNets.reserve(block.nets().size());
    for (const Net &net : block.nets()) {
        Net out(net.name());
        for (const Term &term : net.terms()) {
            out.terms().emplace_back(term.name(), term.layerId(), rotatePointCw(term.position()));
        }
        rotatedNets.push_back(std::move(out));
    }
    block.nets() = std::move(rotatedNets);
}

/*! Parse Qucs Symbol section lines into drawable Path/Pin/Text shapes (and keep raw props separately). */
void appendShapesFromSymbolLines(const std::vector<std::string> &lines, Block &block, std::uint32_t drawingLayer,
                                 std::uint32_t pinLayer, std::uint32_t labelLayer, double dbuPerEditorUnit,
                                 std::vector<std::string> &warnings)
{
    auto toDbu = [dbuPerEditorUnit](std::int64_t editor) {
        return editorUnitsToDbu(static_cast<double>(editor), dbuPerEditorUnit);
    };
    int anonymousPin = 0;
    for (const std::string &raw : lines) {
        const std::string line = trim(raw);
        if (line.size() < 2 || line.front() != '<' || line.back() != '>') {
            continue;
        }
        const std::string inner = line.substr(1, line.size() - 2);
        const std::string tag = symbolTagName(inner);
        const auto tokens = splitQucsTokens(inner);

        if (tag == "Line") {
            if (tokens.size() < 5) {
                warnings.push_back("Skipping malformed Symbol Line");
                continue;
            }
            const std::int64_t x1 = parseInt64(tokens[1]);
            const std::int64_t y1 = parseInt64(tokens[2]);
            const std::int64_t dx = parseInt64(tokens[3]);
            const std::int64_t dy = parseInt64(tokens[4]);
            Shape::PathData path;
            path.layerId = drawingLayer;
            // Xschem L-record first field is the xschem layer number (stored in path.width).
            path.width = 4;
            path.points = {Point{toDbu(x1), toDbu(y1)}, Point{toDbu(x1 + dx), toDbu(y1 + dy)}};
            Shape shape(path);
            if (tokens.size() > 6) {
                addProperty(shape.properties(), "penWidth", tokens[6]);
            }
            block.shapes().push_back(std::move(shape));
            continue;
        }

        if (tag == ".PortSym" || tag == "PortSym") {
            if (tokens.size() < 4) {
                warnings.push_back("Skipping malformed Symbol PortSym");
                continue;
            }
            const std::int64_t cx = parseInt64(tokens[1]);
            const std::int64_t cy = parseInt64(tokens[2]);
            const std::string pinNumber = tokens[3];
            const int angle = (tokens.size() > 4) ? static_cast<int>(parseInt64(tokens[4])) : 0;
            std::string pinName = (tokens.size() > 5) ? unquote(tokens[5]) : ("p" + pinNumber);
            if (pinName.empty()) {
                pinName = "p" + std::to_string(++anonymousPin);
            }
            constexpr std::int64_t kHalfEditor = 2;
            const std::int64_t cxDbu = toDbu(cx);
            const std::int64_t cyDbu = toDbu(cy);
            const std::int64_t half = toDbu(kHalfEditor);
            Shape::RectData rect;
            rect.layerId = pinLayer;
            rect.box = Box{cxDbu - half, cyDbu - half, cxDbu + half, cyDbu + half};
            Shape shape(rect);
            addProperty(shape.properties(), "lab", pinName);
            addProperty(shape.properties(), "name", pinName);
            addProperty(shape.properties(), "pinnumber", pinNumber);
            addProperty(shape.properties(), "direction", angleToDirection(angle));
            addProperty(shape.properties(), "pinWidth", "5");
            block.shapes().push_back(std::move(shape));

            Net net(pinName);
            net.terms().emplace_back(pinName, 0, Point{cxDbu, cyDbu});
            block.nets().push_back(std::move(net));
            continue;
        }

        if (tag == "Text") {
            if (tokens.size() < 7) {
                warnings.push_back("Skipping malformed Symbol Text");
                continue;
            }
            auto editorTextSize = [](const std::string &token) -> std::int64_t {
                try {
                    const double value = std::stod(token);
                    if (token.find('.') != std::string::npos) {
                        return static_cast<std::int64_t>(std::llround(value * 16.0));
                    }
                    return static_cast<std::int64_t>(value);
                } catch (...) {
                    return 8;
                }
            };
            Shape::TextData text;
            text.layerId = labelLayer;
            text.position = Point{toDbu(parseInt64(tokens[1])), toDbu(parseInt64(tokens[2]))};
            const std::int64_t sizeX = editorTextSize(tokens[3]);
            const std::int64_t sizeY =
                tokens.size() >= 8 ? editorTextSize(tokens[4]) : editorTextSize(tokens[3]);
            text.height = static_cast<std::uint32_t>(toDbu(std::max<std::int64_t>(8, sizeX)));
            text.text = unquote(tokens.size() >= 8 ? tokens[7] : tokens[6]);
            Shape shape(text);
            addProperty(shape.properties(), "sizeX", tokens[3]);
            addProperty(shape.properties(), "sizeY", tokens.size() >= 8 ? tokens[4] : tokens[3]);
            addProperty(shape.properties(), "qucs.color", tokens.size() >= 8 ? tokens[5] : tokens[4]);
            addProperty(shape.properties(), "qucs.show", tokens.size() >= 8 ? tokens[6] : tokens[5]);
            block.shapes().push_back(std::move(shape));
            continue;
        }

        if (tag == "EArc") {
            if (tokens.size() < 7) {
                warnings.push_back("Skipping malformed Symbol EArc");
                continue;
            }
            const std::int64_t x = parseInt64(tokens[1]);
            const std::int64_t y = parseInt64(tokens[2]);
            const std::int64_t w = parseInt64(tokens[3]);
            const std::int64_t h = parseInt64(tokens[4]);
            const double startQt = static_cast<double>(parseInt64(tokens[5])) / 16.0;
            const double spanQt = static_cast<double>(parseInt64(tokens[6])) / 16.0;
            Shape::ArcData arc;
            arc.layerId = drawingLayer;
            arc.center = Point{toDbu(x + w / 2), toDbu(y + h / 2)};
            arc.radius = static_cast<double>(toDbu(std::max(w, h))) / 2.0 / dbuPerEditorUnit;
            // Match xschem A-record semantics: start angle and span in degrees.
            arc.startAngle = startQt;
            arc.endAngle = spanQt;
            arc.width = 4;
            Shape shape(arc);
            addProperty(shape.properties(), "qucs.earc.x", tokens[1]);
            addProperty(shape.properties(), "qucs.earc.y", tokens[2]);
            addProperty(shape.properties(), "qucs.earc.w", tokens[3]);
            addProperty(shape.properties(), "qucs.earc.h", tokens[4]);
            addProperty(shape.properties(), "qucs.earc.start", tokens[5]);
            addProperty(shape.properties(), "qucs.earc.span", tokens[6]);
            block.shapes().push_back(std::move(shape));
            continue;
        }
    }
}

} // namespace

/*!****************************************************************************************
 * \brief Constructs a QucsImporter with default options.
 *****************************************************************************************/
QucsImporter::QucsImporter() = default;

/*!****************************************************************************************
 * \brief Constructs a QucsImporter with custom import options.
 * \param options    Library name and optional cell name override.
 *****************************************************************************************/
QucsImporter::QucsImporter(const Options &options) : m_options(options) {}

/*!****************************************************************************************
 * \brief Imports a Qucs schematic file into a new Database.
 * \param schPath    Path to the input .sch file.
 * \return           Database with schematic CellContent, or empty on error.
 *****************************************************************************************/
Database QucsImporter::importFile(const std::string &schPath) const
{
    m_warnings.clear();
    m_errors.clear();

    const std::string text = readFile(schPath, m_errors);
    if (!m_errors.empty()) {
        return Database{};
    }

    const std::string cellName = m_options.cellName.empty() ? stemFromPath(schPath) : m_options.cellName;
    return importText(text, cellName);
}

Database QucsImporter::importText(const std::string &text, const std::string &cellName) const
{
    m_warnings.clear();
    m_errors.clear();

    const auto sections = parseSections(text);
    const std::string resolvedCellName = cellName.empty()
        ? (m_options.cellName.empty() ? std::string("cell") : m_options.cellName)
        : cellName;

    Database db;
    db.setGenerator("ROOM QucsImporter");
    db.lib() = Lib(m_options.libName);

    Cell &cell = db.lib().getOrCreateCell(resolvedCellName);
    const bool symbolOnly = sections.count("Components") == 0 && sections.count("Symbol") > 0;
    // Dual-tool symbols: store geometry in Xschem DBU scale (1000) so ROOM→.sym materialization
    // matches devices/*.sym. Qucs still prefers opaque section.Symbol properties when present.
    const double dbuPerEditor =
        symbolOnly ? kXschemDbuPerEditorUnit : kQucsDbuPerEditorUnit;
    CellContent &content =
        cell.getOrCreateContent(symbolOnly ? ViewType::Symbol : ViewType::Schematic, dbuPerEditor);
    content.setDbuPerMicron(dbuPerEditor);
    content.setDbuPerEditorUnit(dbuPerEditor);
    Block &block = content.block();

    auto addSchematicLayer = [&](const std::string &name, LayerPurpose purpose) {
        LayerSpec spec;
        spec.layerNum = static_cast<std::uint16_t>(content.layers().size());
        spec.dataType = 0;
        spec.name = name;
        spec.purpose = purpose;
        content.layers().push_back(spec);
        db.lib().layers().push_back(spec);
    };
    addSchematicLayer("wire", LayerPurpose::Wire);
    addSchematicLayer("drawing", LayerPurpose::Drawing);
    addSchematicLayer("pin", LayerPurpose::Pin);
    addSchematicLayer("label", LayerPurpose::Label);

    if (sections.count("__header__")) {
        const std::string &header = sections.at("__header__").front();
        content.sourceInfo().setFormat("qucs");
        const std::size_t open = header.find('<');
        const std::size_t close = header.find('>');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            const std::string body = header.substr(open + 1, close - open - 1);
            const std::size_t space = body.find(' ');
            if (space != std::string::npos) {
                content.sourceInfo().setToolVersion(body.substr(space + 1));
            }
            addProperty(content.properties(), "schematic.header", header);
        }
    }

    if (sections.count("Properties")) {
        for (const std::string &line : sections.at("Properties")) {
            addProperty(content.properties(), "schematic.view", line);
        }
    }

    for (const char *sectionName : {"Symbol", "Diagrams", "Paintings"}) {
        if (sections.count(sectionName)) {
            for (const std::string &line : sections.at(sectionName)) {
                addProperty(content.properties(), std::string("section.") + sectionName, line);
            }
        }
    }

    if (sections.count("Symbol")) {
        // drawing=1, pin=2, label=3 (wire=0 reserved for schematic wires)
        appendShapesFromSymbolLines(sections.at("Symbol"), block, 1, 2, 3, content.dbuPerEditorUnit(),
                                    m_warnings);
        // Keep section.Symbol unrotated for Qucs LibComp+rotate field; rotate shapes for Xschem.
        if (hasQucsHistoricalSourceRotate(resolvedCellName)) {
            rotateBlockShapesCw(block);
            addProperty(content.properties(), "qucs.historicalRotate", "1");
        }
    }

    if (sections.count("Components")) {
        for (const std::string &line : sections.at("Components")) {
            block.instances().push_back(parseComponentLine(line, m_warnings));
        }
    } else if (!symbolOnly) {
        m_warnings.push_back("No <Components> section found");
    }

    std::vector<WireRec> wires;
    if (sections.count("Wires")) {
        const std::uint32_t wireLayer = 0;
        for (const std::string &line : sections.at("Wires")) {
            wires.push_back(parseWireLine(line, m_warnings));
        }
        buildNetsFromWires(wires, block);
        for (const WireRec &wire : wires) {
            Shape::PathData path;
            path.layerId = wireLayer;
            path.width = 1;
            path.points = {Point{wire.x1, wire.y1}, Point{wire.x2, wire.y2}};
            Shape shape(path);
            addProperty(shape.properties(), "label", wire.label);
            addProperty(shape.properties(), "labelX", std::to_string(wire.labelX));
            addProperty(shape.properties(), "labelY", std::to_string(wire.labelY));
            addProperty(shape.properties(), "dist", std::to_string(wire.dist));
            addProperty(shape.properties(), "nodeSet", wire.nodeSet);
            block.shapes().push_back(std::move(shape));
        }
    } else if (!symbolOnly) {
        m_warnings.push_back("No <Wires> section found");
    }

    if (!symbolOnly) {
        xschem::syncDualToolGraphProperties(block, content, xschem::GraphSyncDirection::FromQucsDiagram);
        PrimitiveResolver resolver;
        resolver.loadFromEnvironment();
        canonicalizeBlockPrimitives(block, &resolver);
        propagateNetNames(block, &resolver, content.dbuPerEditorUnit());
    }

    db.lib().recomputeAllBBoxes(symbolOnly ? ViewType::Symbol : ViewType::Schematic);
    return db;
}

Instance QucsImporter::parseComponentLinePublic(const std::string &line) const
{
    return parseComponentLine(line, m_warnings);
}

QucsImporter::WireRecord QucsImporter::parseWireLinePublic(const std::string &line) const
{
    const WireRec wire = parseWireLine(line, m_warnings);
    WireRecord record;
    record.x1 = wire.x1;
    record.y1 = wire.y1;
    record.x2 = wire.x2;
    record.y2 = wire.y2;
    record.label = wire.label;
    record.labelX = wire.labelX;
    record.labelY = wire.labelY;
    record.dist = wire.dist;
    record.nodeSet = wire.nodeSet;
    return record;
}

void QucsImporter::importWireLines(Block &block, const std::vector<std::string> &lines, double dbuPerEditorUnit) const
{
    std::vector<WireRec> wires;
    wires.reserve(lines.size());
    for (const std::string &line : lines) {
        wires.push_back(parseWireLine(line, m_warnings));
    }

    buildNetsFromWires(wires, block);
    const std::uint32_t wireLayer = 0;
    for (const WireRec &wire : wires) {
        Shape::PathData path;
        path.layerId = wireLayer;
        path.width = 1;
        path.points = {Point{wire.x1, wire.y1}, Point{wire.x2, wire.y2}};
        Shape shape(path);
        addProperty(shape.properties(), "label", wire.label);
        addProperty(shape.properties(), "labelX", std::to_string(wire.labelX));
        addProperty(shape.properties(), "labelY", std::to_string(wire.labelY));
        addProperty(shape.properties(), "dist", std::to_string(wire.dist));
        addProperty(shape.properties(), "nodeSet", wire.nodeSet);
        block.shapes().push_back(std::move(shape));
    }
}

} // namespace room
