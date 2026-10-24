#include "xschem_bridge.h"

#include "room_paths.h"
#include "database.h"
#include "em_model_netlist.h"
#include "room_json_io.h"
#include "xschem_exporter.h"
#include "xschem_format.h"

#include <fstream>

namespace room::xschem_bridge {
namespace {

ViewType fileViewForPath(const std::string &path)
{
    const std::size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? ".sch" : path.substr(dot);
    return room::xschem::viewTypeForExtension(ext);
}

void appendMessages(Status &status, const std::vector<std::string> &messages, bool asError)
{
    auto &target = asError ? status.errors : status.warnings;
    target.insert(target.end(), messages.begin(), messages.end());
    if (asError && !messages.empty()) {
        status.ok = false;
    }
}

std::string cellNameForImport(const std::string &roomPath, const XschemImporter::Options &options)
{
    if (!options.cellName.empty()) {
        return options.cellName;
    }
    const ParsedRoomPath parsed = parseRoomFilePath(
        RoomJsonIo::isRoomJsonPath(roomPath) ? RoomJsonIo::siblingTwinPath(roomPath) : roomPath);
    if (parsed.valid && !parsed.cellName.empty()) {
        return parsed.cellName;
    }
    return "cell";
}

Database loadDb(const std::string &path, Status &status)
{
    Database db;
    const RoomJsonIo::Result loaded = RoomJsonIo::loadRoomOrJson(path, db);
    if (!loaded.ok) {
        status.ok = false;
        status.errors.push_back(loaded.message);
    }
    return db;
}

ViewType viewForSavePath(const std::string &path, ViewType fromEditor)
{
    // Prefer encoded view in *.schematic.room / *.symbol.room / *.json
    const std::string roomish =
        RoomJsonIo::isRoomJsonPath(path) ? RoomJsonIo::siblingTwinPath(path) : path;
    const ParsedRoomPath parsed = parseRoomFilePath(roomish);
    if (parsed.valid) {
        return parsed.view;
    }
    return fromEditor;
}

bool saveDb(Database &db, ViewType view, const std::string &path, Status &status)
{
    const RoomJsonIo::Result saved = RoomJsonIo::saveViewWithJsonTwin(db, view, path);
    if (!saved.ok) {
        status.ok = false;
        status.errors.push_back(saved.message);
        return false;
    }
    return true;
}

} // namespace

std::vector<std::string> listCells(const std::string &roomPath, Status &status)
{
    std::vector<std::string> cells;
    try {
        const Database db = loadDb(roomPath, status);
        if (!status.ok) {
            return cells;
        }
        for (const Cell &cell : db.lib().cells()) {
            cells.push_back(cell.name());
        }
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return cells;
}

Status exportCell(const std::string &roomPath, const std::string &cellName, const std::string &outputPath)
{
    Status status;
    try {
        const Database db = loadDb(roomPath, status);
        if (!status.ok) {
            return status;
        }
        XschemExporter exporter;
        exporter.exportCell(db, cellName, outputPath);
        appendMessages(status, exporter.warnings(), false);
        appendMessages(status, exporter.errors(), true);
        const std::string roomForLookalike =
            RoomJsonIo::isRoomJsonPath(roomPath) ? RoomJsonIo::siblingTwinPath(roomPath) : roomPath;
        if (status.ok && symbolRoomIsLookalike(roomForLookalike)) {
            std::string spicePath = outputPath;
            const auto dot = spicePath.find_last_of('.');
            const auto slash = spicePath.find_last_of("/\\");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
                spicePath = spicePath.substr(0, dot) + ".spice";
            } else {
                spicePath += ".spice";
            }
            const std::string err =
                writeLookalikeSpiceForSymbolRoom(roomForLookalike, spicePath, cellName);
            if (!err.empty()) {
                status.warnings.push_back("lookalike spice: " + err);
            }
        }
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return status;
}

std::string exportCellToString(const std::string &roomPath, const std::string &cellName, Status &status)
{
    status = Status{};
    try {
        const Database db = loadDb(roomPath, status);
        if (!status.ok) {
            return {};
        }
        XschemExporter exporter;
        const std::string data = exporter.exportCellToString(db, cellName);
        appendMessages(status, exporter.warnings(), false);
        appendMessages(status, exporter.errors(), true);
        if (!status.ok) {
            return {};
        }
        return data;
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
        return {};
    }
}

Status exportAll(const std::string &roomPath, const std::string &outputDir, std::size_t &exportedCount)
{
    Status status;
    exportedCount = 0;
    try {
        const Database db = loadDb(roomPath, status);
        if (!status.ok) {
            return status;
        }
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

bool pathReadable(const std::string &path)
{
    return std::ifstream(path).good();
}

std::string resolveExistingTwin(const std::string &path)
{
    if (pathReadable(path)) {
        return path;
    }
    const std::string twin = RoomJsonIo::siblingTwinPath(path);
    if (pathReadable(twin)) {
        return twin;
    }
    return {};
}

Status importIntoCore(const std::string &inputPath, const std::string &roomPath, const XschemImporter::Options &options)
{
    Status status;
    try {
        Database db;
        const std::string existing = resolveExistingTwin(roomPath);
        if (!existing.empty()) {
            db = loadDb(existing, status);
            if (!status.ok) {
                return status;
            }
            status = Status{};
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

        const ViewType view = viewForSavePath(roomPath, fileViewForPath(inputPath));
        if (!saveDb(db, view, roomPath, status)) {
            return status;
        }
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return status;
}

Status importTextIntoCore(const std::string &text, const std::string &extension, const std::string &roomPath,
                          const XschemImporter::Options &options)
{
    Status status;
    try {
        Database db;
        const std::string existing = resolveExistingTwin(roomPath);
        if (!existing.empty()) {
            db = loadDb(existing, status);
            if (!status.ok) {
                return status;
            }
            status = Status{};
        } else {
            db.lib() = Lib(options.libName);
        }

        XschemImporter importer(options);
        db = importer.importTextInto(std::move(db), text, extension, cellNameForImport(roomPath, options));
        appendMessages(status, importer.warnings(), false);
        appendMessages(status, importer.errors(), true);
        if (!status.ok) {
            return status;
        }

        const ViewType view = viewForSavePath(roomPath, fileViewForPath("placeholder" + extension));
        if (!saveDb(db, view, roomPath, status)) {
            return status;
        }
    } catch (const std::exception &ex) {
        status.ok = false;
        status.errors.push_back(ex.what());
    }
    return status;
}

} // namespace room::xschem_bridge
