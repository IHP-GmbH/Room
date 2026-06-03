#include "qucs_exporter.h"

#include "cell_content.h"
#include "enums.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cdb {
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

int orientToQucsRotate(Orient orient)
{
    switch (orient) {
    case Orient::R90:
    case Orient::MX90:
        return 1;
    case Orient::R180:
    case Orient::MY:
        return 2;
    case Orient::R270:
    case Orient::MY90:
        return 3;
    default:
        return 0;
    }
}

int orientToQucsMirror(Orient orient)
{
    switch (orient) {
    case Orient::MX:
    case Orient::MX90:
    case Orient::MY:
    case Orient::MY90:
        return 1;
    default:
        return 0;
    }
}

std::string quote(const std::string &value)
{
    return '"' + value + '"';
}

std::string formatComponentLine(const Instance &inst)
{
    const std::string type = findProperty(inst.properties(), "qucs.type") ? *findProperty(inst.properties(), "qucs.type")
                                                                          : inst.cellName();
    const std::string instName = findProperty(inst.properties(), "qucs.instName") ? *findProperty(inst.properties(), "qucs.instName")
                                                                                  : inst.cellName();
    const int active = findProperty(inst.properties(), "qucs.active") ? std::stoi(*findProperty(inst.properties(), "qucs.active")) : 1;
    const std::int64_t textX = findProperty(inst.properties(), "qucs.textX") ? std::stoll(*findProperty(inst.properties(), "qucs.textX")) : 0;
    const std::int64_t textY = findProperty(inst.properties(), "qucs.textY") ? std::stoll(*findProperty(inst.properties(), "qucs.textY")) : 0;
    const int mirror = findProperty(inst.properties(), "qucs.mirror") ? std::stoi(*findProperty(inst.properties(), "qucs.mirror"))
                                                                      : orientToQucsMirror(inst.transform().orient);
    const int rotate = findProperty(inst.properties(), "qucs.rotate") ? std::stoi(*findProperty(inst.properties(), "qucs.rotate"))
                                                                      : orientToQucsRotate(inst.transform().orient);

    std::ostringstream oss;
    oss << '<' << type << ' ' << instName << ' ' << active << ' ' << inst.transform().x << ' ' << inst.transform().y
        << ' ' << textX << ' ' << textY << ' ' << mirror << ' ' << rotate;

    for (std::size_t i = 0;; ++i) {
        const std::string propKey = "qucs.prop." + std::to_string(i);
        const std::string visKey = "qucs.vis." + std::to_string(i);
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
                       std::int64_t labelX, std::int64_t labelY, std::int64_t dist)
{
    std::ostringstream oss;
    oss << '<' << a.x << ' ' << a.y << ' ' << b.x << ' ' << b.y << ' ' << quote(label) << ' ' << labelX << ' '
        << labelY << ' ' << dist << " \"\">";
    lines.push_back(oss.str());
}

void connectPoints(std::vector<std::string> &lines, const Point &a, const Point &b, const std::string &label)
{
    if (a.x == b.x && a.y == b.y) {
        return;
    }
    if (a.x == b.x || a.y == b.y) {
        appendWireSegment(lines, a, b, label, 0, 0, 0);
        return;
    }
    appendWireSegment(lines, a, {b.x, a.y}, label, 0, 0, 0);
    appendWireSegment(lines, {b.x, a.y}, b, "", 0, 0, 0);
}

std::vector<std::string> formatWiresFromNets(const Block &block, const std::vector<Property> &contentProps)
{
    const auto stored = collectProperties(contentProps, "qucs.wire");
    if (!stored.empty()) {
        std::vector<std::string> lines;
        for (const std::string &wire : stored) {
            lines.push_back('<' + wire + '>');
        }
        return lines;
    }

    std::vector<std::string> lines;
    for (const Net &net : block.nets()) {
        if (net.terms().empty()) {
            continue;
        }
        const std::string label = net.name().rfind("N$", 0) == 0 ? "" : net.name();
        for (std::size_t i = 1; i < net.terms().size(); ++i) {
            connectPoints(lines, net.terms()[i - 1].position(), net.terms()[i].position(), i == 1 ? label : "");
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

} // namespace

QucsExporter::QucsExporter() = default;
QucsExporter::QucsExporter(const Options &options) : options_(options) {}

void QucsExporter::exportCell(const Database &db, const std::string &cellName, const std::string &schPath) const
{
    warnings_.clear();
    errors_.clear();

    const Cell *cell = db.lib().findCell(cellName);
    if (!cell) {
        errors_.push_back("Cell not found: " + cellName);
        return;
    }

    const CellContent *content = cell->findContent(ViewType::Schematic);
    if (!content) {
        errors_.push_back("No schematic view for cell: " + cellName);
        return;
    }

    std::ofstream out(schPath);
    if (!out) {
        errors_.push_back("Cannot open file for writing: " + schPath);
        return;
    }

    const auto headers = collectProperties(content->properties(), "qucs.header");
    if (!headers.empty()) {
        out << headers.front() << '\n';
    } else {
        out << "<Qucs Schematic " << options_.qucsVersion << ">\n";
    }

    auto propertyLines = collectProperties(content->properties(), "qucs.properties");
    if (propertyLines.empty()) {
        propertyLines = {"<View=0,0,800,600,1,0,0>", "<Grid=10,10,1>"};
    }
    writeSection(out, "Properties", propertyLines);

    for (const char *sectionName : {"Symbol", "Components", "Wires", "Diagrams", "Paintings"}) {
        if (std::string(sectionName) == "Components") {
            std::vector<std::string> lines;
            for (const Instance &inst : content->block().instances()) {
                lines.push_back(formatComponentLine(inst));
            }
            writeSection(out, "Components", lines);
            continue;
        }
        if (std::string(sectionName) == "Wires") {
            writeSection(out, "Wires", formatWiresFromNets(content->block(), content->properties()));
            continue;
        }
        writeSection(out, sectionName, collectProperties(content->properties(), std::string("qucs.section.") + sectionName));
    }
}

} // namespace cdb
