/*!****************************************************************************************
 * \file xschem_io.cpp
 * \brief Shared Xschem record parser and CORE Block mapping (format-neutral model).
 *****************************************************************************************/

#include "xschem_io.h"

#include "net.h"
#include "shape.h"
#include "xschem_format.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace core::xschem {
namespace {

constexpr double kCoordScale = 1000.0;

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

int countBraceDelta(const std::string &line)
{
    int delta = 0;
    for (char ch : line) {
        if (ch == '{') {
            ++delta;
        } else if (ch == '}') {
            --delta;
        }
    }
    return delta;
}

bool extractBracedToken(const std::string &text, std::size_t &pos, std::string &out)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos >= text.size() || text[pos] != '{') {
        return false;
    }
    const std::size_t start = pos;
    int depth = 0;
    for (; pos < text.size(); ++pos) {
        if (text[pos] == '{') {
            ++depth;
        } else if (text[pos] == '}') {
            --depth;
            if (depth == 0) {
                out = text.substr(start, pos - start + 1);
                ++pos;
                return true;
            }
        }
    }
    return false;
}

bool readDoubleToken(const std::string &text, std::size_t &pos, double &value)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    const std::size_t start = pos;
    while (pos < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '-' || text[pos] == '+' ||
            text[pos] == '.')) {
        ++pos;
    }
    if (start == pos) {
        return false;
    }
    value = std::stod(text.substr(start, pos - start));
    return true;
}

bool readIntToken(const std::string &text, std::size_t &pos, int &value)
{
    double tmp = 0.0;
    if (!readDoubleToken(text, pos, tmp)) {
        return false;
    }
    value = static_cast<int>(tmp);
    return true;
}

bool readCoordToken(const std::string &text, std::size_t &pos, std::int64_t &value)
{
    double coord = 0.0;
    if (!readDoubleToken(text, pos, coord)) {
        return false;
    }
    value = static_cast<std::int64_t>(std::llround(coord * kCoordScale));
    return true;
}

void writeCoord(std::ostream &out, std::int64_t value)
{
    const double coord = static_cast<double>(value) / kCoordScale;
    if (std::fabs(coord - std::llround(coord)) < 1e-9) {
        out << static_cast<std::int64_t>(std::llround(coord));
    } else {
        out << std::setprecision(12) << coord;
    }
}

void addProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    props.push_back({name, value});
}

const std::string *findProperty(const std::vector<Property> &props, const std::string &name)
{
    for (const Property &prop : props) {
        if (prop.name == name) {
            return &prop.value;
        }
    }
    return nullptr;
}

void parseAttrBlock(const std::string &block, std::vector<Property> &props)
{
    if (block.size() < 2 || block.front() != '{' || block.back() != '}') {
        return;
    }
    std::string body = block.substr(1, block.size() - 2);
    for (char &ch : body) {
        if (ch == '\r') {
            ch = ' ';
        }
    }
    std::istringstream stream(body);
    std::string token;
    while (stream >> token) {
        const std::size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        addProperty(props, token.substr(0, eq), token.substr(eq + 1));
    }
}

std::string formatAttrBlock(const std::vector<Property> &props)
{
    if (props.empty()) {
        return "{}";
    }
    std::ostringstream oss;
    oss << '{';
    for (std::size_t i = 0; i < props.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        oss << props[i].name << '=' << props[i].value;
    }
    oss << '}';
    return oss.str();
}

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

struct WireRec {
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    std::int64_t x2 = 0;
    std::int64_t y2 = 0;
    std::string label;
};

std::uint32_t ensureLayer(CellContent &content, const std::string &name, LayerPurpose purpose)
{
    for (const LayerSpec &layer : content.layers()) {
        if (layer.name == name) {
            return layer.layerNum;
        }
    }
    LayerSpec spec;
    spec.layerNum = static_cast<std::uint16_t>(content.layers().size());
    spec.dataType = 0;
    spec.name = name;
    spec.purpose = purpose;
    content.layers().push_back(spec);
    return spec.layerNum;
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
    case Shape::Type::Rect: return shape.rect()->layerId;
    case Shape::Type::Polygon: return shape.polygon()->layerId;
    case Shape::Type::Path: return shape.path()->layerId;
    case Shape::Type::Text: return shape.text()->layerId;
    case Shape::Type::Arc: return shape.arc()->layerId;
    }
    return 0;
}

void buildNetsFromWires(const std::vector<WireRec> &wires, Block &block)
{
    if (wires.empty()) {
        return;
    }
    UnionFind uf;
    std::unordered_map<PointKey, std::string, PointKeyHash> rootLabels;
    for (const WireRec &wire : wires) {
        uf.unite({wire.x1, wire.y1}, {wire.x2, wire.y2});
        if (!wire.label.empty()) {
            rootLabels[uf.find({wire.x1, wire.y1})] = wire.label;
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
        std::string netName = rootLabels.count(entry.first) ? rootLabels[entry.first]
                                                            : ("N$" + std::to_string(++anonymous));
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

void parseVersionRecord(const std::string &raw, SourceInfo &info)
{
    std::size_t pos = 1;
    std::string block;
    if (!extractBracedToken(raw, pos, block)) {
        return;
    }
    info.setFormat("xschem");
    if (block.size() < 2) {
        return;
    }
    const std::string body = block.substr(1, block.size() - 2);
    const std::size_t firstLineEnd = body.find('\n');
    const std::string firstLine = firstLineEnd == std::string::npos ? body : body.substr(0, firstLineEnd);
    std::istringstream header(firstLine);
    std::string token;
    while (header >> token) {
        const std::size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = token.substr(0, eq);
        const std::string value = token.substr(eq + 1);
        if (key == "version") {
            info.setToolVersion(value);
        } else if (key == "file_version") {
            info.setFileVersion(value);
        }
    }
    if (firstLineEnd != std::string::npos && firstLineEnd + 1 < body.size()) {
        info.setComments(body.substr(firstLineEnd + 1));
    }
}

void parseComponentRecord(const std::string &raw, Block &block, std::vector<std::string> &warnings)
{
    if (raw.size() < 2 || raw[0] != 'C') {
        warnings.push_back("Skipping malformed component record");
        return;
    }
    std::size_t pos = 1;
    std::string symbol;
    if (!extractBracedToken(raw, pos, symbol)) {
        warnings.push_back("Component record missing symbol");
        return;
    }
    if (symbol.size() >= 2) {
        symbol = symbol.substr(1, symbol.size() - 2);
    }
    std::int64_t x = 0;
    std::int64_t y = 0;
    int rotate = 0;
    int mirror = 0;
    if (!readCoordToken(raw, pos, x) || !readCoordToken(raw, pos, y) || !readIntToken(raw, pos, rotate) ||
        !readIntToken(raw, pos, mirror)) {
        warnings.push_back("Component record missing placement fields");
        return;
    }
    Transform xf;
    xf.x = x;
    xf.y = y;
    xf.orient = orientFromXschem(rotate, mirror);
    Instance inst(symbol, xf);
    std::string attrs;
    if (extractBracedToken(raw, pos, attrs)) {
        parseAttrBlock(attrs, inst.properties());
    }
    block.instances().push_back(std::move(inst));
}

WireRec parseNetRecord(const std::string &raw, std::vector<std::string> &warnings)
{
    WireRec wire;
    if (raw.size() < 2 || raw[0] != 'N') {
        warnings.push_back("Skipping malformed net record");
        return wire;
    }
    std::size_t pos = 1;
    if (!readCoordToken(raw, pos, wire.x1) || !readCoordToken(raw, pos, wire.y1) ||
        !readCoordToken(raw, pos, wire.x2) || !readCoordToken(raw, pos, wire.y2)) {
        warnings.push_back("Net record missing coordinates");
    }
    std::string attrs;
    if (extractBracedToken(raw, pos, attrs)) {
        std::vector<Property> attrProps;
        parseAttrBlock(attrs, attrProps);
        if (const std::string *lab = findProperty(attrProps, "lab")) {
            wire.label = *lab;
        }
    }
    return wire;
}

void parseLineRecord(const std::string &raw, Block &block, std::uint32_t layerId)
{
    if (raw.size() < 2 || raw[0] != 'L') {
        return;
    }
    std::size_t pos = 1;
    int width = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    std::int64_t x2 = 0;
    std::int64_t y2 = 0;
    if (!readIntToken(raw, pos, width) || !readCoordToken(raw, pos, x1) || !readCoordToken(raw, pos, y1) ||
        !readCoordToken(raw, pos, x2) || !readCoordToken(raw, pos, y2)) {
        return;
    }
    Shape::PathData path;
    path.layerId = layerId;
    path.width = static_cast<std::uint32_t>(width);
    path.points = {Point{x1, y1}, Point{x2, y2}};
    block.shapes().push_back(Shape(path));
}

void parseTextRecord(const std::string &raw, Block &block, std::uint32_t layerId)
{
    if (raw.size() < 2 || raw[0] != 'T') {
        return;
    }
    std::size_t pos = 1;
    std::string textToken;
    if (!extractBracedToken(raw, pos, textToken)) {
        return;
    }
    const std::string text = textToken.size() >= 2 ? textToken.substr(1, textToken.size() - 2) : textToken;
    std::int64_t x = 0;
    std::int64_t y = 0;
    int rotate = 0;
    int mirror = 0;
    double sizeX = 0.2;
    double sizeY = 0.2;
    readCoordToken(raw, pos, x);
    readCoordToken(raw, pos, y);
    readIntToken(raw, pos, rotate);
    readIntToken(raw, pos, mirror);
    readDoubleToken(raw, pos, sizeX);
    readDoubleToken(raw, pos, sizeY);
    Shape::TextData textData;
    textData.layerId = layerId;
    textData.position = Point{x, y};
    textData.text = text;
    textData.height = static_cast<std::uint32_t>(sizeX * kCoordScale);
    Shape shape(textData);
    addProperty(shape.properties(), "rotate", std::to_string(rotate));
    addProperty(shape.properties(), "mirror", std::to_string(mirror));
    addProperty(shape.properties(), "sizeY", std::to_string(sizeY));
    block.shapes().push_back(std::move(shape));
}

void parsePolygonRecord(const std::string &raw, Block &block, std::uint32_t layerId)
{
    if (raw.size() < 2 || raw[0] != 'P') {
        return;
    }
    std::size_t pos = 1;
    int count = 0;
    if (!readIntToken(raw, pos, count) || count < 2) {
        return;
    }
    Shape::PolygonData polygon;
    polygon.layerId = layerId;
    for (int i = 0; i < count; ++i) {
        std::int64_t x = 0;
        std::int64_t y = 0;
        if (!readCoordToken(raw, pos, x) || !readCoordToken(raw, pos, y)) {
            break;
        }
        polygon.points.push_back(Point{x, y});
    }
    if (polygon.points.size() >= 2) {
        block.shapes().push_back(Shape(polygon));
    }
}

void parsePinRecord(const std::string &raw, Block &block, std::uint32_t layerId)
{
    if (raw.size() < 2 || raw[0] != 'B') {
        return;
    }
    std::size_t pos = 1;
    int width = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    std::int64_t x2 = 0;
    std::int64_t y2 = 0;
    if (!readIntToken(raw, pos, width) || !readCoordToken(raw, pos, x1) || !readCoordToken(raw, pos, y1) ||
        !readCoordToken(raw, pos, x2) || !readCoordToken(raw, pos, y2)) {
        return;
    }
    Shape::RectData rect;
    rect.layerId = layerId;
    rect.box = Box{x1, y1, x2, y2};
    Shape shape(rect);
    addProperty(shape.properties(), "pinWidth", std::to_string(width));
    std::string attrs;
    if (extractBracedToken(raw, pos, attrs)) {
        parseAttrBlock(attrs, shape.properties());
    }
    block.shapes().push_back(std::move(shape));
}

void parseArcRecord(const std::string &raw, Block &block, std::uint32_t layerId)
{
    if (raw.size() < 2 || raw[0] != 'A') {
        return;
    }
    std::size_t pos = 1;
    int width = 0;
    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;
    double startAngle = 0.0;
    double endAngle = 0.0;
    if (!readIntToken(raw, pos, width) || !readDoubleToken(raw, pos, cx) || !readDoubleToken(raw, pos, cy) ||
        !readDoubleToken(raw, pos, radius) || !readDoubleToken(raw, pos, startAngle) ||
        !readDoubleToken(raw, pos, endAngle)) {
        return;
    }
    Shape::ArcData arc;
    arc.layerId = layerId;
    arc.center = Point{static_cast<std::int64_t>(std::llround(cx * kCoordScale)),
                       static_cast<std::int64_t>(std::llround(cy * kCoordScale))};
    arc.radius = radius;
    arc.startAngle = startAngle;
    arc.endAngle = endAngle;
    arc.width = static_cast<std::uint32_t>(width);
    Shape shape(arc);
    std::string attrs;
    if (extractBracedToken(raw, pos, attrs)) {
        parseAttrBlock(attrs, shape.properties());
    }
    block.shapes().push_back(std::move(shape));
}

void parseMetadataRecord(char kind, const std::string &raw, Cell &cell, CellContent &content)
{
    std::size_t pos = 1;
    std::string block;
    if (!extractBracedToken(raw, pos, block)) {
        block = "{}";
    }
    std::vector<Property> props;
    parseAttrBlock(block, props);
    if (kind == 'G' || kind == 'K') {
        std::vector<Property> &target = (kind == 'G') ? cell.properties() : content.properties();
        for (Property &prop : props) {
            target.push_back(std::move(prop));
        }
        return;
    }
    addProperty(content.properties(), std::string(kSectionPrefix) + static_cast<char>(std::tolower(kind)),
                block.size() >= 2 ? block.substr(1, block.size() - 2) : "");
}

void writeVersionRecord(std::ostream &out, const SourceInfo &info)
{
    out << "v {xschem";
    out << " version=" << (info.toolVersion().empty() ? "3.4.4" : info.toolVersion());
    out << " file_version=" << (info.fileVersion().empty() ? "1.2" : info.fileVersion());
    if (!info.comments().empty()) {
        out << '\n' << info.comments();
    }
    out << "}\n";
}

void writeSectionRecord(std::ostream &out, char kind, const std::string &body) { out << kind << " {" << body << "}\n"; }

void writeNetRecord(std::ostream &out, const Shape::PathData &path, const std::vector<Property> &props)
{
    out << 'N' << ' ';
    writeCoord(out, path.points[0].x);
    out << ' ';
    writeCoord(out, path.points[0].y);
    out << ' ';
    writeCoord(out, path.points[1].x);
    out << ' ';
    writeCoord(out, path.points[1].y);
    if (const std::string *lab = findProperty(props, "lab")) {
        out << " {lab=" << *lab << "}";
    } else {
        out << " {}";
    }
    out << '\n';
}

void writeLineRecord(std::ostream &out, const Shape::PathData &path)
{
    out << 'L' << ' ' << path.width << ' ';
    writeCoord(out, path.points[0].x);
    out << ' ';
    writeCoord(out, path.points[0].y);
    out << ' ';
    writeCoord(out, path.points[1].x);
    out << ' ';
    writeCoord(out, path.points[1].y);
    out << " {}\n";
}

void writeTextRecord(std::ostream &out, const Shape::TextData &text, const std::vector<Property> &props)
{
    const int rotate = findProperty(props, "rotate") ? std::stoi(*findProperty(props, "rotate")) : 0;
    const int mirror = findProperty(props, "mirror") ? std::stoi(*findProperty(props, "mirror")) : 0;
    const double sizeX = static_cast<double>(text.height) / kCoordScale;
    const double sizeY = findProperty(props, "sizeY") ? std::stod(*findProperty(props, "sizeY")) : sizeX;
    out << 'T' << ' ' << '{' << text.text << '}' << ' ';
    writeCoord(out, text.position.x);
    out << ' ';
    writeCoord(out, text.position.y);
    out << ' ' << rotate << ' ' << mirror << ' ' << sizeX << ' ' << sizeY << " {}\n";
}

void writePolygonRecord(std::ostream &out, const Shape::PolygonData &polygon)
{
    out << 'P' << ' ' << polygon.points.size();
    for (const Point &pt : polygon.points) {
        out << ' ';
        writeCoord(out, pt.x);
        out << ' ';
        writeCoord(out, pt.y);
    }
    out << " {}\n";
}

void writePinRecord(std::ostream &out, const Shape::RectData &rect, const std::vector<Property> &props)
{
    const int width = findProperty(props, "pinWidth") ? std::stoi(*findProperty(props, "pinWidth")) : 5;
    std::vector<Property> pinProps;
    for (const Property &prop : props) {
        if (prop.name != "pinWidth") {
            pinProps.push_back(prop);
        }
    }
    out << 'B' << ' ' << width << ' ';
    writeCoord(out, rect.box.llx);
    out << ' ';
    writeCoord(out, rect.box.lly);
    out << ' ';
    writeCoord(out, rect.box.urx);
    out << ' ';
    writeCoord(out, rect.box.ury);
    out << ' ' << formatAttrBlock(pinProps) << '\n';
}

void writeArcRecord(std::ostream &out, const Shape::ArcData &arc, const std::vector<Property> &props)
{
    const double cx = static_cast<double>(arc.center.x) / kCoordScale;
    const double cy = static_cast<double>(arc.center.y) / kCoordScale;
    std::vector<Property> arcProps;
    for (const Property &prop : props) {
        if (prop.name.rfind("arc.", 0) == 0 || prop.name == "geometry") {
            continue;
        }
        arcProps.push_back(prop);
    }
    out << 'A' << ' ' << arc.width << ' ' << cx << ' ' << cy << ' ' << arc.radius << ' ' << arc.startAngle << ' '
        << arc.endAngle << ' ' << formatAttrBlock(arcProps) << '\n';
}

void writeArcFromPath(std::ostream &out, const Shape::PathData &path, const std::vector<Property> &props)
{
    Shape::ArcData arc;
    arc.layerId = path.layerId;
    arc.width = path.width;
    if (const std::string *radius = findProperty(props, "arc.radius")) {
        arc.radius = std::stod(*radius);
    }
    if (const std::string *start = findProperty(props, "arc.startAngle")) {
        arc.startAngle = std::stod(*start);
    }
    if (const std::string *end = findProperty(props, "arc.endAngle")) {
        arc.endAngle = std::stod(*end);
    }
    if (!path.points.empty()) {
        arc.center = path.points.front();
    }
    writeArcRecord(out, arc, props);
}

void writeComponentRecord(std::ostream &out, const Instance &inst)
{
    int rotate = 0;
    int mirror = 0;
    xschemFromOrient(inst.transform().orient, rotate, mirror);
    out << 'C' << " {" << inst.cellName() << "} ";
    writeCoord(out, inst.transform().x);
    out << ' ';
    writeCoord(out, inst.transform().y);
    out << ' ' << rotate << ' ' << mirror << ' ' << formatAttrBlock(inst.properties()) << '\n';
}

} // namespace

std::vector<std::string> readRecords(const std::string &path, std::vector<std::string> &errors)
{
    std::ifstream in(path);
    if (!in) {
        errors.push_back("Cannot open Xschem file: " + path);
        return {};
    }
    std::vector<std::string> records;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (trim(line).empty()) {
            continue;
        }
        std::string record = line;
        int braceBalance = countBraceDelta(line);
        while (braceBalance > 0 && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            record.push_back('\n');
            record += line;
            braceBalance += countBraceDelta(line);
        }
        records.push_back(record);
    }
    return records;
}

void importRecords(const std::vector<std::string> &records, Cell &cell, CellContent &content,
                   std::vector<std::string> &warnings)
{
    Block &block = content.block();
    block.instances().clear();
    block.nets().clear();
    block.shapes().clear();
    content.properties().clear();
    cell.properties().clear();
    content.sourceInfo() = SourceInfo{};

    const std::uint32_t wireLayer = ensureLayer(content, "wire", LayerPurpose::Wire);
    const std::uint32_t drawingLayer = ensureLayer(content, "drawing", LayerPurpose::Drawing);
    const std::uint32_t labelLayer = ensureLayer(content, "label", LayerPurpose::Label);
    const std::uint32_t pinLayer = ensureLayer(content, "pin", LayerPurpose::Pin);

    std::vector<WireRec> wires;
    for (const std::string &raw : records) {
        if (raw.empty()) {
            continue;
        }
        switch (raw[0]) {
        case 'v':
            parseVersionRecord(raw, content.sourceInfo());
            break;
        case 'G':
        case 'K':
        case 'V':
        case 'S':
        case 'E':
            parseMetadataRecord(raw[0], raw, cell, content);
            break;
        case 'C':
            parseComponentRecord(raw, block, warnings);
            break;
        case 'N': {
            WireRec wire = parseNetRecord(raw, warnings);
            wires.push_back(wire);
            Shape::PathData path;
            path.layerId = wireLayer;
            path.width = 1;
            path.points = {Point{wire.x1, wire.y1}, Point{wire.x2, wire.y2}};
            Shape shape(path);
            if (!wire.label.empty()) {
                addProperty(shape.properties(), "lab", wire.label);
            }
            block.shapes().push_back(std::move(shape));
            break;
        }
        case 'L':
            parseLineRecord(raw, block, drawingLayer);
            break;
        case 'T':
            parseTextRecord(raw, block, labelLayer);
            break;
        case 'P':
            parsePolygonRecord(raw, block, drawingLayer);
            break;
        case 'B':
            parsePinRecord(raw, block, pinLayer);
            break;
        case 'A':
            parseArcRecord(raw, block, drawingLayer);
            break;
        default:
            warnings.push_back("Skipping unsupported Xschem record: " + raw.substr(0, 1));
            break;
        }
    }
    buildNetsFromWires(wires, block);
    block.recomputeBBox();
}

void exportRecords(std::ostream &out, const Cell &cell, const CellContent &content)
{
    writeVersionRecord(out, content.sourceInfo());
    if (!cell.properties().empty()) {
        out << 'G' << ' ' << formatAttrBlock(cell.properties()) << '\n';
    } else {
        writeSectionRecord(out, 'G', "");
    }

    std::vector<Property> kProps;
    for (const Property &prop : content.properties()) {
        if (prop.name.rfind(kSectionPrefix, 0) == 0 || prop.name.rfind("editor.", 0) == 0) {
            continue;
        }
        kProps.push_back(prop);
    }
    if (!kProps.empty()) {
        out << 'K' << ' ' << formatAttrBlock(kProps) << '\n';
    }

    const std::string *sectionV = findProperty(content.properties(), std::string(kSectionPrefix) + "v");
    const std::string *sectionS = findProperty(content.properties(), std::string(kSectionPrefix) + "s");
    const std::string *sectionE = findProperty(content.properties(), std::string(kSectionPrefix) + "e");
    writeSectionRecord(out, 'V', sectionV ? *sectionV : "");
    writeSectionRecord(out, 'S', sectionS ? *sectionS : "");
    writeSectionRecord(out, 'E', sectionE ? *sectionE : "");

    const Block &block = content.block();
    for (const Shape &shape : block.shapes()) {
        if (layerPurpose(content, shapeLayerId(shape)) != LayerPurpose::Wire) {
            continue;
        }
        if (const Shape::PathData *path = shape.path()) {
            if (path->points.size() >= 2) {
                writeNetRecord(out, *path, shape.properties());
            }
        }
    }

    for (const Shape &shape : block.shapes()) {
        const LayerPurpose purpose = layerPurpose(content, shapeLayerId(shape));
        if (purpose == LayerPurpose::Wire) {
            continue;
        }
        switch (shape.type()) {
        case Shape::Type::Path:
            if (findProperty(shape.properties(), "geometry") != nullptr) {
                writeArcFromPath(out, *shape.path(), shape.properties());
            } else {
                writeLineRecord(out, *shape.path());
            }
            break;
        case Shape::Type::Text:
            writeTextRecord(out, *shape.text(), shape.properties());
            break;
        case Shape::Type::Polygon:
            writePolygonRecord(out, *shape.polygon());
            break;
        case Shape::Type::Rect:
            if (purpose == LayerPurpose::Pin) {
                writePinRecord(out, *shape.rect(), shape.properties());
            }
            break;
        case Shape::Type::Arc:
            writeArcRecord(out, *shape.arc(), shape.properties());
            break;
        default:
            break;
        }
    }

    for (const Instance &inst : block.instances()) {
        writeComponentRecord(out, inst);
    }
}

} // namespace core::xschem
