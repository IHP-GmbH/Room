/*!****************************************************************************************
 * \file xschem_importer.cpp
 * \brief Import Xschem schematics/symbols into ROOM Block/Instance/Net/Shape model.
 *****************************************************************************************/

#include "xschem_importer.h"

#include "cell.h"
#include "lib.h"
#include "coord_scale.h"
#include "xschem_format.h"
#include "xschem_io.h"

#include <cctype>

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

std::string extensionFromPath(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return ".sch";
    }
    std::string ext = path.substr(dot);
    for (char &ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext;
}

} // namespace

XschemImporter::XschemImporter() = default;

XschemImporter::XschemImporter(const Options &options) : m_options(options) {}

Database XschemImporter::importFile(const std::string &path) const
{
    Database db;
    db.lib() = Lib(m_options.libName);
    return importFileInto(std::move(db), path);
}

Database XschemImporter::importFileInto(Database db, const std::string &path) const
{
    m_warnings.clear();
    m_errors.clear();

    const std::string extension = extensionFromPath(path);
    if (extension != ".sch" && extension != ".sym") {
        m_errors.push_back("Unsupported Xschem extension: " + extension);
        return db;
    }

    const std::vector<std::string> records = xschem::readRecords(path, m_errors);
    if (!m_errors.empty()) {
        return db;
    }

    const std::string cellName = m_options.cellName.empty() ? stemFromPath(path) : m_options.cellName;
    return importTextInto(std::move(db), records, extension, cellName, path);
}

Database XschemImporter::importText(const std::string &text, const std::string &extension,
                                    const std::string &cellName) const
{
    Database db;
    db.lib() = Lib(m_options.libName);
    return importTextInto(std::move(db), text, extension, cellName);
}

Database XschemImporter::importTextInto(Database db, const std::string &text, const std::string &extension,
                                        const std::string &cellName) const
{
    m_warnings.clear();
    m_errors.clear();

    if (extension != ".sch" && extension != ".sym") {
        m_errors.push_back("Unsupported Xschem extension: " + extension);
        return db;
    }

    const std::vector<std::string> records = xschem::readRecordsFromText(text, m_errors);
    if (!m_errors.empty()) {
        return db;
    }

    const std::string resolvedCellName = cellName.empty()
        ? (m_options.cellName.empty() ? std::string("cell") : m_options.cellName)
        : cellName;
    return importTextInto(std::move(db), records, extension, resolvedCellName, {});
}

Database XschemImporter::importTextInto(Database db, const std::vector<std::string> &records,
                                          const std::string &extension, const std::string &cellName,
                                          const std::string &sourcePath) const
{
    m_warnings.clear();
    m_errors.clear();

    const ViewType viewType = xschem::viewTypeForExtension(extension);

    Cell *existing = db.lib().findCell(cellName);
    if (existing != nullptr && existing->findContent(viewType) != nullptr && !m_options.updateExisting) {
        m_errors.push_back("Cell already has " + viewTypeToString(viewType) + " view: " + cellName);
        return db;
    }

    Cell &cell = db.lib().getOrCreateCell(cellName);
    CellContent &content = cell.getOrCreateContent(viewType, kXschemDbuPerEditorUnit);
    content.setDbuPerMicron(kXschemDbuPerEditorUnit);
    content.setDbuPerEditorUnit(kXschemDbuPerEditorUnit);
    content.clearOpaquePayload();

    xschem::importRecords(records, cell, content, m_warnings);
    if (!sourcePath.empty()) {
        content.properties().push_back({xschem::kPropSourcePath, sourcePath});
    }
    content.properties().push_back({xschem::kPropSourceExt, extension});

    for (const LayerSpec &layer : content.layers()) {
        bool found = false;
        for (const LayerSpec &libLayer : db.lib().layers()) {
            if (libLayer.name == layer.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            db.lib().layers().push_back(layer);
        }
    }

    db.setGenerator("ROOM XschemImporter");
    db.setTechnology("xschem");
    db.lib().recomputeAllBBoxes(viewType);
    return db;
}

} // namespace room
