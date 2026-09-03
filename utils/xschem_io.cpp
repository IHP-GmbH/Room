/*!****************************************************************************************
 * \file xschem_io.cpp
 * \brief Shared Xschem record parser and CORE Block mapping (format-neutral model).
 *****************************************************************************************/

#include "xschem_io.h"

#include "coord_scale.h"
#include "net.h"
#include "net_name_propagation.h"
#include "pin_retarget.h"
#include "pulse_params.h"
#include "shape.h"
#include "xschem_format.h"

#include "block.h"
#include "primitive_resolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace core::xschem {
namespace {

double g_dbuPerEditorUnit = kXschemDbuPerEditorUnit;

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

// Qucs .sch often stores "\\n" as two chars in CORE; Xschem/ngspice then see mos_ttn.
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

std::string extractWriteRawFile(const std::string &spiceText)
{
    // "write test_inverter.raw" → test_inverter.raw
    const std::string key = "write ";
    const std::size_t pos = spiceText.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    std::size_t i = pos + key.size();
    while (i < spiceText.size() && std::isspace(static_cast<unsigned char>(spiceText[i]))) {
        ++i;
    }
    std::size_t j = i;
    while (j < spiceText.size() && !std::isspace(static_cast<unsigned char>(spiceText[j])) && spiceText[j] != '\r'
           && spiceText[j] != '\n') {
        ++j;
    }
    return spiceText.substr(i, j - i);
}

// Qucs stores "1.2 V"; ngspice rejects "1.2 V" as unknown parameter (v).
std::string normalizeSpiceDeviceValue(const std::string &raw)
{
    std::string s;
    s.reserve(raw.size());
    for (char ch : raw) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            s.push_back(ch);
        }
    }
    if (s.size() >= 2) {
        const char last = s.back();
        const char prev = s[s.size() - 2];
        // Strip trailing unit letter when preceded by a digit (1.2V, 10nF, 1kOhm → strip Ohm separately).
        if ((last == 'V' || last == 'v' || last == 'A' || last == 'a' || last == 'F' || last == 'f' || last == 'H'
             || last == 'h')
            && std::isdigit(static_cast<unsigned char>(prev))) {
            s.pop_back();
        } else if (s.size() >= 3 && (s.compare(s.size() - 3, 3, "Ohm") == 0 || s.compare(s.size() - 3, 3, "ohm") == 0)) {
            s.resize(s.size() - 3);
        }
    }
    return s;
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
    value = editorUnitsToDbu(coord, g_dbuPerEditorUnit);
    return true;
}

void writeCoord(std::ostream &out, std::int64_t value)
{
    const double coord = dbuToEditorUnits(value, g_dbuPerEditorUnit);
    if (std::fabs(coord - std::llround(coord)) < 1e-9) {
        out << static_cast<std::int64_t>(std::llround(coord));
    } else {
        out << std::setprecision(12) << coord;
    }
}

std::string mapQucsSymbol(const std::string &qucsType)
{
    static const std::unordered_map<std::string, std::string> kMap = {
        {"GND", "gnd.sym"},
        {"Vdc", "vsource.sym"},
        {"Vac", "vsource.sym"},
        {"Vpulse", "vsource.sym"},
        {"Vexp", "vsource.sym"},
        {"Vrect", "vsource.sym"},
        {"Vfile", "vsource_pwl.sym"},
        {"Vpwl", "vsource_pwl.sym"},
        {"Varith", "vsource_arith.sym"},
        {"Idc", "isource.sym"},
        {"Iac", "isource.sym"},
        {"Ipulse", "isource.sym"},
        {"Iexp", "isource.sym"},
        {"Irect", "isource.sym"},
        {"Ifile", "isource_pwl.sym"},
        {"Ipwl", "isource_pwl.sym"},
        {"Iarith", "isource_arith.sym"},
        {"vdd", "analogLib/vdd.sym"},
        {"vss", "analogLib/vss.sym"},
        {"R", "res.sym"},
        {"C", "capa.sym"},
        {"L", "ind.sym"},
        {"INDQ", "ind.sym"},
        {"IProbe", "IProbe.sym"},
        {"VProbe", "ngspice_probe.sym"},
        {"Port", "iopin.sym"},
    };
    const auto it = kMap.find(qucsType);
    if (it != kMap.end()) {
        return it->second;
    }
    if (!qucsType.empty() && qucsType.front() == '.') {
        if (qucsType == ".TR") {
            return "analogLib/TR.sym";
        }
        return "code_shown.sym";
    }
    if (qucsType == "TR") {
        return "analogLib/TR.sym";
    }
    if (qucsType == "INCLSCR" || qucsType == "SpiceLib") {
        return "analogLib/INCLSCR.sym";
    }
    if (qucsType == "launcher") {
        return "analogLib/launcher.sym";
    }
    if (qucsType == "Lib" || qucsType == "Sub") {
        return "qucs_blackbox.sym";
    }
    return "qucs_blackbox.sym";
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

void addProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    props.push_back({name, value});
}

void setProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    for (Property &prop : props) {
        if (prop.name == name) {
            prop.value = value;
            return;
        }
    }
    addProperty(props, name, value);
}

void replaceSingletonProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    props.erase(std::remove_if(props.begin(), props.end(),
                               [&](const Property &prop) { return prop.name == name; }),
                props.end());
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

void setProperty(std::vector<Property> &props, const std::string &name, const std::string &value);

std::optional<std::string> analogLibControllerRef(const Instance &inst);
std::string analogLibControllerCellName(const std::string &primitiveRef);
bool isAnalogLibControllerSymbol(const std::string &cellName);
void annotatePrimitiveReference(Instance &inst);

std::string modelNameFromPrimitiveRef(const std::string &ref)
{
    std::string stem = cellStem(ref);
    for (const char *suffix : {".symbol.core", ".schematic.core", ".symbol", ".schematic"}) {
        const std::size_t len = std::strlen(suffix);
        if (stem.size() > len && stem.compare(stem.size() - len, len, suffix) == 0) {
            stem.resize(stem.size() - len);
            break;
        }
    }
    return stem;
}

bool isPlaceholderLibModel(const std::string &model)
{
    return model.empty() || model == "Lib" || model == "Sub" || model == "SpiceLib";
}

std::string resolveLibCompModel(const Instance &src, const std::string &logicalType)
{
    const std::string stem = cellStem(src.cellName());
    if (logicalType != "Lib" && logicalType != "SpiceLib" && stem != "qucs_blackbox") {
        return logicalType;
    }
    for (const char *key : {"param.1", "qucs.model", "symname"}) {
        if (const std::string *model = findProperty(src.properties(), key)) {
            if (!isPlaceholderLibModel(*model)) {
                return *model;
            }
        }
    }
    if (const std::string *prim = findProperty(src.properties(), "core.primitive"); prim && !prim->empty()) {
        const std::string fromPrim = modelNameFromPrimitiveRef(*prim);
        if (!isPlaceholderLibModel(fromPrim)) {
            return fromPrim;
        }
    }
    return logicalType;
}

bool instanceNeedsQucsExportRemap(const Instance &inst)
{
    if (findProperty(inst.properties(), "qucs.type") != nullptr) {
        return true;
    }
    const std::string stem = cellStem(inst.cellName());
    return stem == "Lib" || stem == "qucs_blackbox" || stem == "Sub" || stem == "Port" || stem == "GND"
        || stem == "Vdc" || stem == "Vpulse" || stem == "INCLSCR" || stem == ".TR" || stem == "TR";
}

Instance instanceForQucsExport(const Instance &src)
{
    std::string logicalType = cellStem(src.cellName());
    if (const std::string *qt = findProperty(src.properties(), "qucs.type"); qt && !qt->empty()) {
        logicalType = *qt;
    }
    // LibComp stores the real model in param.1 (library component name). After a Qucs CORE
    // save, R/C/Vdc/GND/PDK devices often appear as cellName "Lib" — resolve before mapping.
    std::string resolvedModel = resolveLibCompModel(src, logicalType);

    std::string symbol;
    if (logicalType == ".TR" || logicalType == "TR") {
        // Xschem batch netlist needs a full .control block; analogLib/TR.sym only emits bare .tran.
        symbol = "devices/code_shown.sym";
    } else {
        symbol = mapQucsSymbol(logicalType);
    }
    if (resolvedModel == "Port") {
        // Qucs Port Type: param.1 = analog|in|out|inout → commonLib pin cells.
        // For top-level net naming in Xschem, lab_pin is what propagates lab= onto nets.
        std::string portType = "analog";
        if (const std::string *t = findProperty(src.properties(), "param.1"); t && !t->empty()) {
            portType = *t;
        }
        if (portType == "in") {
            symbol = "ipin.sym";
        } else if (portType == "out") {
            symbol = "opin.sym";
        } else {
            // Default analog Port → lab_pin so Vin/Vdd/Vout show as named nets in Xschem.
            symbol = "lab_pin.sym";
        }
    }
    bool remappedToNativePdk = false;
    if (symbol == "qucs_blackbox.sym" && !isPlaceholderLibModel(resolvedModel) && resolvedModel.front() != '.') {
        // PDK / analogLib / hierarchy cell: let Xschem resolve via CORE_PRIMITIVE index or sibling *.sym.
        symbol = resolvedModel + ".sym";
        remappedToNativePdk = true;
    }

    Transform xf = src.transform();
    if (hasQucsHistoricalSourceRotate(resolvedModel)) {
        const int mirror = findProperty(src.properties(), "mirror")
            ? std::stoi(*findProperty(src.properties(), "mirror"))
            : 0;
        const int rotateField = findProperty(src.properties(), "rotate")
            ? std::stoi(*findProperty(src.properties(), "rotate"))
            : 1;
        xf.orient = orientFromQucsSourcePlacement(mirror, rotateField);
    } else if (remappedToNativePdk && qucsLibNeedsEwToNsCompensate(resolvedModel)) {
        // Qucs IHP LibComp artwork is east–west; native Xschem/CORE PDK symbols are north–south.
        // Keep CORE rotate props for Qucs round-trip; only adjust the Xschem placement orient.
        const int mirror = findProperty(src.properties(), "mirror")
            ? std::stoi(*findProperty(src.properties(), "mirror"))
            : 0;
        const int rotateField = findProperty(src.properties(), "rotate")
            ? std::stoi(*findProperty(src.properties(), "rotate"))
            : 0;
        xf.orient = orientFromQucsLibToNativePdk(mirror, rotateField);
    }
    Instance inst(symbol, xf);

    if (const std::string *name = findProperty(src.properties(), "name")) {
        addProperty(inst.properties(), "name", *name);
    }

    // Qucs Port net name lives in param.0; Xschem pins need lab=.
    if (resolvedModel == "Port") {
        std::string lab;
        if (const std::string *p0 = findProperty(src.properties(), "param.0"); p0 && !p0->empty()) {
            lab = *p0;
        }
        if (!lab.empty() && !isAnonymousNetLabel(lab)) {
            addProperty(inst.properties(), "lab", lab);
        }
        addProperty(inst.properties(), "sig_type", "std_logic");
    }

    if (symbol == "gnd.sym" || resolvedModel == "GND") {
        if (!findProperty(inst.properties(), "lab")) {
            addProperty(inst.properties(), "lab", "GND");
        }
    }

    if (symbol == "qucs_blackbox.sym") {
        addProperty(inst.properties(), "symname", resolvedModel);
    } else if (symbol == "qucs_directive.sym" || symbol == "code_shown.sym" || symbol == "devices/code_shown.sym") {
        // Xschem code_shown / directive: spice text via @value (type=netlist_commands).
        if (resolvedModel == "INCLSCR" || resolvedModel == "SpiceLib") {
            if (const std::string *code = findProperty(src.properties(), "param.0")) {
                std::string libLine = unescapeCStyle(*code);
                if (libLine.size() >= 4 && libLine.compare(0, 4, ".LIB") == 0) {
                    libLine.replace(1, 3, "lib");
                }
                while (!libLine.empty() && (libLine.back() == '\n' || libLine.back() == '\r' || libLine.back() == ' ')) {
                    libLine.pop_back();
                }
                addProperty(inst.properties(), "value", libLine);
            }
            addProperty(inst.properties(), "only_toplevel", "true");
        } else if (!resolvedModel.empty() && (resolvedModel.front() == '.' || resolvedModel == "TR")) {
            if (resolvedModel == ".TR" || resolvedModel == "TR") {
                std::string stop = "2u";
                if (const std::string *s = findProperty(src.properties(), "param.2"); s && !s->empty()) {
                    stop = *s;
                }
                addProperty(inst.properties(), "value",
                            std::string(".control\nsave all\ntran 50n ") + stop
                                + "\nwrite test_inverter.raw\n.endc");
            } else if (const std::string *code = findProperty(src.properties(), "param.0")) {
                addProperty(inst.properties(), "value", unescapeCStyle(*code));
            } else {
                addProperty(inst.properties(), "value", resolvedModel);
            }
            addProperty(inst.properties(), "only_toplevel", "true");
        }
    } else if (symbol == "vsource.sym" || symbol == "isource.sym" || symbol == "res.sym" || symbol == "capa.sym" ||
               symbol == "ind.sym" || symbol == "vsource_pwl.sym" || symbol == "isource_pwl.sym" ||
               symbol == "vsource_arith.sym" || symbol == "isource_arith.sym" || symbol == "gnd.sym") {
        if (logicalType == "Lib") {
            // LibComp device params start at param.2 (0=lib, 1=model).
            if (const std::string *value = findProperty(src.properties(), "param.2")) {
                addProperty(inst.properties(), "value", *value);
            }
        } else if (resolvedModel == "Vpulse" || resolvedModel == "Ipulse") {
            if (std::optional<std::string> pulse = buildPulseSpiceValue(src.properties())) {
                addProperty(inst.properties(), "value", std::move(*pulse));
            }
        } else if (const std::string *value = findProperty(src.properties(), "param.0")) {
            // Vdc "1.2 V" → "1.2" so ngspice does not see unknown parameter (v).
            if (symbol == "vsource.sym" || symbol == "isource.sym" || symbol == "res.sym" || symbol == "capa.sym"
                || symbol == "ind.sym") {
                addProperty(inst.properties(), "value", normalizeSpiceDeviceValue(*value));
            } else {
                addProperty(inst.properties(), "value", *value);
            }
        } else if (const std::string *value = findProperty(src.properties(), "value")) {
            addProperty(inst.properties(), "value", *value);
        } else if (logicalType == "Vdc" || resolvedModel == "Vdc") {
            if (const std::string *name = findProperty(src.properties(), "name"); name && *name == "Vdd") {
                addProperty(inst.properties(), "value", "1.2");
            }
        }
    } else if (symbol == "INCLSCR.sym" || symbol == "analogLib/INCLSCR.sym") {
        if (const std::string *value = findProperty(src.properties(), "value")) {
            addProperty(inst.properties(), "value", *value);
        } else if (const std::string *code = findProperty(src.properties(), "param.0")) {
            std::string libLine = unescapeCStyle(*code);
            if (libLine.size() >= 4 && libLine.compare(0, 4, ".LIB") == 0) {
                libLine.replace(1, 3, "lib");
            }
            while (!libLine.empty() && (libLine.back() == '\n' || libLine.back() == '\r' || libLine.back() == ' ')) {
                libLine.pop_back();
            }
            addProperty(inst.properties(), "value", libLine);
        }
        addProperty(inst.properties(), "only_toplevel", "true");
    } else if (symbol == "TR.sym" || symbol == "analogLib/TR.sym") {
        if (const std::string *value = findProperty(src.properties(), "value")) {
            addProperty(inst.properties(), "value", *value);
        } else if (logicalType == ".TR" || logicalType == "TR" || resolvedModel == ".TR" || resolvedModel == "TR") {
            std::string stop = "2u";
            if (const std::string *s = findProperty(src.properties(), "param.2"); s && !s->empty()) {
                stop = *s;
            }
            addProperty(inst.properties(), "value",
                        std::string(".control\nsave all\ntran 50n ") + stop + "\nwrite test_inverter.raw\n.endc");
        }
        addProperty(inst.properties(), "only_toplevel", "true");
    } else if (symbol == "launcher.sym" || symbol == "analogLib/launcher.sym") {
        for (const char *key : {"descr", "tclcommand"}) {
            if (const std::string *val = findProperty(src.properties(), key)) {
                addProperty(inst.properties(), key, *val);
            }
        }
    }

    addProperty(inst.properties(), "qucs.type", logicalType);
    if (resolvedModel != logicalType) {
        addProperty(inst.properties(), "qucs.model", resolvedModel);
    }
    if (const std::string *rot = findProperty(src.properties(), "rotate")) {
        addProperty(inst.properties(), "rotate", *rot);
    }
    if (const std::string *mir = findProperty(src.properties(), "mirror")) {
        addProperty(inst.properties(), "mirror", *mir);
    }
    if (const std::optional<std::string> controllerRef = analogLibControllerRef(src)) {
        Instance rebadged(analogLibControllerCellName(*controllerRef), inst.transform());
        rebadged.properties() = inst.properties();
        setProperty(rebadged.properties(), "core.primitive", *controllerRef);
        inst = std::move(rebadged);
    }
    if (const std::string *corePrim = findProperty(src.properties(), "core.primitive")) {
        addProperty(inst.properties(), "core.primitive", *corePrim);
    } else {
        annotatePrimitiveReference(inst);
    }
    return inst;
}

void skipAttrWhitespace(const std::string &body, std::size_t &pos)
{
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
}

std::string readAttrValue(const std::string &body, std::size_t &pos)
{
    skipAttrWhitespace(body, pos);
    if (pos >= body.size()) {
        return {};
    }
    if (body[pos] == '"') {
        ++pos;
        std::string value;
        while (pos < body.size()) {
            if (body[pos] == '"') {
                ++pos;
                break;
            }
            if (body[pos] == '\\' && pos + 1 < body.size()) {
                value.push_back(body[pos + 1]);
                pos += 2;
                continue;
            }
            value.push_back(body[pos]);
            ++pos;
        }
        return value;
    }
    const std::size_t start = pos;
    while (pos < body.size() && !std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
    return body.substr(start, pos - start);
}

bool attrValueNeedsQuotes(const std::string &value)
{
    if (value.empty()) {
        return true;
    }
    for (const char ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}') {
            return true;
        }
    }
    return false;
}

std::string quoteAttrValue(const std::string &value)
{
    std::ostringstream oss;
    oss << '"';
    for (const char ch : value) {
        if (ch == '"') {
            oss << '\\' << '"';
        } else {
            oss << ch;
        }
    }
    oss << '"';
    return oss.str();
}

void parseAttrBlock(const std::string &block, std::vector<Property> &props)
{
    if (block.size() < 2 || block.front() != '{' || block.back() != '}') {
        return;
    }
    std::string body = block.substr(1, block.size() - 2);
    for (char &ch : body) {
        if (ch == '\r') {
            ch = '\n';
        }
    }
    std::size_t pos = 0;
    while (pos < body.size()) {
        skipAttrWhitespace(body, pos);
        if (pos >= body.size()) {
            break;
        }
        const std::size_t eq = body.find('=', pos);
        if (eq == std::string::npos) {
            break;
        }
        const std::string name = trim(body.substr(pos, eq - pos));
        pos = eq + 1;
        const std::string value = readAttrValue(body, pos);
        if (!name.empty()) {
            addProperty(props, name, value);
        }
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
        oss << props[i].name << '=';
        if (attrValueNeedsQuotes(props[i].value)) {
            oss << quoteAttrValue(props[i].value);
        } else {
            oss << props[i].value;
        }
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

std::optional<std::string> analogLibControllerRef(const Instance &inst)
{
    std::string logical;
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && !qt->empty()) {
        logical = *qt;
    } else {
        logical = cellStem(inst.cellName());
    }
    if (logical == "INCLSCR" || logical == "SpiceLib") {
        return "analogLib/INCLSCR.symbol.core";
    }
    if (logical == ".TR" || logical == "TR") {
        return "analogLib/TR.symbol.core";
    }
    if (logical == "launcher" || cellStem(inst.cellName()) == "launcher") {
        return "analogLib/launcher.symbol.core";
    }
    return std::nullopt;
}

std::string analogLibControllerCellName(const std::string &primitiveRef)
{
    const std::size_t slash = primitiveRef.find('/');
    const std::string stem = slash != std::string::npos ? primitiveRef.substr(slash + 1) : primitiveRef;
    const std::size_t dot = stem.find(".symbol");
    const std::string cell = dot != std::string::npos ? stem.substr(0, dot) : stem;
    const std::string lib = slash != std::string::npos ? primitiveRef.substr(0, slash) : std::string("analogLib");
    return lib + "/" + cell + ".sym";
}

bool isAnalogLibControllerSymbol(const std::string &cellName)
{
    const std::string stem = cellStem(cellName);
    return stem == "INCLSCR" || stem == "TR" || stem == ".TR" || stem == "launcher";
}

void normalizeAnalogLibController(Instance &inst)
{
    const std::optional<std::string> controllerRef = analogLibControllerRef(inst);
    if (!controllerRef) {
        return;
    }

    setProperty(inst.properties(), "core.primitive", *controllerRef);
    const std::string sym = analogLibControllerCellName(*controllerRef);
    if (inst.cellName() != sym) {
        Instance normalized(sym, inst.transform());
        normalized.properties() = inst.properties();
        inst = std::move(normalized);
    }
    if (!findProperty(inst.properties(), "qucs.type")) {
        if (*controllerRef == "analogLib/INCLSCR.symbol.core") {
            addProperty(inst.properties(), "qucs.type", "INCLSCR");
        } else if (*controllerRef == "analogLib/TR.symbol.core") {
            addProperty(inst.properties(), "qucs.type", ".TR");
        } else {
            addProperty(inst.properties(), "qucs.type", "launcher");
        }
    }
}

void annotatePrimitiveReference(Instance &inst)
{
    normalizeAnalogLibController(inst);
    if (findProperty(inst.properties(), "core.primitive") != nullptr) {
        return;
    }

    PrimitiveResolver resolver;
    resolver.loadFromEnvironment();
    const ResolvedPrimitive resolved = resolver.resolveReference(inst.cellName());
    if (!resolved.found) {
        return;
    }

    addProperty(inst.properties(), "core.primitive", resolved.logicalRef);
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
    annotatePrimitiveReference(inst);
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
    textData.height = static_cast<std::uint32_t>(editorUnitsToDbu(sizeX, g_dbuPerEditorUnit));
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
    int xschemLayer = 0;
    int count = 0;
    if (!readIntToken(raw, pos, xschemLayer) || !readIntToken(raw, pos, count) || count < 2) {
        return;
    }
    Shape::PolygonData polygon;
    polygon.layerId = layerId;
    for (int i = 0; i < count; ++i) {
        std::int64_t x = 0;
        std::int64_t y = 0;
        if (!readCoordToken(raw, pos, x)) {
            break;
        }
        if (!readCoordToken(raw, pos, y)) {
            if (polygon.points.empty()) {
                break;
            }
            // Xschem shorthand: a lone trailing coordinate closes the polygon to the first point.
            if (i == count - 1) {
                y = polygon.points.front().y;
            } else {
                y = polygon.points.back().y;
            }
        }
        polygon.points.push_back(Point{x, y});
    }
    if (polygon.points.size() < 2) {
        return;
    }
    Shape shape(polygon);
    addProperty(shape.properties(), "xschemLayer", std::to_string(xschemLayer));
    block.shapes().push_back(std::move(shape));
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
    arc.center = Point{editorUnitsToDbu(cx, g_dbuPerEditorUnit), editorUnitsToDbu(cy, g_dbuPerEditorUnit)};
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

void normalizeSubcircuitMetadata(std::vector<Property> &props)
{
    const std::string *type = findProperty(props, "type");
    if (!type || *type != "subcircuit") {
        return;
    }
    for (Property &prop : props) {
        if (prop.name != "format") {
            continue;
        }
        if (prop.value.find("@pinlist") == std::string::npos) {
            prop.value = "@name @pinlist @symname";
        }
        break;
    }
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
    } else if (const std::string *lab = findProperty(props, "label")) {
        out << " {lab=" << *lab << "}";
    } else {
        out << " {}";
    }
    out << '\n';
}

bool pointNear(const Point &a, std::int64_t x, std::int64_t y, std::int64_t tol)
{
    return std::llabs(a.x - x) <= tol && std::llabs(a.y - y) <= tol;
}

bool isAnonymousXschemNetLabel(const std::string &lab)
{
    return isAnonymousNetLabel(lab);
}

bool pointOnWireSegment(Point p, Point a, Point b, std::int64_t tol)
{
    if (pointNear(p, a.x, a.y, tol) || pointNear(p, b.x, b.y, tol)) {
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

void setWireLabProperty(std::vector<Property> &props, const std::string &lab)
{
    for (Property &prop : props) {
        if (prop.name == "lab" || prop.name == "label") {
            prop.value = lab;
            return;
        }
    }
    addProperty(props, "lab", lab);
}

// Qucs→CORE wires often lack lab=; without it xschem assigns net1/net2 and does not short
// separate gnd.sym pins to global GND — inverter floats and Vout stays 0.
void applyGndLabelsToWireProps(const Block &block, double dbuPerEditorUnit,
                               const std::vector<Shape::PathData> &wirePaths,
                               std::vector<std::vector<Property>> &mutableWireProps)
{
    const std::int64_t tol = editorUnitsToDbu(1.0, dbuPerEditorUnit);
    std::vector<Point> gndPins;
    for (const Instance &inst : block.instances()) {
        const std::string &cell = inst.cellName();
        bool isGnd = (cell == "GND" || cell == "gnd.sym" || cell == "gnd");
        if (!isGnd) {
            if (const std::string *qt = findProperty(inst.properties(), "qucs.type")) {
                isGnd = (*qt == "GND");
            }
        }
        if (!isGnd) {
            continue;
        }
        gndPins.push_back(Point{inst.transform().x, inst.transform().y});
    }
    if (gndPins.empty()) {
        return;
    }
    for (std::size_t i = 0; i < wirePaths.size() && i < mutableWireProps.size(); ++i) {
        const std::string *existing = findProperty(mutableWireProps[i], "lab");
        if (existing != nullptr && !existing->empty() && !isAnonymousXschemNetLabel(*existing)) {
            continue;
        }
        const auto &pts = wirePaths[i].points;
        if (pts.size() < 2) {
            continue;
        }
        for (const Point &g : gndPins) {
            if (pointNear(pts.front(), g.x, g.y, tol) || pointNear(pts.back(), g.x, g.y, tol)) {
                addProperty(mutableWireProps[i], "lab", "GND");
                break;
            }
        }
    }
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

int parseOptionalIntProperty(const std::vector<Property> &props, const char *name)
{
    if (const std::string *value = findProperty(props, name)) {
        try {
            return std::stoi(*value);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

void writeTextRecord(std::ostream &out, const Shape::TextData &text, const std::vector<Property> &props)
{
    const int rotate = parseOptionalIntProperty(props, "rotate");
    const int mirror = parseOptionalIntProperty(props, "mirror");
    auto qucsTextSize = [](const std::string &token) -> std::optional<double> {
        try {
            const double value = std::stod(token);
            if (value > 0.0 && value <= 2.0) {
                return std::max(0.25, value * 0.7);
            }
        } catch (...) {
        }
        return std::nullopt;
    };
    double sizeX = 0.35;
    double sizeY = 0.35;
    if (const std::string *sx = findProperty(props, "sizeX")) {
        if (const std::optional<double> mapped = qucsTextSize(*sx)) {
            sizeX = *mapped;
        }
    } else {
        sizeX = dbuToEditorUnits(static_cast<std::int64_t>(text.height), g_dbuPerEditorUnit);
        if (sizeX > 1.0) {
            sizeX = 0.35;
        }
    }
    if (const std::string *sy = findProperty(props, "sizeY")) {
        if (const std::optional<double> mapped = qucsTextSize(*sy)) {
            sizeY = *mapped;
        } else {
            sizeY = sizeX;
        }
    } else {
        sizeY = sizeX;
    }
    out << 'T' << ' ' << '{' << text.text << '}' << ' ';
    writeCoord(out, text.position.x);
    out << ' ';
    writeCoord(out, text.position.y);
    out << ' ' << rotate << ' ' << mirror << ' ' << sizeX << ' ' << sizeY << " {}\n";
}

void writePolygonRecord(std::ostream &out, const Shape::PolygonData &polygon, const std::vector<Property> &props)
{
    const int layer = findProperty(props, "xschemLayer") ? std::stoi(*findProperty(props, "xschemLayer")) : 4;
    out << 'P' << ' ' << layer << ' ' << polygon.points.size();
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

void normalizeArcAnglesForXschem(double &startAngle, double &endAngle)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    // Legacy Qucs import stored absolute end angles in radians; xschem expects degrees + span.
    if (startAngle <= kTwoPi && endAngle <= kTwoPi && endAngle >= startAngle) {
        const double startDeg = startAngle * 180.0 / kPi;
        const double spanDeg = (endAngle - startAngle) * 180.0 / kPi;
        startAngle = startDeg;
        endAngle = spanDeg;
    }
}

void writeArcRecord(std::ostream &out, const Shape::ArcData &arc, const std::vector<Property> &props)
{
    double startAngle = arc.startAngle;
    double endAngle = arc.endAngle;
    normalizeArcAnglesForXschem(startAngle, endAngle);
    const double cx = dbuToEditorUnits(arc.center.x, g_dbuPerEditorUnit);
    const double cy = dbuToEditorUnits(arc.center.y, g_dbuPerEditorUnit);
    std::vector<Property> arcProps;
    for (const Property &prop : props) {
        if (prop.name.rfind("arc.", 0) == 0 || prop.name == "geometry") {
            continue;
        }
        arcProps.push_back(prop);
    }
    out << 'A' << ' ' << arc.width << ' ' << cx << ' ' << cy << ' ' << arc.radius << ' ' << startAngle << ' '
        << endAngle << ' ' << formatAttrBlock(arcProps) << '\n';
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
    if (const std::string *cx = findProperty(props, "arc.centerX")) {
        arc.center.x = std::stoll(*cx);
    }
    if (const std::string *cy = findProperty(props, "arc.centerY")) {
        arc.center.y = std::stoll(*cy);
    }
    // Never use path.points.front() as center: compact encoding tessellates arcs and the
    // first vertex is the arc start point, not the circle center.
    normalizeArcAnglesForXschem(arc.startAngle, arc.endAngle);
    writeArcRecord(out, arc, props);
}

void writeComponentRecord(std::ostream &out, const Instance &inst)
{
    int rotate = 0;
    int mirror = 0;
    Orient orient = inst.transform().orient;

    std::string typeKey = inst.cellName();
    if (const std::string *qucsType = findProperty(inst.properties(), "qucs.type")) {
        typeKey = *qucsType;
    }
    if (hasQucsHistoricalSourceRotate(typeKey) || hasQucsHistoricalSourceRotate(inst.cellName())) {
        // Prefer the original Qucs rotate field so default sources (rotate=1) become xschem rot=0.
        const int mirrorField =
            findProperty(inst.properties(), "mirror") ? std::stoi(*findProperty(inst.properties(), "mirror")) : 0;
        if (const std::string *rotateField = findProperty(inst.properties(), "rotate")) {
            orient = orientFromQucsSourcePlacement(mirrorField, std::stoi(*rotateField));
        }
    }

    xschemFromOrient(orient, rotate, mirror);
    out << 'C' << " {" << inst.cellName() << "} ";
    writeCoord(out, inst.transform().x);
    out << ' ';
    writeCoord(out, inst.transform().y);
    out << ' ' << rotate << ' ' << mirror << ' ' << formatAttrBlock(inst.properties()) << '\n';
}

std::string toLowerAscii(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::optional<std::string> outputProbeNodeFromBlock(const Block &block)
{
    auto probeFromName = [](std::string name) -> std::optional<std::string> {
        const std::string lower = toLowerAscii(std::move(name));
        if (lower.find("out") != std::string::npos) {
            return lower;
        }
        return std::nullopt;
    };

    for (const Instance &inst : block.instances()) {
        const std::string stem = cellStem(inst.cellName());
        const std::string *qucsType = findProperty(inst.properties(), "qucs.type");
        if (stem != "Port" && (!qucsType || *qucsType != "Port")) {
            continue;
        }
        if (const std::string *lab = findProperty(inst.properties(), "lab"); lab && !lab->empty()) {
            if (std::optional<std::string> probe = probeFromName(*lab)) {
                return probe;
            }
        }
        if (const std::string *num = findProperty(inst.properties(), "param.0"); num && !num->empty()) {
            if (std::optional<std::string> probe = probeFromName(*num)) {
                return probe;
            }
        }
    }
    return std::nullopt;
}

std::optional<double> transientStopSecondsFromBlock(const Block &block)
{
    for (const Instance &inst : block.instances()) {
        std::string logical = cellStem(inst.cellName());
        if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && !qt->empty()) {
            logical = *qt;
        }
        if (logical != ".TR" && logical != "TR") {
            continue;
        }
        if (const std::string *stop = findProperty(inst.properties(), "param.2"); stop && !stop->empty()) {
            return parseSpiceTimeValue(*stop);
        }
    }
    return std::nullopt;
}

std::string instanceLogicalType(const Instance &inst)
{
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && !qt->empty()) {
        return *qt;
    }
    return inst.cellName();
}

std::optional<double> supplyVoltageFromBlock(const Block &block)
{
    for (const Instance &inst : block.instances()) {
        const std::string logical = instanceLogicalType(inst);
        if (logical == "Vdc" || inst.cellName() == "Vdc") {
            if (const std::string *value = findProperty(inst.properties(), "param.0"); value && !value->empty()) {
                try {
                    return std::stod(normalizeSpiceDeviceValue(*value));
                } catch (...) {
                    return std::nullopt;
                }
            }
        }
        if (inst.cellName() == "Lib") {
            if (const std::string *model = findProperty(inst.properties(), "param.1"); model && *model == "Vdc") {
                if (const std::string *value = findProperty(inst.properties(), "param.2"); value && !value->empty()) {
                    try {
                        return std::stod(normalizeSpiceDeviceValue(*value));
                    } catch (...) {
                        return std::nullopt;
                    }
                }
            }
        }
    }
    return std::nullopt;
}

struct DiagramAxisLimits {
    double xMin = 0.0;
    double xMax = 2e-6;
    double yMin = -0.002;
    double yMax = 1.3;
    bool yManual = true;
};

std::optional<double> parseGraphAttrDouble(const std::string &graphRecord, const char *key)
{
    const std::string needle = std::string(key) + "=";
    const std::size_t pos = graphRecord.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    char *end = nullptr;
    const double value = std::strtod(graphRecord.c_str() + pos + needle.size(), &end);
    if (end == graphRecord.c_str() + pos + needle.size()) {
        return std::nullopt;
    }
    return value;
}

DiagramAxisLimits defaultDiagramAxisLimits(const Block &block)
{
    DiagramAxisLimits limits;
    if (const std::optional<double> tstop = transientStopSecondsFromBlock(block)) {
        limits.xMax = *tstop;
    }
    double yMax = 1.0;
    if (const std::optional<double> vdd = supplyVoltageFromBlock(block)) {
        yMax = *vdd * 1.08;
    }
    limits.yMax = yMax;
    limits.yMin = -0.002 * yMax;
    limits.yManual = true;
    return limits;
}

std::optional<DiagramAxisLimits> parseRectDiagramAxes(const std::string &line)
{
    const std::size_t pos = line.find(" 00 ");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const char *cursor = line.c_str() + pos + 4;
    DiagramAxisLimits limits;
    int xAuto = 0;
    double xStep = 0.0;
    int yAuto = 0;
    double yStep = 0.0;
    if (std::sscanf(cursor, "%d %lf %lf %lf %d %lf %lf %lf", &xAuto, &limits.xMin, &xStep, &limits.xMax, &yAuto,
                    &limits.yMin, &yStep, &limits.yMax)
        < 8) {
        return std::nullopt;
    }
    limits.yManual = yAuto == 0;
    return limits;
}

std::optional<DiagramAxisLimits> qucsDiagramAxisLimitsFromContent(const CellContent &content)
{
    for (const Property &prop : content.properties()) {
        if (prop.name != "section.Diagrams") {
            continue;
        }
        const std::size_t tag = prop.value.find("<Rect ");
        if (tag == std::string::npos) {
            continue;
        }
        const std::size_t end = prop.value.find('>', tag);
        if (end == std::string::npos) {
            continue;
        }
        return parseRectDiagramAxes(prop.value.substr(tag, end - tag + 1));
    }
    return std::nullopt;
}

DiagramAxisLimits axisLimitsFromGraphRecord(const std::string &graphRecord, const Block &block)
{
    DiagramAxisLimits limits = defaultDiagramAxisLimits(block);
    if (const std::optional<double> x2 = parseGraphAttrDouble(graphRecord, "x2")) {
        limits.xMax = *x2;
    }
    if (const std::optional<double> y1 = parseGraphAttrDouble(graphRecord, "y1")) {
        limits.yMin = *y1;
    }
    if (const std::optional<double> y2 = parseGraphAttrDouble(graphRecord, "y2")) {
        limits.yMax = *y2;
    }
    limits.yManual = true;
    return limits;
}

std::string formatRectDiagramOpening(int x, int bottomY, int w, int h, const DiagramAxisLimits &limits)
{
    const double xStep = limits.xMax > 0.0 ? limits.xMax / 4.0 : 0.5e-6;
    const double ySpan = limits.yMax - limits.yMin;
    const double yStep = ySpan > 0.0 ? ySpan / 10.0 : 0.1;
    std::ostringstream rect;
    rect << std::setprecision(12);
    rect << "<Rect " << x << ' ' << bottomY << ' ' << w << ' ' << h << " 3 #c0c0c0 1 00 1 " << limits.xMin << ' '
         << xStep << ' ' << limits.xMax << ' ' << (limits.yManual ? 0 : 1) << ' ' << limits.yMin << ' ' << yStep
         << ' ' << limits.yMax << " 1 -1 0.2 1 315 0 225 1 0 0 \"\" \"\" \"\">";
    return rect.str();
}

std::optional<std::array<int, 4>> qucsDiagramRectFromContent(const CellContent &content)
{
    for (const Property &prop : content.properties()) {
        if (prop.name != "section.Diagrams") {
            continue;
        }
        const std::string tag = "<Rect ";
        const std::size_t pos = prop.value.find(tag);
        if (pos == std::string::npos) {
            continue;
        }
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        if (std::sscanf(prop.value.c_str() + pos + tag.size(), "%d %d %d %d", &x, &y, &w, &h) == 4 && w > 0
            && h > 0) {
            return std::array<int, 4>{x, y, w, h};
        }
    }
    return std::nullopt;
}

std::optional<std::string> probeFromDiagramSection(const CellContent &content)
{
    for (const Property &prop : content.properties()) {
        if (prop.name != "section.Diagrams") {
            continue;
        }
        const std::string lower = toLowerAscii(prop.value);
        std::size_t search = 0;
        while (search < lower.size()) {
            std::size_t start = std::string::npos;
            std::size_t prefixLen = 0;
            if ((search = lower.find("ngspice/tran.v(", search)) != std::string::npos) {
                start = search + 15;
                prefixLen = 15;
            } else if ((search = lower.find("ngspice/v(", search)) != std::string::npos) {
                start = search + 10;
                prefixLen = 10;
            } else {
                break;
            }
            const std::size_t end = lower.find(')', start);
            if (end == std::string::npos) {
                break;
            }
            const std::string node = lower.substr(start, end - start);
            if (node.find("out") != std::string::npos) {
                return node;
            }
            search = end + 1;
        }
    }
    return std::nullopt;
}

std::optional<std::string> buildGraphRecordFromQucsDiagram(const Block &block, const CellContent &content)
{
    const std::optional<std::array<int, 4>> rect = qucsDiagramRectFromContent(content);
    if (!rect) {
        return std::nullopt;
    }

    std::optional<std::string> probe = probeFromDiagramSection(content);
    if (!probe) {
        probe = outputProbeNodeFromBlock(block);
    }
    const std::optional<double> tstopSec = transientStopSecondsFromBlock(block);
    if (!probe || !tstopSec || *tstopSec <= 0.0) {
        return std::nullopt;
    }

    DiagramAxisLimits limits = defaultDiagramAxisLimits(block);
    if (const std::optional<DiagramAxisLimits> fromDiagram = qucsDiagramAxisLimitsFromContent(content)) {
        if (fromDiagram->yManual && fromDiagram->yMax > fromDiagram->yMin) {
            limits.yMin = fromDiagram->yMin;
            limits.yMax = fromDiagram->yMax;
            limits.yManual = true;
        }
        if (fromDiagram->xMax > fromDiagram->xMin) {
            limits.xMin = fromDiagram->xMin;
            limits.xMax = fromDiagram->xMax;
        }
    }

    // Qucs <Rect cx cy x2 y2>: (cx, cy) is the bottom-left corner; the plot extends upward by y2.
    const int gx1 = (*rect)[0];
    const int gx2 = gx1 + (*rect)[2];
    const int gy2 = (*rect)[1];
    const int gy1 = gy2 - (*rect)[3];

    std::ostringstream graph;
    graph << "B 2 " << gx1 << ' ' << gy1 << ' ' << gx2 << ' ' << gy2 << " {flags=graph y1=" << limits.yMin
          << " y2=" << limits.yMax
          << " ypos1=0 ypos2=2 divy=5 subdivy=1 unity=1 x1=0 x2=" << *tstopSec
          << " divx=5 subdivx=1 xlabmag=1.0 ylabmag=1.0 node=" << *probe
          << " color=4 dataset=-1 unitx=1 logx=0 logy=0}\n";
    return graph.str();
}

std::optional<std::string> buildTransientGraphRecord(const Block &block, double dbuPerEditorUnit)
{
    const std::optional<std::string> probe = outputProbeNodeFromBlock(block);
    const std::optional<double> tstopSec = transientStopSecondsFromBlock(block);
    if (!probe || !tstopSec || *tstopSec <= 0.0) {
        return std::nullopt;
    }

    const DiagramAxisLimits limits = defaultDiagramAxisLimits(block);

    const Box bbox = block.bbox();
    const int gx1 = static_cast<int>(std::llround(dbuToEditorUnits(bbox.urx, dbuPerEditorUnit))) + 40;
    const int gy2 = static_cast<int>(std::llround(dbuToEditorUnits(bbox.ury, dbuPerEditorUnit)));
    const int gx2 = gx1 + 800;
    const int gy1 = gy2 - 400;

    std::ostringstream graph;
    graph << "B 2 " << gx1 << ' ' << gy1 << ' ' << gx2 << ' ' << gy2 << " {flags=graph y1=" << limits.yMin
          << " y2=" << limits.yMax
          << " ypos1=0 ypos2=2 divy=5 subdivy=1 unity=1 x1=0 x2=" << *tstopSec
          << " divx=5 subdivx=1 xlabmag=1.0 ylabmag=1.0 node=" << *probe
          << " color=4 dataset=-1 unitx=1 logx=0 logy=0}\n";
    return graph.str();
}

bool instanceLooksQucsNative(const Instance &inst)
{
    if (findProperty(inst.properties(), "qucs.type") != nullptr) {
        return true;
    }
    const std::string stem = cellStem(inst.cellName());
    return stem == "INCLSCR" || stem == ".TR" || stem == "TR" || stem == "GND" || stem == "Vdc" || stem == "Vpulse"
        || stem == "Port" || stem == "Lib" || stem == "Sub";
}

bool blockLooksQucsNative(const Block &block)
{
    for (const Instance &inst : block.instances()) {
        if (instanceLooksQucsNative(inst)) {
            return true;
        }
    }
    return false;
}

} // namespace

void annotateInstanceForStorage(Instance &inst)
{
    const std::string stem = cellStem(inst.cellName());
    if (!findProperty(inst.properties(), "qucs.type")) {
        if (stem == "INCLSCR" || stem == ".TR" || stem == "TR" || stem == "GND" || stem == "Vdc" || stem == "Vpulse"
            || stem == "Port" || stem == "Lib" || stem == "launcher") {
            addProperty(inst.properties(), "qucs.type", stem == "TR" ? ".TR" : stem);
        }
    }
    annotatePrimitiveReference(inst);
}

void annotateBlockForStorage(Block &block)
{
    for (Instance &inst : block.instances()) {
        annotateInstanceForStorage(inst);
    }
}

std::optional<std::string> graphRecordForContent(const Block &block, const CellContent &content)
{
    if (std::optional<std::string> fromDiagram = buildGraphRecordFromQucsDiagram(block, content)) {
        return fromDiagram;
    }
    return buildTransientGraphRecord(block, effectiveDbuPerEditorUnit(content));
}

std::vector<std::string> collectSectionLines(const std::vector<Property> &props, const std::string &name)
{
    std::vector<std::string> lines;
    for (const Property &prop : props) {
        if (prop.name == name) {
            lines.push_back(prop.value);
        }
    }
    return lines;
}

void replaceSectionLines(std::vector<Property> &props, const std::string &name, const std::vector<std::string> &lines)
{
    props.erase(std::remove_if(props.begin(), props.end(),
                               [&](const Property &prop) { return prop.name == name; }),
                props.end());
    for (const std::string &line : lines) {
        props.push_back({name, line});
    }
}

void copyAllSectionLinesIfMissing(std::vector<Property> &dest, const std::vector<Property> &src,
                                const std::string &name)
{
    for (const Property &prop : dest) {
        if (prop.name == name) {
            return;
        }
    }
    for (const Property &prop : src) {
        if (prop.name == name) {
            dest.push_back(prop);
        }
    }
}

bool isValidGraphRecord(const std::string &record)
{
    return !record.empty() && record.find("B 2 ") != std::string::npos && record.find("flags=graph") != std::string::npos;
}

void removeInvalidGraphProperties(std::vector<Property> &props)
{
    props.erase(std::remove_if(props.begin(), props.end(),
                               [](const Property &prop) {
                                   return prop.name == "section.graph" && !isValidGraphRecord(prop.value);
                               }),
                props.end());
}

std::string portNetNameForGraph(const Instance &inst)
{
    if (const std::string *lab = findProperty(inst.properties(), "lab"); lab && !lab->empty()) {
        return *lab;
    }
    if (const std::string *num = findProperty(inst.properties(), "param.0"); num && !num->empty()) {
        return *num;
    }
    return {};
}

bool isPortInstance(const Instance &inst)
{
    if (cellStem(inst.cellName()) == "Port") {
        return true;
    }
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && *qt == "Port") {
        return true;
    }
    return false;
}

std::vector<std::string> graphRecordToDiagramLines(const std::string &graphRecord, const Block &block)
{
    const std::size_t tag = graphRecord.find("B 2 ");
    if (tag == std::string::npos) {
        return {};
    }
    int gx1 = 0;
    int gy1 = 0;
    int gx2 = 0;
    int gy2 = 0;
    if (std::sscanf(graphRecord.c_str() + tag + 4, "%d %d %d %d", &gx1, &gy1, &gx2, &gy2) != 4) {
        return {};
    }
    const int w = std::abs(gx2 - gx1);
    const int h = std::abs(gy2 - gy1);
    if (w <= 0 || h <= 0) {
        return {};
    }
    const int x = std::min(gx1, gx2);
    const int bottomY = std::max(gy1, gy2);

    std::string probe = "vout";
    if (const std::size_t nodePos = graphRecord.find("node="); nodePos != std::string::npos) {
        const std::size_t start = nodePos + 5;
        const std::size_t end = graphRecord.find_first_of(" \t}\n", start);
        if (end != std::string::npos) {
            probe = graphRecord.substr(start, end - start);
        }
    }
    std::string vin = "vin";
    std::string vout = probe;
    for (const Instance &inst : block.instances()) {
        if (!isPortInstance(inst)) {
            continue;
        }
        const std::string net = portNetNameForGraph(inst);
        if (net.empty()) {
            continue;
        }
        const std::string lower = toLowerAscii(net);
        if (lower.find("in") != std::string::npos && lower.find("out") == std::string::npos) {
            vin = lower;
        } else if (lower.find("out") != std::string::npos) {
            vout = lower;
        }
    }

    std::ostringstream rect;
    rect << formatRectDiagramOpening(x, bottomY, w, h, axisLimitsFromGraphRecord(graphRecord, block)) << '\n';
    // Match AbstractSpiceKernel::normalizeVarsNames transient prefix used in .dat.ngspice.
    rect << "\t<\"ngspice/tran.v(" << vout << ")\" #0000ff 0 3 0 0 0 0>\n";
    rect << "\t<\"ngspice/tran.v(" << vin << ")\" #ff0000 0 3 0 0 0 0>\n";
    rect << "</Rect>";
    return {rect.str()};
}

std::optional<std::string> graphRecordToDiagramRectLine(const std::string &graphRecord, const Block &block)
{
    const std::size_t tag = graphRecord.find("B 2 ");
    if (tag == std::string::npos) {
        return std::nullopt;
    }
    int gx1 = 0;
    int gy1 = 0;
    int gx2 = 0;
    int gy2 = 0;
    if (std::sscanf(graphRecord.c_str() + tag + 4, "%d %d %d %d", &gx1, &gy1, &gx2, &gy2) != 4) {
        return std::nullopt;
    }
    const int w = std::abs(gx2 - gx1);
    const int h = std::abs(gy2 - gy1);
    if (w <= 0 || h <= 0) {
        return std::nullopt;
    }
    const int x = std::min(gx1, gx2);
    const int bottomY = std::max(gy1, gy2);
    return formatRectDiagramOpening(x, bottomY, w, h, axisLimitsFromGraphRecord(graphRecord, block));
}

void updateDiagramRectFromGraphRecord(std::vector<std::string> &diagramLines, const std::string &graphRecord,
                                    const Block &block)
{
    const std::optional<std::string> rectLine = graphRecordToDiagramRectLine(graphRecord, block);
    if (!rectLine) {
        return;
    }
    bool replaced = false;
    for (std::string &line : diagramLines) {
        if (line.rfind("<Rect ", 0) == 0) {
            line = *rectLine;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        diagramLines.insert(diagramLines.begin(), *rectLine);
    }
}

void syncDualToolGraphProperties(Block &block, CellContent &content, GraphSyncDirection direction)
{
    std::vector<Property> &properties = content.properties();
    removeInvalidGraphProperties(properties);
    const std::string *graphProp = findProperty(properties, "section.graph");
    const bool hasGraph = graphProp != nullptr && isValidGraphRecord(*graphProp);
    std::vector<std::string> diagramLines = collectSectionLines(properties, "section.Diagrams");
    const bool hasDiagrams = !diagramLines.empty();

    if (!hasGraph && !hasDiagrams) {
        return;
    }

    const bool updateGraphFromDiagrams =
        direction == GraphSyncDirection::FromQucsDiagram
        || (direction == GraphSyncDirection::DeriveMissing && hasDiagrams && !hasGraph);
    const bool updateDiagramsFromGraph =
        direction == GraphSyncDirection::FromXschemGraph
        || (direction == GraphSyncDirection::DeriveMissing && hasGraph && !hasDiagrams);

    if (updateGraphFromDiagrams && hasDiagrams) {
        if (std::optional<std::string> graph = buildGraphRecordFromQucsDiagram(block, content)) {
            replaceSingletonProperty(properties, "section.graph", *graph);
        }
    }
    if (updateDiagramsFromGraph) {
        const std::string *graph = findProperty(properties, "section.graph");
        if (graph != nullptr && !graph->empty()) {
            if (hasDiagrams && direction == GraphSyncDirection::FromXschemGraph) {
                updateDiagramRectFromGraphRecord(diagramLines, *graph, block);
            } else {
                diagramLines = graphRecordToDiagramLines(*graph, block);
            }
            if (!diagramLines.empty()) {
                replaceSectionLines(properties, "section.Diagrams", diagramLines);
            }
        }
    }
}

std::vector<std::string> readRecordsFromText(const std::string &text, std::vector<std::string> &errors)
{
    (void)errors;
    std::vector<std::string> records;
    std::istringstream in(text);
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

std::vector<std::string> readRecords(const std::string &path, std::vector<std::string> &errors)
{
    std::ifstream in(path);
    if (!in) {
        errors.push_back("Cannot open Xschem file: " + path);
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return readRecordsFromText(buffer.str(), errors);
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

    g_dbuPerEditorUnit = content.dbuPerEditorUnit() > 0.0 ? content.dbuPerEditorUnit() : kXschemDbuPerEditorUnit;

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
        case 'C': {
            parseComponentRecord(raw, block, warnings);
            if (raw.find("launcher.sym") != std::string::npos || raw.find("{launcher.sym}") != std::string::npos) {
                setProperty(content.properties(), "section.launcher", raw);
            }
            break;
        }
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
            if (raw.find("flags=graph") != std::string::npos) {
                setProperty(content.properties(), "section.graph", raw);
            } else {
                parsePinRecord(raw, block, pinLayer);
            }
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
    if (content.dbuPerEditorUnit() <= 0.0) {
        content.setDbuPerEditorUnit(g_dbuPerEditorUnit);
    }
    removeInvalidGraphProperties(content.properties());
    syncDualToolGraphProperties(block, content, GraphSyncDirection::FromXschemGraph);
}

void exportRecords(std::ostream &out, const Cell &cell, const CellContent &content)
{
    CellContent working = content;
    const bool fromQucs = working.sourceInfo().format() == "qucs" || working.sourceInfo().format() == "qucs_s"
        || blockLooksQucsNative(working.block());
    if (fromQucs) {
        // Qucs diagram geometry is authoritative when opening in Xschem (fixes stale section.graph).
        syncDualToolGraphProperties(working.block(), working, GraphSyncDirection::FromQucsDiagram);
    } else {
        syncDualToolGraphProperties(working.block(), working, GraphSyncDirection::DeriveMissing);
    }

    const Block &block = working.block();
    g_dbuPerEditorUnit = effectiveDbuPerEditorUnit(working);

    PrimitiveResolver resolver;
    resolver.loadFromEnvironment();
    canonicalizeBlockPrimitives(working.block(), &resolver);
    propagateNetNames(working.block(), &resolver, g_dbuPerEditorUnit);

    writeVersionRecord(out, working.sourceInfo());
    if (working.viewType() == ViewType::Symbol && isAnalogLibControllerSymbol(cell.name())) {
        // Xschem applies type/format from K (not G) for netlist_commands.
        writeSectionRecord(out, 'G', "");
    } else if (!cell.properties().empty()) {
        out << 'G' << ' ' << formatAttrBlock(cell.properties()) << '\n';
    } else {
        writeSectionRecord(out, 'G', "");
    }

    std::vector<Property> kProps;
    for (const Property &prop : working.properties()) {
        if (prop.name.rfind(kSectionPrefix, 0) == 0 || prop.name.rfind("editor.", 0) == 0) {
            continue;
        }
        kProps.push_back(prop);
    }
    if (working.viewType() == ViewType::Symbol && isAnalogLibControllerSymbol(cell.name())) {
        replaceSingletonProperty(kProps, "type", "netlist_commands");
        replaceSingletonProperty(kProps, "template", "name=@name only_toplevel=true value=@value");
        replaceSingletonProperty(kProps, "format", "@value");
    }
    if (!kProps.empty()) {
        normalizeSubcircuitMetadata(kProps);
        out << 'K' << ' ' << formatAttrBlock(kProps) << '\n';
    }

    const std::string *sectionV = findProperty(working.properties(), std::string(kSectionPrefix) + "v");
    const std::string *sectionS = findProperty(working.properties(), std::string(kSectionPrefix) + "s");
    const std::string *sectionE = findProperty(working.properties(), std::string(kSectionPrefix) + "e");
    writeSectionRecord(out, 'V', sectionV ? *sectionV : "");
    writeSectionRecord(out, 'S', sectionS ? *sectionS : "");
    writeSectionRecord(out, 'E', sectionE ? *sectionE : "");

    std::optional<std::string> graphRecord;
    if (fromQucs) {
        if (std::optional<std::string> fromDiagram = buildGraphRecordFromQucsDiagram(block, working)) {
            graphRecord = std::move(fromDiagram);
        }
    }
    if (!graphRecord) {
        if (const std::string *storedGraph = findProperty(working.properties(), "section.graph")) {
            if (isValidGraphRecord(*storedGraph)) {
                graphRecord = *storedGraph;
            }
        }
    }

    std::vector<std::pair<Point, Point>> pinRetargets;
    if (fromQucs) {
        pinRetargets.reserve(block.instances().size() * 4);
        for (const Instance &inst : block.instances()) {
            appendQucsToXschemPinRetargets(inst, g_dbuPerEditorUnit, pinRetargets);
        }
    }

    std::vector<Shape::PathData> wirePaths;
    std::vector<std::vector<Property>> wirePropsOwned;
    for (const Shape &shape : block.shapes()) {
        if (layerPurpose(working, shapeLayerId(shape)) != LayerPurpose::Wire) {
            continue;
        }
        if (const Shape::PathData *path = shape.path()) {
            if (path->points.size() >= 2) {
                wirePaths.push_back(*path);
                wirePropsOwned.push_back(shape.properties());
            }
        }
    }
    if (!pinRetargets.empty() && !wirePaths.empty()) {
        std::vector<std::vector<Point>> polylines;
        polylines.reserve(wirePaths.size());
        for (const Shape::PathData &path : wirePaths) {
            polylines.push_back(path.points);
        }
        retargetWirePolylinesQucsToCore(polylines, pinRetargets, g_dbuPerEditorUnit);
        for (std::size_t i = 0; i < wirePaths.size(); ++i) {
            wirePaths[i].points = std::move(polylines[i]);
        }
    }
    for (std::size_t i = 0; i < wirePaths.size(); ++i) {
        writeNetRecord(out, wirePaths[i], wirePropsOwned[i]);
    }

    for (const Shape &shape : working.block().shapes()) {
        const LayerPurpose purpose = layerPurpose(working, shapeLayerId(shape));
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
            writePolygonRecord(out, *shape.polygon(), shape.properties());
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

    bool hasLauncher = false;
    bool needWaveLauncher = false;
    std::string waveRawFile;

    for (const Instance &inst : block.instances()) {
        const std::string cell = inst.cellName();
        if (cell == "launcher.sym" || cell == "analogLib/launcher.sym" || cell == "launcher"
            || cell.find("launcher") != std::string::npos) {
            hasLauncher = true;
        }
        if (fromQucs || instanceNeedsQucsExportRemap(inst)) {
            Instance exported = instanceForQucsExport(inst);
            if (const std::string *val = findProperty(exported.properties(), "value")) {
                const std::string raw = extractWriteRawFile(*val);
                if (!raw.empty()) {
                    needWaveLauncher = true;
                    waveRawFile = raw;
                }
            }
            writeComponentRecord(out, exported);
        } else {
            writeComponentRecord(out, inst);
        }
    }

    if (!hasLauncher) {
        if (const std::string *storedLauncher = findProperty(working.properties(), "section.launcher")) {
            out << *storedLauncher;
            if (storedLauncher->empty() || storedLauncher->back() != '\n') {
                out << '\n';
            }
            hasLauncher = true;
        }
    }

    // Qucs dual-tool TB uses .TR (no launcher). Restore launcher for Xschem when derivable from CORE.
    if (fromQucs && needWaveLauncher && !hasLauncher && !waveRawFile.empty()) {
        const Box bbox = block.bbox();
        double launchX = dbuToEditorUnits(bbox.urx, g_dbuPerEditorUnit) + 100.0;
        double launchY = dbuToEditorUnits(bbox.ury, g_dbuPerEditorUnit) + 60.0;
        if (const std::optional<std::array<int, 4>> rect = qucsDiagramRectFromContent(content)) {
            launchX = static_cast<double>((*rect)[0] + std::min(170, (*rect)[2]));
            launchY = static_cast<double>((*rect)[1] + std::min(60, (*rect)[3] / 3));
        }
        Transform xf{editorUnitsToDbu(launchX, g_dbuPerEditorUnit), editorUnitsToDbu(launchY, g_dbuPerEditorUnit)};
        Instance launcher("analogLib/launcher.sym", xf);
        addProperty(launcher.properties(), "name", "h5");
        addProperty(launcher.properties(), "descr", "load waves");
        addProperty(launcher.properties(), "tclcommand",
                    "xschem raw_read $netlist_dir/" + waveRawFile + " tran");
        writeComponentRecord(out, launcher);
    }

    if (graphRecord) {
        out << *graphRecord;
        if (graphRecord->empty() || graphRecord->back() != '\n') {
            out << '\n';
        }
    }
}

} // namespace core::xschem
