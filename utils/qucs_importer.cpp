/*!****************************************************************************************
 * \file qucs_importer.cpp
 * \brief Qucs .sch schematic parser into CORE schematic views.
 *****************************************************************************************/

#include "qucs_importer.h"

#include "cell.h"
#include "coord_scale.h"
#include "layer_spec.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace core {
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

std::string unquote(const std::string &token)
{
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        return token.substr(1, token.size() - 2);
    }
    return token;
}

std::int64_t parseInt64(const std::string &token)
{
    return std::stoll(token);
}

Orient qucsRotateToOrient(int rotate, int mirror)
{
    Orient base = Orient::R0;
    switch (rotate & 3) {
    case 1: base = Orient::R90; break;
    case 2: base = Orient::R180; break;
    case 3: base = Orient::R270; break;
    default: base = Orient::R0; break;
    }
    if (mirror != 0) {
        switch (base) {
        case Orient::R0: return Orient::MX;
        case Orient::R90: return Orient::MX90;
        case Orient::R180: return Orient::MY;
        case Orient::R270: return Orient::MY90;
        default: return base;
        }
    }
    return base;
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
    const int rotate = (tokens.size() > 8) ? std::stoi(tokens[8]) : 0;

    Transform xf;
    xf.x = x;
    xf.y = y;
    xf.orient = qucsRotateToOrient(rotate, mirror);

    Instance inst(type, xf);
    addProperty(inst.properties(), "name", instName);
    addProperty(inst.properties(), "active", std::to_string(active));
    addProperty(inst.properties(), "textX", std::to_string(textX));
    addProperty(inst.properties(), "textY", std::to_string(textY));
    addProperty(inst.properties(), "mirror", std::to_string(mirror));
    addProperty(inst.properties(), "rotate", std::to_string(rotate));

    for (std::size_t i = 9; i + 1 < tokens.size(); i += 2) {
        addProperty(inst.properties(), "param." + std::to_string((i - 9) / 2), unquote(tokens[i]));
        addProperty(inst.properties(), "visible." + std::to_string((i - 9) / 2), tokens[i + 1]);
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

    const auto sections = parseSections(text);
    const std::string cellName = m_options.cellName.empty() ? stemFromPath(schPath) : m_options.cellName;

    Database db;
    db.setGenerator("CORE QucsImporter");
    db.lib() = Lib(m_options.libName);

    Cell &cell = db.lib().getOrCreateCell(cellName);
    CellContent &content = cell.getOrCreateContent(ViewType::Schematic, kQucsDbuPerEditorUnit);
    content.setDbuPerMicron(kQucsDbuPerEditorUnit);
    content.setDbuPerEditorUnit(kQucsDbuPerEditorUnit);
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
    addSchematicLayer("instance", LayerPurpose::Pin);
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

    if (sections.count("Components")) {
        for (const std::string &line : sections.at("Components")) {
            block.instances().push_back(parseComponentLine(line, m_warnings));
        }
    } else {
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
    } else {
        m_warnings.push_back("No <Wires> section found");
    }

    db.lib().recomputeAllBBoxes(ViewType::Schematic);
    return db;
}

} // namespace core
