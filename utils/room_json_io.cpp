#include "room_json_io.h"

#include "room_paths.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace room {
namespace {

// ---------------------------------------------------------------------------
// Minimal JSON DOM (objects / arrays / string / number / bool / null)
// ---------------------------------------------------------------------------

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JsonValue> a;
    std::map<std::string, JsonValue> o;

    static JsonValue null() { return {}; }
    static JsonValue boolean(bool v)
    {
        JsonValue j;
        j.kind = Kind::Bool;
        j.b = v;
        return j;
    }
    static JsonValue number(double v)
    {
        JsonValue j;
        j.kind = Kind::Number;
        j.n = v;
        return j;
    }
    static JsonValue string(std::string v)
    {
        JsonValue j;
        j.kind = Kind::String;
        j.s = std::move(v);
        return j;
    }
    static JsonValue array()
    {
        JsonValue j;
        j.kind = Kind::Array;
        return j;
    }
    static JsonValue object()
    {
        JsonValue j;
        j.kind = Kind::Object;
        return j;
    }

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isBool() const { return kind == Kind::Bool; }

    const JsonValue *get(const std::string &key) const
    {
        if (!isObject()) {
            return nullptr;
        }
        const auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }

    std::string asString(const std::string &fallback = {}) const
    {
        return isString() ? s : fallback;
    }

    double asNumber(double fallback = 0.0) const
    {
        return isNumber() ? n : fallback;
    }

    std::int64_t asInt(std::int64_t fallback = 0) const
    {
        return isNumber() ? static_cast<std::int64_t>(std::llround(n)) : fallback;
    }

    bool asBool(bool fallback = false) const
    {
        return isBool() ? b : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : m_text(std::move(text)) {}

    JsonValue parse()
    {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (m_i != m_text.size()) {
            throw std::runtime_error("Trailing data in JSON");
        }
        return v;
    }

private:
    std::string m_text;
    std::size_t m_i = 0;

    [[noreturn]] void fail(const char *msg) const
    {
        throw std::runtime_error(std::string(msg) + " at offset " + std::to_string(m_i));
    }

    void skipWs()
    {
        while (m_i < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_i]))) {
            ++m_i;
        }
    }

    char peek() const
    {
        return m_i < m_text.size() ? m_text[m_i] : '\0';
    }

    char get()
    {
        return m_i < m_text.size() ? m_text[m_i++] : '\0';
    }

    bool match(char c)
    {
        skipWs();
        if (peek() != c) {
            return false;
        }
        ++m_i;
        return true;
    }

    void expect(char c)
    {
        skipWs();
        if (get() != c) {
            fail((std::string("Expected '") + c + "'").c_str());
        }
    }

    JsonValue parseValue()
    {
        skipWs();
        const char c = peek();
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            return JsonValue::string(parseString());
        }
        if (c == 't' || c == 'f') {
            return parseBool();
        }
        if (c == 'n') {
            return parseNull();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber();
        }
        fail("Unexpected token");
    }

    JsonValue parseObject()
    {
        expect('{');
        JsonValue obj = JsonValue::object();
        skipWs();
        if (match('}')) {
            return obj;
        }
        for (;;) {
            skipWs();
            if (peek() != '"') {
                fail("Expected object key");
            }
            std::string key = parseString();
            expect(':');
            obj.o.emplace(std::move(key), parseValue());
            skipWs();
            if (match('}')) {
                break;
            }
            expect(',');
        }
        return obj;
    }

    JsonValue parseArray()
    {
        expect('[');
        JsonValue arr = JsonValue::array();
        skipWs();
        if (match(']')) {
            return arr;
        }
        for (;;) {
            arr.a.push_back(parseValue());
            skipWs();
            if (match(']')) {
                break;
            }
            expect(',');
        }
        return arr;
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        while (m_i < m_text.size()) {
            const char c = get();
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                const char e = get();
                switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = get();
                        code <<= 4;
                        if (h >= '0' && h <= '9') {
                            code |= static_cast<unsigned>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            code |= static_cast<unsigned>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            code |= static_cast<unsigned>(h - 'A' + 10);
                        } else {
                            fail("Bad unicode escape");
                        }
                    }
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default:
                    fail("Bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        fail("Unterminated string");
    }

    JsonValue parseNumber()
    {
        skipWs();
        const std::size_t start = m_i;
        if (peek() == '-') {
            ++m_i;
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            ++m_i;
        }
        if (peek() == '.') {
            ++m_i;
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++m_i;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++m_i;
            if (peek() == '+' || peek() == '-') {
                ++m_i;
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++m_i;
            }
        }
        const std::string slice = m_text.substr(start, m_i - start);
        return JsonValue::number(std::stod(slice));
    }

    JsonValue parseBool()
    {
        skipWs();
        if (m_text.compare(m_i, 4, "true") == 0) {
            m_i += 4;
            return JsonValue::boolean(true);
        }
        if (m_text.compare(m_i, 5, "false") == 0) {
            m_i += 5;
            return JsonValue::boolean(false);
        }
        fail("Expected boolean");
    }

    JsonValue parseNull()
    {
        skipWs();
        if (m_text.compare(m_i, 4, "null") == 0) {
            m_i += 4;
            return JsonValue::null();
        }
        fail("Expected null");
    }
};

// ---------------------------------------------------------------------------
// Writer helpers
// ---------------------------------------------------------------------------

class JsonWriter {
public:
    explicit JsonWriter(std::ostream &out) : m_out(out) {}

    void beginObject()
    {
        beforeValue();
        m_out << '{';
        ++m_depth;
        m_first.push_back(true);
    }

    void endObject()
    {
        --m_depth;
        m_first.pop_back();
        nl();
        m_out << '}';
    }

    void beginArray()
    {
        beforeValue();
        m_out << '[';
        ++m_depth;
        m_first.push_back(true);
    }

    void endArray()
    {
        --m_depth;
        m_first.pop_back();
        nl();
        m_out << ']';
    }

    void key(const std::string &k)
    {
        comma();
        nl();
        writeString(k);
        m_out << ": ";
        m_pendingKey = true;
    }

    void valueNull()
    {
        beforeValue();
        m_out << "null";
    }

    void valueBool(bool v)
    {
        beforeValue();
        m_out << (v ? "true" : "false");
    }

    void valueNumber(double v)
    {
        beforeValue();
        if (std::isfinite(v) && std::floor(v) == v && std::abs(v) < 1e15) {
            m_out << static_cast<long long>(v);
        } else {
            m_out << v;
        }
    }

    void valueInt(std::int64_t v)
    {
        beforeValue();
        m_out << v;
    }

    void valueUInt(std::uint64_t v)
    {
        beforeValue();
        m_out << v;
    }

    void valueString(const std::string &v)
    {
        beforeValue();
        writeString(v);
    }

private:
    std::ostream &m_out;
    int m_depth = 0;
    std::vector<bool> m_first;
    bool m_pendingKey = false;

    void beforeValue()
    {
        if (!m_pendingKey) {
            comma();
            nl();
        }
        m_pendingKey = false;
    }

    void comma()
    {
        if (m_first.empty()) {
            return;
        }
        if (m_first.back()) {
            m_first.back() = false;
            return;
        }
        m_out << ',';
    }

    void nl()
    {
        m_out << '\n';
        for (int i = 0; i < m_depth; ++i) {
            m_out << "  ";
        }
    }

    void writeString(const std::string &v)
    {
        m_out << '"';
        for (unsigned char c : v) {
            switch (c) {
            case '"':
                m_out << "\\\"";
                break;
            case '\\':
                m_out << "\\\\";
                break;
            case '\b':
                m_out << "\\b";
                break;
            case '\f':
                m_out << "\\f";
                break;
            case '\n':
                m_out << "\\n";
                break;
            case '\r':
                m_out << "\\r";
                break;
            case '\t':
                m_out << "\\t";
                break;
            default:
                if (c < 0x20) {
                    static const char *hex = "0123456789abcdef";
                    m_out << "\\u00" << hex[c >> 4] << hex[c & 0xF];
                } else {
                    m_out << static_cast<char>(c);
                }
            }
        }
        m_out << '"';
    }
};

std::string orientToString(Orient o)
{
    switch (o) {
    case Orient::R0:
        return "R0";
    case Orient::R90:
        return "R90";
    case Orient::R180:
        return "R180";
    case Orient::R270:
        return "R270";
    case Orient::MY:
        return "MY";
    case Orient::MX:
        return "MX";
    case Orient::MX90:
        return "MX90";
    case Orient::MY90:
        return "MY90";
    }
    return "R0";
}

Orient orientFromString(const std::string &s)
{
    if (s == "R90") {
        return Orient::R90;
    }
    if (s == "R180") {
        return Orient::R180;
    }
    if (s == "R270") {
        return Orient::R270;
    }
    if (s == "MY") {
        return Orient::MY;
    }
    if (s == "MX") {
        return Orient::MX;
    }
    if (s == "MX90") {
        return Orient::MX90;
    }
    if (s == "MY90") {
        return Orient::MY90;
    }
    return Orient::R0;
}

LayerPurpose purposeFromString(const std::string &s)
{
    if (s == "pin") {
        return LayerPurpose::Pin;
    }
    if (s == "label") {
        return LayerPurpose::Label;
    }
    if (s == "boundary") {
        return LayerPurpose::Boundary;
    }
    if (s == "blockage") {
        return LayerPurpose::Blockage;
    }
    if (s == "wire") {
        return LayerPurpose::Wire;
    }
    if (s == "fill") {
        return LayerPurpose::Fill;
    }
    if (s == "other") {
        return LayerPurpose::Other;
    }
    return LayerPurpose::Drawing;
}

SigType sigTypeFromString(const std::string &s)
{
    if (s == "power") {
        return SigType::Power;
    }
    if (s == "ground") {
        return SigType::Ground;
    }
    if (s == "clock") {
        return SigType::Clock;
    }
    return SigType::Signal;
}

void writeProperties(JsonWriter &w, const std::vector<Property> &props)
{
    w.key("properties");
    w.beginArray();
    for (const Property &p : props) {
        w.beginObject();
        w.key("name");
        w.valueString(p.name);
        w.key("value");
        w.valueString(p.value);
        w.endObject();
    }
    w.endArray();
}

void writePoint(JsonWriter &w, const Point &p)
{
    w.beginObject();
    w.key("x");
    w.valueInt(p.x);
    w.key("y");
    w.valueInt(p.y);
    w.endObject();
}

void writeBox(JsonWriter &w, const Box &b)
{
    w.beginObject();
    w.key("llx");
    w.valueInt(b.llx);
    w.key("lly");
    w.valueInt(b.lly);
    w.key("urx");
    w.valueInt(b.urx);
    w.key("ury");
    w.valueInt(b.ury);
    w.endObject();
}

void writeLayers(JsonWriter &w, const std::vector<LayerSpec> &layers)
{
    w.key("layers");
    w.beginArray();
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const LayerSpec &L = layers[i];
        w.beginObject();
        w.key("id");
        w.valueUInt(static_cast<std::uint64_t>(i));
        w.key("layerNum");
        w.valueUInt(L.layerNum);
        w.key("dataType");
        w.valueUInt(L.dataType);
        w.key("name");
        w.valueString(L.name);
        w.key("purpose");
        w.valueString(layerPurposeToString(L.purpose));
        w.endObject();
    }
    w.endArray();
}

void writeBlock(JsonWriter &w, const Block &block)
{
    w.key("block");
    w.beginObject();
    w.key("bbox");
    writeBox(w, block.bbox());

    w.key("shapes");
    w.beginArray();
    for (const Shape &sh : block.shapes()) {
        w.beginObject();
        switch (sh.type()) {
        case Shape::Type::Rect: {
            const auto *r = sh.rect();
            w.key("type");
            w.valueString("rect");
            w.key("layerId");
            w.valueUInt(r->layerId);
            w.key("box");
            writeBox(w, r->box);
            break;
        }
        case Shape::Type::Polygon: {
            const auto *p = sh.polygon();
            w.key("type");
            w.valueString("polygon");
            w.key("layerId");
            w.valueUInt(p->layerId);
            w.key("points");
            w.beginArray();
            for (const Point &pt : p->points) {
                writePoint(w, pt);
            }
            w.endArray();
            break;
        }
        case Shape::Type::Path: {
            const auto *p = sh.path();
            w.key("type");
            w.valueString("path");
            w.key("layerId");
            w.valueUInt(p->layerId);
            w.key("width");
            w.valueUInt(p->width);
            w.key("points");
            w.beginArray();
            for (const Point &pt : p->points) {
                writePoint(w, pt);
            }
            w.endArray();
            break;
        }
        case Shape::Type::Text: {
            const auto *t = sh.text();
            w.key("type");
            w.valueString("text");
            w.key("layerId");
            w.valueUInt(t->layerId);
            w.key("height");
            w.valueUInt(t->height);
            w.key("text");
            w.valueString(t->text);
            w.key("position");
            writePoint(w, t->position);
            break;
        }
        case Shape::Type::Arc: {
            const auto *a = sh.arc();
            w.key("type");
            w.valueString("arc");
            w.key("layerId");
            w.valueUInt(a->layerId);
            w.key("width");
            w.valueUInt(a->width);
            w.key("radius");
            w.valueNumber(a->radius);
            w.key("startAngle");
            w.valueNumber(a->startAngle);
            w.key("endAngle");
            w.valueNumber(a->endAngle);
            w.key("center");
            writePoint(w, a->center);
            break;
        }
        }
        writeProperties(w, sh.properties());
        w.endObject();
    }
    w.endArray();

    w.key("instances");
    w.beginArray();
    for (const Instance &inst : block.instances()) {
        w.beginObject();
        w.key("cellName");
        w.valueString(inst.cellName());
        w.key("transform");
        w.beginObject();
        w.key("x");
        w.valueInt(inst.transform().x);
        w.key("y");
        w.valueInt(inst.transform().y);
        w.key("orient");
        w.valueString(orientToString(inst.transform().orient));
        w.key("mag");
        w.valueNumber(inst.transform().mag);
        w.endObject();
        writeProperties(w, inst.properties());
        w.endObject();
    }
    w.endArray();

    w.key("nets");
    w.beginArray();
    for (const Net &net : block.nets()) {
        w.beginObject();
        w.key("name");
        w.valueString(net.name());
        w.key("sigType");
        w.valueString(sigTypeToString(net.sigType()));
        w.key("terms");
        w.beginArray();
        for (const Term &t : net.terms()) {
            w.beginObject();
            w.key("name");
            w.valueString(t.name());
            w.key("layerId");
            w.valueUInt(t.layerId());
            w.key("position");
            writePoint(w, t.position());
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();

    w.endObject();
}

Point readPoint(const JsonValue &v)
{
    return Point(v.get("x") ? v.get("x")->asInt() : 0, v.get("y") ? v.get("y")->asInt() : 0);
}

Box readBox(const JsonValue &v)
{
    return Box(v.get("llx") ? v.get("llx")->asInt() : 0,
               v.get("lly") ? v.get("lly")->asInt() : 0,
               v.get("urx") ? v.get("urx")->asInt() : 0,
               v.get("ury") ? v.get("ury")->asInt() : 0);
}

std::vector<Property> readProperties(const JsonValue *arr)
{
    std::vector<Property> out;
    if (arr == nullptr || !arr->isArray()) {
        return out;
    }
    for (const JsonValue &item : arr->a) {
        if (!item.isObject()) {
            continue;
        }
        const std::string name = item.get("name") ? item.get("name")->asString() : std::string();
        const std::string value = item.get("value") ? item.get("value")->asString() : std::string();
        out.emplace_back(name, value);
    }
    return out;
}

void readLayers(const JsonValue *arr, std::vector<LayerSpec> &layers)
{
    layers.clear();
    if (arr == nullptr || !arr->isArray()) {
        return;
    }
    layers.resize(arr->a.size());
    for (const JsonValue &item : arr->a) {
        if (!item.isObject()) {
            continue;
        }
        const std::size_t id = item.get("id") ? static_cast<std::size_t>(item.get("id")->asInt()) : layers.size();
        LayerSpec L;
        L.layerNum = static_cast<std::uint16_t>(item.get("layerNum") ? item.get("layerNum")->asInt() : 0);
        L.dataType = static_cast<std::uint16_t>(item.get("dataType") ? item.get("dataType")->asInt() : 0);
        L.name = item.get("name") ? item.get("name")->asString() : std::string();
        L.purpose = purposeFromString(item.get("purpose") ? item.get("purpose")->asString() : "drawing");
        if (id >= layers.size()) {
            layers.resize(id + 1);
        }
        layers[id] = std::move(L);
    }
}

void readBlock(const JsonValue &blockObj, Block &block)
{
    if (const JsonValue *bbox = blockObj.get("bbox")) {
        // bbox is recomputed after load; accept field for forward-compat.
        (void)readBox(*bbox);
    }

    if (const JsonValue *shapes = blockObj.get("shapes")) {
        if (shapes->isArray()) {
            for (const JsonValue &sh : shapes->a) {
                if (!sh.isObject()) {
                    continue;
                }
                const std::string type = sh.get("type") ? sh.get("type")->asString() : std::string();
                if (type == "rect") {
                    Shape::RectData d;
                    d.layerId = static_cast<std::uint32_t>(sh.get("layerId") ? sh.get("layerId")->asInt() : 0);
                    if (const JsonValue *box = sh.get("box")) {
                        d.box = readBox(*box);
                    }
                    Shape shape(d);
                    shape.properties() = readProperties(sh.get("properties"));
                    block.shapes().push_back(std::move(shape));
                } else if (type == "polygon") {
                    Shape::PolygonData d;
                    d.layerId = static_cast<std::uint32_t>(sh.get("layerId") ? sh.get("layerId")->asInt() : 0);
                    if (const JsonValue *pts = sh.get("points")) {
                        if (pts->isArray()) {
                            for (const JsonValue &pt : pts->a) {
                                d.points.push_back(readPoint(pt));
                            }
                        }
                    }
                    Shape shape(d);
                    shape.properties() = readProperties(sh.get("properties"));
                    block.shapes().push_back(std::move(shape));
                } else if (type == "path") {
                    Shape::PathData d;
                    d.layerId = static_cast<std::uint32_t>(sh.get("layerId") ? sh.get("layerId")->asInt() : 0);
                    d.width = static_cast<std::uint32_t>(sh.get("width") ? sh.get("width")->asInt() : 0);
                    if (const JsonValue *pts = sh.get("points")) {
                        if (pts->isArray()) {
                            for (const JsonValue &pt : pts->a) {
                                d.points.push_back(readPoint(pt));
                            }
                        }
                    }
                    Shape shape(d);
                    shape.properties() = readProperties(sh.get("properties"));
                    block.shapes().push_back(std::move(shape));
                } else if (type == "text") {
                    Shape::TextData d;
                    d.layerId = static_cast<std::uint32_t>(sh.get("layerId") ? sh.get("layerId")->asInt() : 0);
                    d.height = static_cast<std::uint32_t>(sh.get("height") ? sh.get("height")->asInt() : 0);
                    d.text = sh.get("text") ? sh.get("text")->asString() : std::string();
                    if (const JsonValue *pos = sh.get("position")) {
                        d.position = readPoint(*pos);
                    }
                    Shape shape(d);
                    shape.properties() = readProperties(sh.get("properties"));
                    block.shapes().push_back(std::move(shape));
                } else if (type == "arc") {
                    Shape::ArcData d;
                    d.layerId = static_cast<std::uint32_t>(sh.get("layerId") ? sh.get("layerId")->asInt() : 0);
                    d.width = static_cast<std::uint32_t>(sh.get("width") ? sh.get("width")->asInt() : 0);
                    d.radius = sh.get("radius") ? sh.get("radius")->asNumber() : 0.0;
                    d.startAngle = sh.get("startAngle") ? sh.get("startAngle")->asNumber() : 0.0;
                    d.endAngle = sh.get("endAngle") ? sh.get("endAngle")->asNumber() : 0.0;
                    if (const JsonValue *c = sh.get("center")) {
                        d.center = readPoint(*c);
                    }
                    Shape shape(d);
                    shape.properties() = readProperties(sh.get("properties"));
                    block.shapes().push_back(std::move(shape));
                }
            }
        }
    }

    if (const JsonValue *instances = blockObj.get("instances")) {
        if (instances->isArray()) {
            for (const JsonValue &inst : instances->a) {
                if (!inst.isObject()) {
                    continue;
                }
                const std::string cellName = inst.get("cellName") ? inst.get("cellName")->asString() : std::string();
                Transform xf;
                if (const JsonValue *t = inst.get("transform")) {
                    xf.x = t->get("x") ? t->get("x")->asInt() : 0;
                    xf.y = t->get("y") ? t->get("y")->asInt() : 0;
                    xf.orient = orientFromString(t->get("orient") ? t->get("orient")->asString() : "R0");
                    xf.mag = t->get("mag") ? t->get("mag")->asNumber(1.0) : 1.0;
                }
                Instance obj(cellName, xf);
                obj.properties() = readProperties(inst.get("properties"));
                block.instances().push_back(std::move(obj));
            }
        }
    }

    if (const JsonValue *nets = blockObj.get("nets")) {
        if (nets->isArray()) {
            for (const JsonValue &netVal : nets->a) {
                if (!netVal.isObject()) {
                    continue;
                }
                const std::string name = netVal.get("name") ? netVal.get("name")->asString() : std::string();
                const SigType sig = sigTypeFromString(netVal.get("sigType") ? netVal.get("sigType")->asString() : "signal");
                Net net(name, sig);
                if (const JsonValue *terms = netVal.get("terms")) {
                    if (terms->isArray()) {
                        for (const JsonValue &termVal : terms->a) {
                            if (!termVal.isObject()) {
                                continue;
                            }
                            const std::string tname = termVal.get("name") ? termVal.get("name")->asString() : std::string();
                            const std::uint32_t layerId =
                                static_cast<std::uint32_t>(termVal.get("layerId") ? termVal.get("layerId")->asInt() : 0);
                            Point pos;
                            if (const JsonValue *p = termVal.get("position")) {
                                pos = readPoint(*p);
                            }
                            net.terms().emplace_back(tname, layerId, pos);
                        }
                    }
                }
                block.nets().push_back(std::move(net));
            }
        }
    }

    block.recomputeBBox();
}

std::string readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

RoomJsonIo::Result RoomJsonIo::exportToFile(const Database &db,
                                            const std::string &cellName,
                                            ViewType view,
                                            const std::string &jsonPath)
{
    Result result;
    if (view != ViewType::Schematic && view != ViewType::Symbol) {
        result.message = "ROOM JSON supports only schematic and symbol views";
        return result;
    }

    const Cell *cell = db.lib().findCell(cellName);
    if (cell == nullptr) {
        result.message = "Cell not found: " + cellName;
        return result;
    }
    const CellContent *content = cell->findContent(view);
    if (content == nullptr) {
        result.message = "Cell has no " + viewTypeToString(view) + " view: " + cellName;
        return result;
    }

    std::ofstream out(jsonPath, std::ios::binary);
    if (!out) {
        result.message = "Cannot write: " + jsonPath;
        return result;
    }

    JsonWriter w(out);
    w.beginObject();
    w.key("format");
    w.valueString("room-json");
    w.key("version");
    w.valueString("1.0");
    w.key("view");
    w.valueString(viewTypeToString(view));
    w.key("lib");
    w.valueString(db.lib().name());
    w.key("cell");
    w.valueString(cellName);
    w.key("generator");
    w.valueString(db.generator());
    w.key("technology");
    w.valueString(db.technology());
    w.key("dbuPerMicron");
    w.valueNumber(content->dbuPerMicron());
    w.key("dbuPerEditorUnit");
    w.valueNumber(content->dbuPerEditorUnit());
    writeLayers(w, content->layers());
    writeProperties(w, content->properties());
    writeBlock(w, content->block());
    w.endObject();
    out << '\n';

    if (!out) {
        result.message = "Write failed: " + jsonPath;
        return result;
    }
    result.ok = true;
    result.message = jsonPath;
    return result;
}

RoomJsonIo::Result RoomJsonIo::importFromFile(const std::string &jsonPath, Database &dbOut)
{
    Result result;
    try {
        const std::string text = readFile(jsonPath);
        JsonParser parser(text);
        const JsonValue root = parser.parse();
        if (!root.isObject()) {
            result.message = "JSON root must be an object";
            return result;
        }

        const std::string format = root.get("format") ? root.get("format")->asString() : std::string();
        if (!format.empty() && format != "room-json") {
            result.message = "Unsupported format field: " + format;
            return result;
        }

        const std::string viewStr = root.get("view") ? root.get("view")->asString() : std::string();
        const auto viewOpt = parseViewTypeName(viewStr);
        if (!viewOpt.has_value() ||
            (*viewOpt != ViewType::Schematic && *viewOpt != ViewType::Symbol)) {
            result.message = "JSON view must be schematic or symbol (got '" + viewStr + "')";
            return result;
        }
        const ViewType view = *viewOpt;

        const std::string cellName = root.get("cell") ? root.get("cell")->asString() : std::string();
        if (cellName.empty()) {
            result.message = "JSON missing cell name";
            return result;
        }

        Database db;
        db.setGenerator(root.get("generator") ? root.get("generator")->asString("room_json_io") : "room_json_io");
        db.setTechnology(root.get("technology") ? root.get("technology")->asString() : std::string());
        db.lib().setName(root.get("lib") ? root.get("lib")->asString("default") : "default");

        const double dbuPerMicron = root.get("dbuPerMicron") ? root.get("dbuPerMicron")->asNumber(1000.0) : 1000.0;
        const double dbuPerEditorUnit =
            root.get("dbuPerEditorUnit") ? root.get("dbuPerEditorUnit")->asNumber(0.0) : 0.0;

        Cell &cell = db.lib().getOrCreateCell(cellName);
        CellContent &content = cell.getOrCreateContent(view, dbuPerMicron);
        content.setDbuPerEditorUnit(dbuPerEditorUnit);
        content.properties() = readProperties(root.get("properties"));
        readLayers(root.get("layers"), content.layers());
        if (const JsonValue *block = root.get("block")) {
            if (block->isObject()) {
                readBlock(*block, content.block());
            }
        } else {
            content.block().recomputeBBox();
        }

        dbOut = std::move(db);
        result.ok = true;
        result.message = cellName + "." + viewTypeToString(view);
        return result;
    } catch (const std::exception &ex) {
        result.message = ex.what();
        return result;
    }
}

namespace {

bool endsWithInsensitive(const std::string &text, const std::string &suffix)
{
    if (text.size() < suffix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(
            text[text.size() - suffix.size() + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

std::string cellNameForRoomPath(const Database &db, const std::string &roomPath,
                                const std::string &cellName)
{
    if (!cellName.empty()) {
        return cellName;
    }
    const ParsedRoomPath parsed = parseRoomFilePath(
        RoomJsonIo::isRoomJsonPath(roomPath) ? RoomJsonIo::siblingTwinPath(roomPath) : roomPath);
    if (parsed.valid && !parsed.cellName.empty() && db.lib().findCell(parsed.cellName) != nullptr) {
        return parsed.cellName;
    }
    if (!db.lib().cells().empty()) {
        return db.lib().cells().front().name();
    }
    return {};
}

ViewType viewForRoomPath(const Database &db, const std::string &path)
{
    const std::string roomish =
        RoomJsonIo::isRoomJsonPath(path) ? RoomJsonIo::siblingTwinPath(path) : path;
    const ParsedRoomPath parsed = parseRoomFilePath(roomish);
    if (parsed.valid) {
        return parsed.view;
    }
    if (!db.lib().cells().empty()) {
        const Cell &cell = db.lib().cells().front();
        if (cell.findContent(ViewType::Schematic) != nullptr) {
            return ViewType::Schematic;
        }
        if (cell.findContent(ViewType::Symbol) != nullptr) {
            return ViewType::Symbol;
        }
    }
    return db.fileView();
}

} // namespace

RoomJsonIo::Result RoomJsonIo::roomFileToJson(const std::string &roomPath,
                                              const std::string &jsonPath,
                                              const std::string &cellName)
{
    Result result;
    try {
        if (isJsonRoom(roomPath)) {
            result.message = "Already room-json content";
            return result;
        }
        Database db = Database::loadFromFile(roomPath);
        ViewType view = viewForRoomPath(db, roomPath);
        if (view != ViewType::Schematic && view != ViewType::Symbol) {
            result.message = "ROOM file view is not schematic/symbol";
            return result;
        }

        const std::string cell = cellNameForRoomPath(db, roomPath, cellName);
        if (cell.empty()) {
            result.message = "No cell to export";
            return result;
        }
        return exportToFile(db, cell, view, jsonPath);
    } catch (const std::exception &ex) {
        result.message = ex.what();
        return result;
    }
}

bool RoomJsonIo::isJsonRoom(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    // Skip UTF-8 BOM and leading whitespace; Cap’n Proto never starts with '{'.
    unsigned char bom[3] = {0, 0, 0};
    in.read(reinterpret_cast<char *>(bom), 3);
    const auto n = static_cast<std::streamsize>(in.gcount());
    if (!(n == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)) {
        in.clear();
        in.seekg(0);
    }
    int ch = 0;
    while ((ch = in.get()) != EOF) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            continue;
        }
        return ch == '{';
    }
    return false;
}

bool RoomJsonIo::isRoomJsonPath(const std::string &path)
{
    return endsWithInsensitive(path, ".json");
}

RoomJsonIo::Result RoomJsonIo::loadRoomOrJson(const std::string &path, Database &dbOut)
{
    Result result;
    try {
        if (isRoomJsonPath(path) || isJsonRoom(path)) {
            return importFromFile(path, dbOut);
        }
        dbOut = Database::loadFromFile(path);
        result.ok = true;
        result.message = path;
        return result;
    } catch (const std::exception &ex) {
        result.message = ex.what();
        return result;
    }
}

RoomJsonIo::Result RoomJsonIo::convertInPlaceToJson(const std::string &roomPath,
                                                    const std::string &cellName)
{
    Result result;
    try {
        if (isJsonRoom(roomPath)) {
            result.ok = true;
            result.message = roomPath;
            return result;
        }
        const std::string tmp = roomPath + ".json.tmp";
        result = roomFileToJson(roomPath, tmp, cellName);
        if (!result.ok) {
            std::remove(tmp.c_str());
            return result;
        }
        std::remove(roomPath.c_str());
        if (std::rename(tmp.c_str(), roomPath.c_str()) != 0) {
            result.ok = false;
            result.message = "Failed to replace " + roomPath + " with JSON";
            return result;
        }
        result.ok = true;
        result.message = roomPath;
        return result;
    } catch (const std::exception &ex) {
        result.ok = false;
        result.message = ex.what();
        return result;
    }
}

RoomJsonIo::Result RoomJsonIo::convertInPlaceToBinary(const std::string &roomPath)
{
    Result result;
    try {
        if (!isJsonRoom(roomPath) && !isRoomJsonPath(roomPath)) {
            result.ok = true;
            result.message = roomPath;
            return result;
        }
        Database db;
        result = importFromFile(roomPath, db);
        if (!result.ok) {
            return result;
        }
        const ViewType view = viewForRoomPath(db, roomPath);
        if (view != ViewType::Schematic && view != ViewType::Symbol) {
            result.ok = false;
            result.message = "JSON view is not schematic/symbol";
            return result;
        }
        const std::string tmp = roomPath + ".bin.tmp";
        db.saveToFile(tmp, view);
        std::remove(roomPath.c_str());
        if (std::rename(tmp.c_str(), roomPath.c_str()) != 0) {
            result.ok = false;
            result.message = "Failed to replace " + roomPath + " with binary ROOM";
            return result;
        }
        result.ok = true;
        result.message = roomPath;
        return result;
    } catch (const std::exception &ex) {
        result.ok = false;
        result.message = ex.what();
        return result;
    }
}

RoomJsonIo::Result RoomJsonIo::jsonFileToRoom(const std::string &jsonPath, const std::string &roomPath)
{
    Database db;
    Result result = importFromFile(jsonPath, db);
    if (!result.ok) {
        return result;
    }
    try {
        ViewType view = ViewType::Schematic;
        if (!db.lib().cells().empty()) {
            const Cell &cell = db.lib().cells().front();
            if (cell.findContent(ViewType::Symbol) != nullptr &&
                cell.findContent(ViewType::Schematic) == nullptr) {
                view = ViewType::Symbol;
            } else if (cell.findContent(ViewType::Schematic) != nullptr) {
                view = ViewType::Schematic;
            } else if (cell.findContent(ViewType::Symbol) != nullptr) {
                view = ViewType::Symbol;
            }
        }
        // Prefer view from JSON path if encoded.
        const std::string lower = jsonPath;
        if (lower.size() >= 13 && lower.compare(lower.size() - 13, 13, ".symbol.json") == 0) {
            view = ViewType::Symbol;
        } else if (lower.size() >= 16 && lower.compare(lower.size() - 16, 16, ".schematic.json") == 0) {
            view = ViewType::Schematic;
        }
        db.saveToFile(roomPath, view);
        result.ok = true;
        result.message = roomPath;
        return result;
    } catch (const std::exception &ex) {
        result.ok = false;
        result.message = ex.what();
        return result;
    }
}

std::string RoomJsonIo::siblingTwinPath(const std::string &path)
{
    auto endsWithInsensitive = [](const std::string &text, const std::string &suffix) {
        if (text.size() < suffix.size()) {
            return false;
        }
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(
                text[text.size() - suffix.size() + i])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
            if (a != b) {
                return false;
            }
        }
        return true;
    };
    if (endsWithInsensitive(path, ".room")) {
        return path.substr(0, path.size() - 5) + ".json";
    }
    if (endsWithInsensitive(path, ".json")) {
        return path.substr(0, path.size() - 5) + ".room";
    }
    return path + ".json";
}

RoomJsonIo::Result RoomJsonIo::saveViewWithJsonTwin(Database &db, ViewType view, const std::string &path)
{
    Result result;
    try {
        const bool schematicOrSymbol = (view == ViewType::Schematic || view == ViewType::Symbol);
        if (!schematicOrSymbol) {
            db.saveToFile(path, view);
            result.ok = true;
            result.message = path;
            return result;
        }

        // Keep on-disk format: JSON-in-*.room / *.json stay text; otherwise Cap’n Proto.
        const bool asJson = isRoomJsonPath(path) || isJsonRoom(path);
        if (!asJson) {
            db.saveToFile(path, view);
            result.ok = true;
            result.message = path;
            return result;
        }

        const std::string cellName = cellNameForRoomPath(db, path, {});
        if (cellName.empty()) {
            result.message = "No cell to export as JSON";
            return result;
        }
        return exportToFile(db, cellName, view, path);
    } catch (const std::exception &ex) {
        result.message = ex.what();
        return result;
    }
}

} // namespace room
