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
#include "net_name_propagation.h"
#include "pin_retarget.h"
#include "primitive_resolver.h"
#include "property.h"
#include "pulse_params.h"
#include "shape.h"
#include "xschem_io.h"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace core {
namespace {

std::string sanitizeSingleLineProperty(std::string text)
{
    for (char &ch : text) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return text;
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

void ensureDataSetProperties(std::vector<std::string> &propertyLines, const std::string &cellName)
{
    if (cellName.empty()) {
        return;
    }
    bool hasDataSet = false;
    bool hasDataDisplay = false;
    for (const std::string &line : propertyLines) {
        if (line.find("DataSet=") != std::string::npos) {
            hasDataSet = true;
        }
        if (line.find("DataDisplay=") != std::string::npos) {
            hasDataDisplay = true;
        }
    }
    if (!hasDataSet) {
        propertyLines.push_back("<DataSet=" + cellName + ".dat>");
    }
    if (!hasDataDisplay) {
        propertyLines.push_back("<DataDisplay=" + cellName + ".dpl>");
    }
}

std::vector<std::string> normalizeDiagramLines(const std::vector<std::string> &lines)
{
    std::vector<std::string> normalized;
    normalized.reserve(lines.size());
    for (std::string line : lines) {
        for (;;) {
            const std::size_t pos = line.find("ngspice/tran.v(");
            if (pos == std::string::npos) {
                break;
            }
            line.replace(pos, 15, "ngspice/v(");
        }
        normalized.push_back(std::move(line));
    }
    return normalized;
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
    return '"' + sanitizeSingleLineProperty(value) + '"';
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
std::string qucsComponentTypeForInstance(const Instance &inst);
bool isNativeQucsComponentType(const std::string &type);

bool shouldUseLibPrimitiveExport(const ResolvedPrimitive &resolved)
{
    const std::string stem = xschemCellStem(resolved.cellName);
    if (stem.rfind("title", 0) == 0) {
        return true;
    }
    if (isQucsSchematicDecoration(resolved.cellName)) {
        return false;
    }
    const std::string mapped = qucsComponentType(resolved.cellName);
    // Remapped (vsource→Vpulse, res→R, …) or identity-but-built-in (Vdc, INCLSCR, .TR).
    if (mapped != stem || isNativeQucsComponentType(mapped)) {
        return false;
    }
    // Use <Lib> only for cells without a built-in Qucs component mapping (PDK, hierarchy).
    return true;
}

std::string qucsComponentType(const std::string &cellName)
{
    const std::string type = xschemCellStem(cellName);
    // Fallback only when commonLib is not attached: keep native Qucs Port.
    if (type == "iopin" || type == "ipin" || type == "opin" || type == "Port") {
        return "Port";
    }
    if (type == "gnd" || type == "GND") {
        return "GND";
    }
    // Prefer Qucs-native source cell names (analogLib) over legacy commonLib vsource.
    if (type == "Vdc" || type == "Vac" || type == "Vpulse" || type == "Vexp" || type == "Vrect" ||
        type == "Vfile" || type == "Vpwl" || type == "Varith") {
        return type;
    }
    if (type == "Idc" || type == "Iac" || type == "Ipulse" || type == "Iexp" || type == "Irect" ||
        type == "Ifile" || type == "Ipwl" || type == "Iarith") {
        return type;
    }
    if (type == "INCLSCR" || type == "SpiceLib" || type == ".TR" || type == "TR" || type == ".DC" || type == ".SW" ||
        type == ".AC" || type == ".SP" || type == ".HB") {
        return type == "TR" ? ".TR" : type;
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
    // lab_wire is Xschem-only routing aid; controllers use analogLib (INCLSCR, .TR, launcher).
    if (type == "lab_wire") {
        return "Sub";
    }
    if (type.rfind("title", 0) == 0) {
        return "Sub";
    }
    return type;
}

// Prefer explicit qucs.type on the instance (analogLib controllers in CORE).
std::string qucsComponentTypeForInstance(const Instance &inst)
{
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && !qt->empty()) {
        if (*qt == "TR") {
            return ".TR";
        }
        return *qt;
    }
    // commonLib vsource: DC-like value → Vdc, otherwise Vpulse.
    const std::string stem = xschemCellStem(inst.cellName());
    if (stem == "vsource") {
        if (const std::string *value = findProperty(inst.properties(), "value")) {
            const std::string &v = *value;
            if (v.find("PULSE") != std::string::npos || v.find("pulse") != std::string::npos ||
                v.find("SIN") != std::string::npos || v.find("EXP") != std::string::npos) {
                return "Vpulse";
            }
            return "Vdc";
        }
    }
    return qucsComponentType(inst.cellName());
}

bool isNativeQucsComponentType(const std::string &type)
{
    return type == "Port" || type == "GND" || type == "R" || type == "C" || type == "L" || type == "INDQ"
        || type == "Vdc" || type == "Vac" || type == "Vpulse" || type == "Vexp" || type == "Vrect"
        || type == "Vfile" || type == "Vpwl" || type == "Varith" || type == "Idc" || type == "Iac"
        || type == "Ipulse" || type == "Iexp" || type == "Irect" || type == "Ifile" || type == "Ipwl"
        || type == "Iarith" || type == "IProbe" || type == "VProbe" || type == "INCLSCR" || type == "SpiceLib"
        || type == ".TR" || type == "TR" || type == ".DC" || type == ".SW" || type == ".AC" || type == ".SP"
        || type == ".HB" || type == "vdd" || type == "vss" || type == "Sub";
}

// Collect param.N values already stored on the instance.
std::vector<std::string> collectParamValues(const std::vector<Property> &props)
{
    std::vector<std::string> values;
    for (std::size_t i = 0;; ++i) {
        const std::string *prop = findProperty(props, "param." + std::to_string(i));
        if (!prop) {
            break;
        }
        values.push_back(*prop);
    }
    return values;
}

// When CORE came from Xschem (value=…) rather than Qucs (param.N), synthesize Qucs props.
namespace {

std::vector<std::pair<std::string, std::string>> synthesizePulseParamsFromSpice(const std::vector<std::string> &toks)
{
    if (toks.size() < 3) {
        return {};
    }
    const std::string u1 = toks[0];
    const std::string u2 = toks[1];
    const std::string t1 = toks[2];
    const std::string tr = toks.size() > 3 ? toks[3] : std::string{};
    const std::string tf = toks.size() > 4 ? toks[4] : std::string{};
    const std::string pw = toks.size() > 5 ? toks[5] : std::string{};
    const std::string t2 =
        !pw.empty() ? "{" + t1 + "+" + tr + "+" + tf + "+" + pw + "}" : t1;
    std::vector<std::pair<std::string, std::string>> out = {
        {u1, "1"}, {u2, "1"}, {t1, "1"}, {t2, "1"}, {tr, "1"}, {tf, "1"}};
    if (toks.size() > 6) {
        out.push_back({toks[6], "0"});
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> synthesizePulseParamsFromStored(
    const std::vector<std::string> &existing, const std::vector<Property> &props)
{
    const std::string u1 = existing[0];
    const std::string u2 = existing[1];
    const std::string t1 = existing[2];
    const std::string perOrEnd = existing[3];
    const std::string tr = existing[4];
    const std::string tf = existing[5];
    std::string pw;
    std::string per;
    if (const std::string *value = findProperty(props, "value")) {
        const std::string lower = *value;
        if (lower.find("PULSE") != std::string::npos || lower.find("pulse") != std::string::npos) {
            const std::vector<std::string> toks = tokenizePulseBody(*value);
            if (toks.size() > 5) {
                pw = toks[5];
            }
            if (toks.size() > 6) {
                per = toks[6];
            }
        }
    }
    if (existing.size() >= 7 && per.empty()) {
        per = existing[6];
    }
    // Legacy CORE round-trip stored SPICE PER in Qucs param.3 (T2) instead of end time.
    if (per.empty() && !looksLikeQucsEndTimeExpr(perOrEnd)) {
        per = perOrEnd;
    }
    if (pw.empty()) {
        if (looksLikeQucsEndTimeExpr(perOrEnd)) {
            pw = "{" + perOrEnd + "-(" + t1 + ")-(" + tr + ")-(" + tf + ")}";
        } else if (!per.empty()) {
            pw = pulseWidthFromLegacyPeriod(per, t1);
        }
    }
    std::string t2;
    if (!pw.empty()) {
        if (!per.empty() && !looksLikeQucsEndTimeExpr(perOrEnd)) {
            t2 = "{" + t1 + "+" + tr + "+" + tf + "+" + per + "-2*(" + t1 + ")}";
        } else {
            t2 = "{" + t1 + "+" + tr + "+" + tf + "+" + pw + "}";
        }
    } else {
        t2 = perOrEnd;
    }
    std::vector<std::pair<std::string, std::string>> out = {
        {u1, "1"}, {u2, "1"}, {t1, "1"}, {t2, "1"}, {tr, "1"}, {tf, "1"}};
    if (!per.empty()) {
        out.push_back({per, "0"});
    }
    return out;
}

} // namespace

std::vector<std::pair<std::string, std::string>> synthesizeQucsParams(const std::string &type,
                                                                      const std::vector<Property> &props)
{
    std::vector<std::pair<std::string, std::string>> out; // value, visible
    const std::string *value = findProperty(props, "value");
    const std::string val = value ? *value : "";

    if (type == "Vpulse" || type == "Ipulse") {
        if (val.find("PULSE") != std::string::npos || val.find("pulse") != std::string::npos) {
            return synthesizePulseParamsFromSpice(tokenizePulseBody(val));
        }
    }

    if (type == ".TR") {
        // Prefer parsing NGSPICE/.control blocks over stale param.N left from Xschem code_shown.
        std::string body = val;
        for (char &ch : body) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        if (body.find("tran") != std::string::npos) {
            std::string start = "0";
            std::string stop = "2u";
            std::string points = "41";
            std::istringstream iss(body);
            std::string tok;
            std::vector<std::string> toks;
            while (iss >> tok) {
                toks.push_back(tok);
            }
            std::size_t tranIdx = toks.size();
            for (std::size_t j = 0; j < toks.size(); ++j) {
                if (toks[j] == ".tran" || toks[j] == "tran") {
                    tranIdx = j;
                    break;
                }
            }
            if (tranIdx + 2 < toks.size()) {
                stop = toks[tranIdx + 2];
            }
            if (tranIdx + 3 < toks.size()) {
                const std::string &maybeStart = toks[tranIdx + 3];
                if (!maybeStart.empty() && (std::isdigit(static_cast<unsigned char>(maybeStart[0])) || maybeStart[0] == '.')) {
                    start = maybeStart;
                }
            }
            return {{"lin", "1"}, {start, "1"}, {stop, "1"}, {points, "0"}};
        }
    }

    const auto existing = collectParamValues(props);
    if (!existing.empty()) {
        if ((type == "Vpulse" || type == "Ipulse") && existing.size() >= 6) {
            return synthesizePulseParamsFromStored(existing, props);
        }
        for (const std::string &v : existing) {
            out.push_back({v, "1"});
        }
        // Preserve visibility flags when present.
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (const std::string *vis = findProperty(props, "visible." + std::to_string(i))) {
                out[i].second = *vis;
            }
        }
        return out;
    }

    if (type == "Vdc" || type == "Idc" || type == "R" || type == "C" || type == "L") {
        out.push_back({val.empty() ? "1" : val, "1"});
        return out;
    }
    if (type == "Vpulse" || type == "Ipulse") {
        return synthesizePulseParamsFromSpice(tokenizePulseBody(val));
    }
    if (type == "INCLSCR") {
        std::string script = val.empty() ? ".LIB cornerMOSlv.lib mos_tt" : val;
        out.push_back({sanitizeSingleLineProperty(std::move(script)), "1"});
        out.push_back({"", "0"});
        out.push_back({"", "0"});
        return out;
    }
    if (!val.empty()) {
        out.push_back({val, "1"});
    }
    return out;
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

    // PDK symbols netlist via QUCS_PRIMITIVE_LIB (.lib). Design hierarchies keep techLibrary.
    std::string libName;
    const bool pdkCell = compName.rfind("sg13_", 0) == 0;
    if (!qucsLibrary.empty() && (pdkCell || resolved.techLibrary == "sg13g2_pr")) {
        libName = qucsLibrary;
    } else if (!resolved.techLibrary.empty()) {
        libName = resolved.techLibrary;
    } else {
        libName = qucsLibrary;
    }

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
    // Xschem stores analogLib controllers (INCLSCR, .TR, …) as code_shown.sym in CORE; keep Qucs-native export.
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && !qt->empty()) {
        std::string logical = *qt;
        if (logical == "TR") {
            logical = ".TR";
        }
        if (isNativeQucsComponentType(logical)) {
            return std::nullopt;
        }
    }

    std::string ref = inst.cellName();
    if (const std::string *coreRef = findProperty(inst.properties(), "core.primitive")) {
        ref = *coreRef;
    }

    const ResolvedPrimitive resolved = resolver.resolveReference(ref);
    if (!resolved.found) {
        // Xschem PDK devices (sg13_lv_nmos, …) netlist via Qucs .lib even without *.symbol.core index.
        const std::string model = pinRetargetModelName(inst);
        if (!qucsLibrary.empty() && model.rfind("sg13_", 0) == 0) {
            ResolvedPrimitive synthetic;
            synthetic.found = true;
            synthetic.cellName = model;
            const std::size_t slash = ref.find('/');
            synthetic.techLibrary = slash != std::string::npos ? ref.substr(0, slash) : qucsLibrary;
            return formatLibPrimitiveLine(inst, dbuPerEditorUnit, synthetic, qucsLibrary);
        }
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

    const std::string type = qucsComponentTypeForInstance(inst);
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
    int mirror = 0;
    int rotate = 0;
    if (hasQucsHistoricalSourceRotate(inst.cellName()) || hasQucsHistoricalSourceRotate(type)) {
        if (findProperty(inst.properties(), "mirror") && findProperty(inst.properties(), "rotate")) {
            mirror = std::stoi(*findProperty(inst.properties(), "mirror"));
            rotate = std::stoi(*findProperty(inst.properties(), "rotate"));
        } else {
            orientToQucsSourcePlacement(inst.transform().orient, mirror, rotate);
        }
    } else {
        mirror = findProperty(inst.properties(), "mirror") ? std::stoi(*findProperty(inst.properties(), "mirror"))
                                                           : orientToQucsMirror(inst.transform().orient);
        rotate = findProperty(inst.properties(), "rotate") ? std::stoi(*findProperty(inst.properties(), "rotate"))
                                                           : orientToQucsRotate(inst.transform().orient);
    }

    if (type == "Port") {
        std::string portNum = findProperty(inst.properties(), "lab") ? *findProperty(inst.properties(), "lab")
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

    if (type == "Lib") {
        std::string libName;
        std::string compName;
        if (const std::string *p0 = findProperty(inst.properties(), "param.0")) {
            libName = *p0;
        }
        if (const std::string *p1 = findProperty(inst.properties(), "param.1")) {
            compName = *p1;
        }
        if (compName.empty()) {
            if (const std::string *model = findProperty(inst.properties(), "qucs.model")) {
                compName = *model;
            } else {
                compName = xschemCellStem(inst.cellName());
            }
        }
        if (libName.empty()) {
            if (const std::string *corePrim = findProperty(inst.properties(), "core.primitive")) {
                const std::string &path = *corePrim;
                const std::size_t slash = path.find('/');
                if (slash != std::string::npos) {
                    libName = path.substr(0, slash);
                }
            }
        }
        if (!compName.empty()) {
            ResolvedPrimitive synthetic;
            synthetic.found = true;
            synthetic.cellName = compName;
            synthetic.techLibrary = libName;
            if (const std::optional<std::string> line =
                    formatLibPrimitiveLine(inst, dbuPerEditorUnit, synthetic, qucsLibrary)) {
                return *line;
            }
        }
    }

    std::ostringstream oss;
    oss << '<' << type << ' ' << instName << ' ' << active << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().y, dbuPerEditorUnit)))
        << ' ' << textX << ' ' << textY << ' ' << mirror << ' ' << rotate;

    const auto params = synthesizeQucsParams(type, inst.properties());
    for (const auto &pv : params) {
        oss << ' ' << quote(pv.first) << ' ' << pv.second;
    }
    oss << '>';
    return oss.str();
}

void appendWireSegment(std::vector<std::string> &lines, const Point &a, const Point &b, const std::string &label,
                       std::int64_t labelX, std::int64_t labelY, std::int64_t dist, double dbuPerEditorUnit,
                       const std::string &nodeSet = "")
{
    // Qucs draws a magenta leader from the wire to (labelX,labelY). Defaulting both to 0
    // parks every label at the origin and creates a "spider" across the schematic.
    std::int64_t lx = labelX;
    std::int64_t ly = labelY;
    if (!label.empty() && lx == 0 && ly == 0) {
        lx = (a.x + b.x) / 2 + static_cast<std::int64_t>(std::llround(10.0 * (dbuPerEditorUnit > 0.0 ? dbuPerEditorUnit : 1.0)));
        ly = (a.y + b.y) / 2 - static_cast<std::int64_t>(std::llround(10.0 * (dbuPerEditorUnit > 0.0 ? dbuPerEditorUnit : 1.0)));
    }
    std::ostringstream oss;
    oss << '<' << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(a.x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(a.y, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(b.x, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(b.y, dbuPerEditorUnit))) << ' ' << quote(label)
        << ' ' << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(lx, dbuPerEditorUnit))) << ' '
        << static_cast<std::int64_t>(std::llround(dbuToEditorUnits(ly, dbuPerEditorUnit))) << ' ' << dist << ' '
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

bool isGroundNetLabel(const std::string &label)
{
    return label == "GND" || label == "gnd" || label == "0";
}

// Qucs maps only lowercase "gnd" to SPICE node 0 (see Ground::giveNodeNames and
// vPulse::spice_netlist). CORE/Xschem often labels ground wires "GND".
std::string qucsWireLabel(const std::string &label)
{
    if (isGroundNetLabel(label)) {
        return "gnd";
    }
    return label;
}

bool pointsNear(const Point &a, const Point &b, std::int64_t tolDbu)
{
    return std::llabs(a.x - b.x) <= tolDbu && std::llabs(a.y - b.y) <= tolDbu;
}

std::optional<std::string> namedNetAtPoint(const Block &block, const Point &pt, double dbuPerEditorUnit)
{
    const double tolD = std::max(1.0, static_cast<double>(editorUnitsToDbu(2.0, dbuPerEditorUnit)));
    const std::int64_t tol = static_cast<std::int64_t>(std::llround(tolD));
    for (const Net &net : block.nets()) {
        if (net.name().empty() || net.name().rfind("N$", 0) == 0 || isAnonymousNetLabel(net.name())) {
            continue;
        }
        for (const Term &term : net.terms()) {
            if (pointsNear(term.position(), pt, tol)) {
                return net.name();
            }
        }
    }
    return std::nullopt;
}

std::string wireLabelForSegment(const Block &block, const Point &a, const Point &b,
                                const std::vector<Property> &props, double dbuPerEditorUnit)
{
    std::string label = qucsWireLabel(wireLabelFromProps(props));
    if (!label.empty()) {
        return label;
    }
    if (const std::optional<std::string> net = namedNetAtPoint(block, a, dbuPerEditorUnit)) {
        return qucsWireLabel(*net);
    }
    if (const std::optional<std::string> net = namedNetAtPoint(block, b, dbuPerEditorUnit)) {
        return qucsWireLabel(*net);
    }
    return {};
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
        const std::string label = wireLabelForSegment(block, pts[0], pts[1], props, dbuPerEditorUnit);
        const std::int64_t labelX = findProperty(props, "labelX") ? std::stoll(*findProperty(props, "labelX")) : 0;
        const std::int64_t labelY = findProperty(props, "labelY") ? std::stoll(*findProperty(props, "labelY")) : 0;
        const std::int64_t dist = findProperty(props, "dist") ? std::stoll(*findProperty(props, "dist")) : 0;
        const std::string nodeSet = findProperty(props, "nodeSet") ? *findProperty(props, "nodeSet") : "";
        appendWireSegment(lines, pts[0], pts[1], label, labelX, labelY, dist, dbuPerEditorUnit, nodeSet);
    }
    if (!lines.empty()) {
        return lines;
    }

    std::vector<std::vector<Point>> netPolylines;
    struct NetWireMeta {
        std::string label;
        std::int64_t labelX = 0;
        std::int64_t labelY = 0;
        std::int64_t dist = 0;
        std::string nodeSet;
    };
    std::vector<NetWireMeta> netMeta;
    for (const Net &net : block.nets()) {
        if (net.terms().empty()) {
            continue;
        }
        const std::string label =
            net.name().rfind("N$", 0) == 0 ? "" : qucsWireLabel(net.name());
        for (std::size_t i = 1; i < net.terms().size(); ++i) {
            netPolylines.push_back({net.terms()[i - 1].position(), net.terms()[i].position()});
            netMeta.push_back({i == 1 ? label : "", 0, 0, 0, ""});
        }
    }
    if (!netPolylines.empty() && !pinRetargets.empty()) {
        retargetWirePolylinesQucsToCore(netPolylines, pinRetargets, dbuPerEditorUnit);
    }
    for (std::size_t i = 0; i < netPolylines.size(); ++i) {
        const auto &pts = netPolylines[i];
        if (pts.size() < 2) {
            continue;
        }
        const NetWireMeta &meta = netMeta[i];
        connectPoints(lines, pts[0], pts[1], meta.label, dbuPerEditorUnit);
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
    int nextPinNumber = 1;

    auto emitPort = [&](std::int64_t cx, std::int64_t cy, std::vector<Property> props) {
        // Qucs analyseLine sizes Ports by pin number; missing pinnumber → all pins collapse to #1.
        if (!findProperty(props, "pinnumber")) {
            props.push_back({"pinnumber", std::to_string(nextPinNumber)});
        }
        ++nextPinNumber;
        appendQucsPort(lines, cx, cy, props);
        ++portSymCount;
    };

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
                emitPort(cx, cy, shape.properties());
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
                emitPort(toQucsCoord(term.position().x, dbuPerEditorUnit),
                         toQucsCoord(term.position().y, dbuPerEditorUnit), {Property{"lab", net.name()}});
            }
        }
    }

    return lines;
}

} // namespace

bool isLauncherInstance(const Instance &inst)
{
    if (const std::string *qt = findProperty(inst.properties(), "qucs.type"); qt && *qt == "launcher") {
        return true;
    }
    return xschemCellStem(inst.cellName()) == "launcher";
}

void appendLauncherPainting(std::vector<std::string> &lines, const Instance &inst, double dbuPerEditorUnit)
{
    const std::string name =
        findProperty(inst.properties(), "name") ? *findProperty(inst.properties(), "name") : "launcher";
    const std::string descr =
        findProperty(inst.properties(), "descr") ? *findProperty(inst.properties(), "descr") : "load waves";
    const std::int64_t x =
        static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().x, dbuPerEditorUnit)));
    const std::int64_t y =
        static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().y, dbuPerEditorUnit)));
    std::ostringstream oss;
    oss << "<Text " << x << ' ' << y << " 10 #000080 0 \"[" << name << "] " << descr << "\">";
    lines.push_back(oss.str());
}

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
    return type == "lab_wire";
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

    CellContent working = *content;
    core::xschem::syncDualToolGraphProperties(working.block(), working, core::xschem::GraphSyncDirection::DeriveMissing);

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

    canonicalizeBlockPrimitives(working.block(), &resolver);
    const double dbuPerEditorUnit = effectiveDbuPerEditorUnit(working);
    propagateNetNames(working.block(), &resolver, dbuPerEditorUnit);
    const CellContent &exportContent = working;
    std::string effectiveQucsLib = m_options.qucsPrimitiveLib;
    if (effectiveQucsLib.empty()) {
        effectiveQucsLib = "IHP_PDK_nonlinear_components";
    }

    const auto headers = collectProperties(exportContent.properties(), "schematic.header");
    if (!headers.empty()) {
        out << headers.front() << '\n';
    } else if (!exportContent.sourceInfo().toolVersion().empty()) {
        out << "<Qucs Schematic " << exportContent.sourceInfo().toolVersion() << ">\n";
    } else {
        out << "<Qucs Schematic " << m_options.qucsVersion << ">\n";
    }

    auto propertyLines = collectProperties(exportContent.properties(), "schematic.view");
    if (propertyLines.empty()) {
        propertyLines = {"<View=0,0,800,600,1,0,0>", "<Grid=10,10,1>"};
    }
    for (std::string &line : propertyLines) {
        line = normalizeQucsViewPropertyLine(line, cellName);
    }
    ensureDataSetProperties(propertyLines, cellName);
    writeSection(out, "Properties", propertyLines);

    for (const char *sectionName : {"Symbol", "Components", "Wires", "Diagrams", "Paintings"}) {
        if (std::string(sectionName) == "Components") {
            std::vector<std::string> lines;
            for (const Instance &inst : exportContent.block().instances()) {
                if (isQucsSchematicDecoration(inst.cellName())) {
                    continue;
                }
                if (isLauncherInstance(inst)) {
                    continue;
                }
                lines.push_back(formatComponentLine(inst, dbuPerEditorUnit, &resolver, effectiveQucsLib));
            }
            writeSection(out, "Components", lines);
            continue;
        }
        if (std::string(sectionName) == "Diagrams") {
            writeSection(out, "Diagrams",
                         normalizeDiagramLines(collectProperties(exportContent.properties(),
                                                                 std::string("section.") + sectionName)));
            continue;
        }
        if (std::string(sectionName) == "Paintings") {
            std::vector<std::string> lines =
                collectProperties(exportContent.properties(), std::string("section.") + sectionName);
            for (const Instance &inst : exportContent.block().instances()) {
                if (isLauncherInstance(inst)) {
                    appendLauncherPainting(lines, inst, dbuPerEditorUnit);
                }
            }
            writeSection(out, "Paintings", lines);
            continue;
        }
        if (std::string(sectionName) == "Wires") {
            const std::string sourceFormat =
                exportContent.sourceInfo().format().empty() ? "xschem" : exportContent.sourceInfo().format();
            writeSection(out, "Wires",
                         exportBlockWiresAsLines(exportContent.block(), exportContent.layers(), dbuPerEditorUnit,
                                                 sourceFormat));
            continue;
        }
        writeSection(out, sectionName,
                     collectProperties(exportContent.properties(), std::string("section.") + sectionName));
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

namespace {

bool corePrimitiveLibsConfigured()
{
    if (const char *netlistLibPins = std::getenv("LIBMAN_NETLIST_LIBPINS");
        netlistLibPins != nullptr && *netlistLibPins != '\0' && std::strcmp(netlistLibPins, "0") != 0) {
        return false;
    }
    if (const char *listFile = std::getenv("CORE_PRIMITIVE_LIBS_FILE"); listFile != nullptr && *listFile != '\0') {
        return true;
    }
    if (const char *libs = std::getenv("CORE_PRIMITIVE_LIBS"); libs != nullptr && *libs != '\0') {
        return true;
    }
    if (const char *single = std::getenv("CORE_PRIMITIVE_LIB"); single != nullptr && *single != '\0') {
        return true;
    }
    return false;
}

} // namespace

std::vector<std::string> QucsExporter::exportBlockWiresAsLines(const Block &block,
                                                               const std::vector<LayerSpec> &layers,
                                                               double dbuPerEditorUnit,
                                                               const std::string &sourceFormat,
                                                               std::int64_t coordDivisor) const
{
    std::vector<std::pair<Point, Point>> pinRetargets;
    pinRetargets.reserve(block.instances().size() * 4);
    const bool netlistLibPins = [] {
        if (const char *v = std::getenv("LIBMAN_NETLIST_LIBPINS"); v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0) {
            return true;
        }
        return false;
    }();
    const std::int64_t divisor = coordDivisor > 0 ? coordDivisor : 1;
    for (const Instance &inst : block.instances()) {
        if (netlistLibPins) {
            continue;
        }
        if (sourceFormat == "qucs") {
            appendQucsToXschemPinRetargets(inst, dbuPerEditorUnit, pinRetargets);
        } else if (!corePrimitiveLibsConfigured()) {
            // No CORE primitive symbols in Qucs → LibComp uses legacy .lib pin artwork.
            appendXschemToQucsPinRetargets(inst, dbuPerEditorUnit, pinRetargets);
        }
    }
    return formatWiresFromBlock(block, layers, dbuPerEditorUnit, pinRetargets);
}

} // namespace core
