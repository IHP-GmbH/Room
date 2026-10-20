/************************************************************************
 *  CommonDB – ROOM design database
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "em_model_netlist.h"

#include "database.h"
#include "room_paths.h"
#include "touchstone_to_spice.h"

#include <cctype>
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace room {
namespace {

std::string trimCopy(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

bool endsWithInsensitive(const std::string &value, const std::string &suffix)
{
    if (value.size() < suffix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a =
            static_cast<unsigned char>(value[value.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

bool pathExists(const std::string &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

bool isWindowsDrivePath(const std::string &path)
{
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
}

bool isAbsolutePath(const std::string &path)
{
    if (isWindowsDrivePath(path)) {
        return true;
    }
    return !path.empty() && (path[0] == '/' || path[0] == '\\');
}

/*! Map C:/foo or C:\foo → /mnt/c/foo when running under WSL/Linux. */
std::string windowsPathToPosixIfNeeded(const std::string &path)
{
    if (!isWindowsDrivePath(path)) {
        return path;
    }
#ifdef _WIN32
    return path;
#else
    std::string out = "/mnt/";
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(path[0]))));
    out.push_back('/');
    for (std::size_t i = 2; i < path.size(); ++i) {
        const char ch = path[i];
        out.push_back(ch == '\\' ? '/' : ch);
    }
    return out;
#endif
}

std::string parentDir(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return {};
    }
    return path.substr(0, pos);
}

std::string joinPath(const std::string &dir, const std::string &file)
{
    if (dir.empty()) {
        return file;
    }
    const char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + file;
    }
#ifdef _WIN32
    return dir + '\\' + file;
#else
    return dir + '/' + file;
#endif
}

void ensureParentDirectories(const std::string &filePath)
{
    std::string partial;
    for (char ch : filePath) {
        if (ch == '/' || ch == '\\') {
            if (partial.size() > 1) {
#ifdef _WIN32
                _mkdir(partial.c_str());
#else
                mkdir(partial.c_str(), 0755);
#endif
            }
        }
        partial.push_back(ch);
    }
}

int portsFromSnpSuffix(const std::string &snpPath)
{
    const auto slash = snpPath.find_last_of("/\\");
    const std::string file = slash == std::string::npos ? snpPath : snpPath.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos) {
        return 0;
    }
    std::string ext = file.substr(dot + 1);
    for (char &c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext.size() >= 3 && ext.front() == 's' && ext.back() == 'p') {
        try {
            return std::stoi(ext.substr(1, ext.size() - 2));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

std::string resolveSnpPath(const std::string &emModelPath, const std::string &snpRaw)
{
    const std::string snp = trimCopy(snpRaw);
    if (snp.empty()) {
        return {};
    }
    if (isAbsolutePath(snp)) {
        const std::string abs = windowsPathToPosixIfNeeded(snp);
        if (pathExists(abs)) {
            return abs;
        }
        return abs;
    }
    const std::string joined = joinPath(parentDir(emModelPath), snp);
    if (pathExists(joined)) {
        return joined;
    }
    // Emmodel may still store a Windows absolute path that failed isAbsolutePath
    // heuristics; try the POSIX mapping of the raw string as a last resort.
    const std::string mapped = windowsPathToPosixIfNeeded(snp);
    if (mapped != snp && pathExists(mapped)) {
        return mapped;
    }
    return joined;
}

void replaceOrPush(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    for (Property &p : props) {
        if (p.name == name) {
            p.value = value;
            return;
        }
    }
    props.push_back({name, value});
}

const Cell *findCellLoose(const Database &db, const std::string &cellName)
{
    if (!cellName.empty()) {
        if (const Cell *c = db.lib().findCell(cellName)) {
            return c;
        }
    }
    if (!db.fileSummary().primaryCell.empty()) {
        return db.lib().findCell(db.fileSummary().primaryCell);
    }
    if (db.lib().cells().size() == 1) {
        return &db.lib().cells().front();
    }
    return nullptr;
}

std::string cellNameFromSymbolPath(const std::string &symbolRoomPath)
{
    const ParsedRoomPath parsed = parseRoomFilePath(symbolRoomPath);
    if (!parsed.cellName.empty()) {
        return parsed.cellName;
    }
    const auto slash = symbolRoomPath.find_last_of("/\\");
    std::string file =
        slash == std::string::npos ? symbolRoomPath : symbolRoomPath.substr(slash + 1);
    // cell.symbol.room → cell
    const auto firstDot = file.find('.');
    if (firstDot != std::string::npos) {
        return file.substr(0, firstDot);
    }
    return file;
}

} // namespace

bool contentHasLookalike(const CellContent &content)
{
    for (const Property &p : content.properties()) {
        if (p.name == "em.lookalike"
            && (p.value == "1" || p.value == "true" || p.value == "yes")) {
            return true;
        }
    }
    return false;
}

std::string siblingRoomViewPath(const std::string &roomPath, const std::string &targetView)
{
    static const char *kKnown[] = {".schematic.room", ".symbol.room", ".sym.room",
                                   ".emmodel.room",    ".layout.room", ".emsetup.room"};
    const std::string target = "." + targetView + ".room";
    for (const char *suf : kKnown) {
        const std::string suffix(suf);
        if (endsWithInsensitive(roomPath, suffix)) {
            return roomPath.substr(0, roomPath.size() - suffix.size()) + target;
        }
    }
    if (endsWithInsensitive(roomPath, ".room")) {
        return roomPath;
    }
    return roomPath + target;
}

bool symbolRoomIsLookalike(const std::string &symbolRoomPath)
{
    try {
        const Database db = Database::loadFromFile(symbolRoomPath);
        const ParsedRoomPath parsed = parseRoomFilePath(symbolRoomPath);
        const Cell *cell = findCellLoose(db, parsed.cellName);
        if (!cell) {
            return false;
        }
        const CellContent *content = cell->findContent(ViewType::Symbol);
        return content && contentHasLookalike(*content);
    } catch (...) {
        return false;
    }
}

bool tryLoadEmModelNetlist(const std::string &symbolRoomPath, EmModelNetlistInfo &out,
                           std::string *errorOut)
{
    out = EmModelNetlistInfo{};
    if (symbolRoomPath.empty()) {
        if (errorOut) {
            *errorOut = "Symbol ROOM path is empty.";
        }
        return false;
    }

    out.lookalike = symbolRoomIsLookalike(symbolRoomPath);
    const std::string emPath = siblingRoomViewPath(symbolRoomPath, "emmodel");
    if (!pathExists(emPath)) {
        if (errorOut) {
            *errorOut = "Sibling emmodel not found: " + emPath;
        }
        return false;
    }

    try {
        const Database db = Database::loadFromFile(emPath);
        const ParsedRoomPath parsed = parseRoomFilePath(emPath);
        const Cell *cell = findCellLoose(db, parsed.cellName);
        if (!cell) {
            if (errorOut) {
                *errorOut = "No cell in emmodel: " + emPath;
            }
            return false;
        }
        const CellContent *content = cell->findContent(ViewType::EmModel);
        if (!content || !content->hasEmModelPayload()) {
            if (errorOut) {
                *errorOut = "Missing EmModel payload: " + emPath;
            }
            return false;
        }
        const EmModelViewData &model = content->emModel();
        const std::string snp = resolveSnpPath(emPath, model.snpPath);
        if (snp.empty() || !pathExists(snp)) {
            if (errorOut) {
                *errorOut = "Touchstone missing: " + snp;
            }
            return false;
        }

        out.emModelPath = emPath;
        out.snpPath = snp;
        out.z0 = model.z0 > 0.0 ? model.z0 : 50.0;
        out.portCount = static_cast<int>(model.ports.size());
        if (out.portCount <= 0) {
            out.portCount = portsFromSnpSuffix(snp);
        }
        return true;
    } catch (const std::exception &ex) {
        if (errorOut) {
            *errorOut = ex.what();
        }
        return false;
    } catch (...) {
        if (errorOut) {
            *errorOut = "Failed to load emmodel netlist info.";
        }
        return false;
    }
}

std::string writeLookalikeSpiceForSymbolRoom(const std::string &symbolRoomPath,
                                             const std::string &spiceOutPath,
                                             const std::string &deviceName)
{
    EmModelNetlistInfo info;
    std::string err;
    if (!tryLoadEmModelNetlist(symbolRoomPath, info, &err) || !info.lookalike) {
        return err.empty() ? "Not a lookalike symbol or emmodel unresolved." : err;
    }
    if (spiceOutPath.empty()) {
        return "Spice output path is empty.";
    }

    std::string name = trimCopy(deviceName);
    if (name.empty()) {
        name = cellNameFromSymbolPath(symbolRoomPath);
    }

    ensureParentDirectories(spiceOutPath);

    if (!writeTouchstoneSpiceFile(info.snpPath, name, info.z0, spiceOutPath, &err)) {
        return err.empty() ? "Touchstone conversion failed." : err;
    }
    return {};
}

void applyLookalikeXschemNetlistProps(std::vector<Property> &props)
{
    bool lookalike = false;
    for (const Property &p : props) {
        if (p.name == "em.lookalike"
            && (p.value == "1" || p.value == "true" || p.value == "yes")) {
            lookalike = true;
            break;
        }
    }
    if (!lookalike) {
        return;
    }
    // Black-box subcircuit: instance nodes = pins + trailing gnd; body from sibling .spice
    // (.include injected by room.tcl). default_schematic=ignore skips spice_block_netlist's
    // empty .subckt wrapper (spice_stop alone still emits .subckt … .ends).
    replaceOrPush(props, "type", "subcircuit");
    replaceOrPush(props, "spice_stop", "true");
    replaceOrPush(props, "default_schematic", "ignore");
    replaceOrPush(props, "format", "@name @pinlist 0 @symname");
    replaceOrPush(props, "template", "name=x1");
}

} // namespace room
