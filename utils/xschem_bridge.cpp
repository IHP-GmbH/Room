#include "xschem_bridge.h"

#include "database.h"
#include "xschem_exporter.h"
#include "xschem_format.h"

#include <fstream>

namespace core::xschem_bridge {
namespace {

ViewType fileViewForPath(const std::string &path)
{
    const std::size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? ".sch" : path.substr(dot);
    return core::xschem::viewTypeForExtension(ext);
}

void appendMessages(Status &status, const std::vector<std::string> &messages, bool asError)
{
    auto &target = asError ? status.errors : status.warnings;
    target.insert(target.end(), messages.begin(), messages.end());
    if (asError && !messages.empty()) {
        status.ok = false;
    }
}

} // namespace

std::vector<std::string> listCells(const std::string &corePath, Status &status)
{
    std::vector<std::string> cells;
    try {
        const Database db = Database::loadFromFile(corePath);
        for (const Cell &cell : db.lib().cells()) {
            cells.push_back(cell.name());
        }
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return cells;
}

Status exportCell(const std::string &corePath, const std::string &cellName, const std::string &outputPath)
{
    Status status;
    try {
        const Database db = Database::loadFromFile(corePath);
        XschemExporter exporter;
        exporter.exportCell(db, cellName, outputPath);
        appendMessages(status, exporter.warnings(), false);
        appendMessages(status, exporter.errors(), true);
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return status;
}

Status exportAll(const std::string &corePath, const std::string &outputDir, std::size_t &exportedCount)
{
    Status status;
    exportedCount = 0;
    try {
        const Database db = Database::loadFromFile(corePath);
        XschemExporter exporter;
        exportedCount = exporter.exportAll(db, outputDir);
        appendMessages(status, exporter.warnings(), false);
        appendMessages(status, exporter.errors(), true);
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return status;
}

Status importIntoCore(const std::string &inputPath, const std::string &corePath, const XschemImporter::Options &options)
{
    Status status;
    try {
        Database db;
        if (std::ifstream(corePath).good()) {
            db = Database::loadFromFile(corePath);
        } else {
            db.lib() = Lib(options.libName);
        }

        XschemImporter importer(options);
        db = importer.importFileInto(std::move(db), inputPath);
        appendMessages(status, importer.warnings(), false);
        appendMessages(status, importer.errors(), true);
        if (!status.ok) {
            return status;
        }

        db.saveToFile(corePath, fileViewForPath(inputPath));
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return status;
}

} // namespace core::xschem_bridge
