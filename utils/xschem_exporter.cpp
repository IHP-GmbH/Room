/*!****************************************************************************************
 * \file xschem_exporter.cpp
 * \brief Export CORE schematic/symbol views back to native Xschem files.
 *****************************************************************************************/

#include "xschem_exporter.h"

#include "cell_content.h"
#include "xschem_format.h"
#include "xschem_io.h"

#include <fstream>
#include <sstream>

namespace core {
namespace {

const CellContent *findXschemView(const Cell &cell)
{
    if (const CellContent *schematic = cell.findContent(ViewType::Schematic)) {
        return schematic;
    }
    return cell.findContent(ViewType::Symbol);
}

std::string defaultOutputPath(const std::string &cellName, const CellContent &content)
{
    const std::string ext = xschem::extensionForViewType(content.viewType());
    return cellName + ext;
}

} // namespace

void XschemExporter::exportCell(const Database &db, const std::string &cellName,
                                const std::string &outputPath) const
{
    std::ofstream out(outputPath);
    if (!out) {
        m_warnings.clear();
        m_errors.clear();
        m_errors.push_back("Cannot open file for writing: " + outputPath);
        return;
    }
    exportCell(db, cellName, out);
}

void XschemExporter::exportCell(const Database &db, const std::string &cellName, std::ostream &out) const
{
    m_warnings.clear();
    m_errors.clear();

    const Cell *cell = db.lib().findCell(cellName);
    if (cell == nullptr) {
        m_errors.push_back("Cell not found: " + cellName);
        return;
    }

    const CellContent *content = findXschemView(*cell);
    if (content == nullptr) {
        m_errors.push_back("No Xschem schematic/symbol view for cell: " + cellName);
        return;
    }

    try {
        xschem::exportRecords(out, *cell, *content);
    } catch (const std::exception &ex) {
        m_errors.push_back(ex.what());
    }
}

std::string XschemExporter::exportCellToString(const Database &db, const std::string &cellName) const
{
    std::ostringstream out;
    exportCell(db, cellName, out);
    return out.str();
}

std::size_t XschemExporter::exportAll(const Database &db, const std::string &outputDir) const
{
    m_warnings.clear();
    m_errors.clear();

    std::size_t count = 0;
    for (const Cell &cell : db.lib().cells()) {
        const CellContent *content = findXschemView(cell);
        if (content == nullptr) {
            continue;
        }

        std::string path = outputDir;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path.push_back('/');
        }
        path += defaultOutputPath(cell.name(), *content);

        exportCell(db, cell.name(), path);
        if (m_errors.empty()) {
            ++count;
        }
    }

    return count;
}

} // namespace core
