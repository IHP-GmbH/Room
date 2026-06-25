/*!****************************************************************************************
 * \file qucs_exporter.cpp
 * \brief Qucs .sch exporter from CORE schematic views.
 *****************************************************************************************/

#include "qucs_exporter.h"

#include "cell_content.h"
#include "coord_scale.h"
#include "enums.h"
#include "layer_spec.h"
#include "shape.h"

#include <cmath>
#include <fstream>
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

std::string qucsComponentType(const std::string &cellName)
{
    std::string type = cellName;
    const auto slash = type.find_last_of("/\\");
    if (slash != std::string::npos) {
        type = type.substr(slash + 1);
    }
    return type;
}

std::string formatComponentLine(const Instance &inst, double dbuPerEditorUnit)
{
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
                                              double dbuPerEditorUnit)
{
    std::vector<std::string> lines;
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
        const std::string label = findProperty(shape.properties(), "label") ? *findProperty(shape.properties(), "label") : "";
        const std::int64_t labelX = findProperty(shape.properties(), "labelX") ? std::stoll(*findProperty(shape.properties(), "labelX")) : 0;
        const std::int64_t labelY = findProperty(shape.properties(), "labelY") ? std::stoll(*findProperty(shape.properties(), "labelY")) : 0;
        const std::int64_t dist = findProperty(shape.properties(), "dist") ? std::stoll(*findProperty(shape.properties(), "dist")) : 0;
        const std::string nodeSet = findProperty(shape.properties(), "nodeSet") ? *findProperty(shape.properties(), "nodeSet") : "";
        appendWireSegment(lines, path->points[0], path->points[1], label, labelX, labelY, dist, dbuPerEditorUnit,
                          nodeSet);
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
            connectPoints(lines, net.terms()[i - 1].position(), net.terms()[i].position(), i == 1 ? label : "",
                          dbuPerEditorUnit);
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

    std::ofstream out(schPath);
    if (!out) {
        m_errors.push_back("Cannot open file for writing: " + schPath);
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

    auto propertyLines = collectProperties(content->properties(), "schematic.view");
    if (propertyLines.empty()) {
        propertyLines = {"<View=0,0,800,600,1,0,0>", "<Grid=10,10,1>"};
    }
    writeSection(out, "Properties", propertyLines);

    for (const char *sectionName : {"Symbol", "Components", "Wires", "Diagrams", "Paintings"}) {
        if (std::string(sectionName) == "Components") {
            std::vector<std::string> lines;
            for (const Instance &inst : content->block().instances()) {
                lines.push_back(formatComponentLine(inst, dbuPerEditorUnit));
            }
            writeSection(out, "Components", lines);
            continue;
        }
        if (std::string(sectionName) == "Wires") {
            writeSection(out, "Wires", formatWiresFromBlock(content->block(), content->layers(), dbuPerEditorUnit));
            continue;
        }
        writeSection(out, sectionName, collectProperties(content->properties(), std::string("section.") + sectionName));
    }
}

} // namespace core
