#include "database.h"
#include "gds_importer.h"
#include "lib_index.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

bool boxesEqual(const room::Box &a, const room::Box &b)
{
    return a.llx == b.llx && a.lly == b.lly && a.urx == b.urx && a.ury == b.ury;
}

bool indexMatches(const room::LibIndex &a, const room::LibIndex &b)
{
    if (a.topCells != b.topCells) {
        return false;
    }
    if (a.placementCount != b.placementCount) {
        return false;
    }
    if (a.referenceCount != b.referenceCount) {
        return false;
    }
    if (a.childRefs != b.childRefs) {
        return false;
    }
    if (a.cellBboxes.size() != b.cellBboxes.size()) {
        return false;
    }
    for (const auto &entry : a.cellBboxes) {
        const auto other = b.cellBboxes.find(entry.first);
        if (other == b.cellBboxes.end() || !boxesEqual(entry.second, other->second)) {
            return false;
        }
    }
    return true;
}

void ensureDirectory(const std::string &dirPath)
{
    std::string partial;
    partial.reserve(dirPath.size());
    for (char ch : dirPath) {
        partial.push_back(ch);
        if (ch != '/' && ch != '\\') {
            continue;
        }
        if (partial.size() <= 1) {
            continue;
        }
#ifdef _WIN32
        _mkdir(partial.c_str());
#else
        mkdir(partial.c_str(), 0755);
#endif
    }
#ifdef _WIN32
    _mkdir(partial.c_str());
#else
    mkdir(partial.c_str(), 0755);
#endif
}

bool layoutBboxesValid(const room::Database &db)
{
    for (const auto &cell : db.lib().cells()) {
        const room::CellContent *content = cell.findContent(room::ViewType::Layout);
        if (content == nullptr || content->block().shapes().empty()) {
            continue;
        }
        if (content->block().bbox().empty()) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string gdsPath = (argc > 1) ? argv[1] : "testdata/sample.gds";
    const std::string roomPath = (argc > 2) ? argv[2] : "build/tests/lib_index_persist.room";

    room::GdsImporter importer;
    room::Database original = importer.importFile(gdsPath);
    if (!importer.errors().empty()) {
        for (const auto &msg : importer.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 1;
    }

    if (!layoutBboxesValid(original)) {
        std::cerr << "error: layout cells with shapes must have a bbox after import\n";
        return 2;
    }

    const room::LibIndex expectedIndex = room::LibIndex::build(original.lib());
    const std::size_t slash = roomPath.find_last_of("/\\");
    if (slash != std::string::npos) {
        ensureDirectory(roomPath.substr(0, slash));
    }
    original.saveToFile(roomPath, room::ViewType::Layout);

    const room::Database reloaded = room::Database::loadFromFile(roomPath);
    if (!reloaded.lib().hasIndex()) {
        std::cerr << "error: expected persisted LibIndex after load\n";
        return 3;
    }
    if (!layoutBboxesValid(reloaded)) {
        std::cerr << "error: bbox missing after .room reload\n";
        return 4;
    }

    const room::LibIndex rebuilt = room::LibIndex::build(reloaded.lib());
    if (!indexMatches(reloaded.lib().index(), expectedIndex)) {
        std::cerr << "error: persisted index does not match pre-save index\n";
        return 5;
    }
    if (!indexMatches(rebuilt, expectedIndex)) {
        std::cerr << "error: rebuilt index does not match after reload\n";
        return 6;
    }

    std::cout << "index persist OK: top=" << reloaded.lib().index().topCells.size()
              << " placements=" << reloaded.lib().index().placementCount << '\n';
    return 0;
}
