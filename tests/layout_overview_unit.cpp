#include "database.h"
#include "serialization.h"

#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

void ensureParentDir(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return;
    }
    std::string partial;
    for (char ch : path.substr(0, slash + 1)) {
        partial.push_back(ch);
        if ((ch == '/' || ch == '\\') && partial.size() > 1) {
#ifdef _WIN32
            _mkdir(partial.c_str());
#else
            mkdir(partial.c_str(), 0755);
#endif
        }
    }
}

room::Shape rect(std::int64_t llx, std::int64_t lly, std::int64_t urx, std::int64_t ury)
{
    room::Shape::RectData data;
    data.box = room::Box(llx, lly, urx, ury);
    data.layerId = 1;
    return room::Shape(data);
}

bool hasDecodedLayout(const room::Database &db, const std::string &name)
{
    const room::Cell *cell = db.lib().findCell(name);
    return cell != nullptr && cell->findContent(room::ViewType::Layout) != nullptr;
}

// TOP (1 rect, 100 x MID) -> MID (5 rects, 1 x LEAF) -> LEAF (10 rects)
void writeFixture(const std::string &path)
{
    room::Database db;
    db.setFileView(room::ViewType::Layout);
    room::Lib &lib = db.lib();

    room::CellContent &leaf = lib.getOrCreateCell("LEAF").getOrCreateContent(room::ViewType::Layout);
    for (int i = 0; i < 10; ++i) {
        leaf.block().shapes().push_back(rect(i * 10, 0, i * 10 + 5, 5));
    }

    room::CellContent &mid = lib.getOrCreateCell("MID").getOrCreateContent(room::ViewType::Layout);
    for (int i = 0; i < 5; ++i) {
        mid.block().shapes().push_back(rect(0, i * 20, 200, i * 20 + 10));
    }
    mid.block().instances().emplace_back("LEAF", room::Transform(0, 200));

    room::CellContent &top = lib.getOrCreateCell("TOP").getOrCreateContent(room::ViewType::Layout);
    top.block().shapes().push_back(rect(0, 0, 10000, 10000));
    for (int i = 0; i < 100; ++i) {
        top.block().instances().emplace_back("MID", room::Transform((i % 10) * 1000, (i / 10) * 1000));
    }

    lib.recomputeAllBBoxes();
    lib.refreshIndex(room::ViewType::Layout);
    ensureParentDir(path);
    db.saveToFile(path, room::ViewType::Layout);
}

int fail(int code, const std::string &message)
{
    std::cerr << "error: " << message << '\n';
    return code;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string path = (argc > 1) ? argv[1] : "build/tests/layout_overview.layout.room";
    writeFixture(path);

    // Subtree via memory map: whole hierarchy under TOP.
    {
        const room::Database db = room::loadLayoutCellSubtree(path, "TOP", 4);
        if (!hasDecodedLayout(db, "TOP") || !hasDecodedLayout(db, "MID") || !hasDecodedLayout(db, "LEAF")) {
            return fail(1, "subtree must decode TOP, MID and LEAF");
        }
    }

    // Zero budget: only the root is decoded, but the index still has child bboxes.
    {
        room::LayoutOverviewOptions options;
        options.shapeBudget = 0;
        const room::Database db = room::loadLayoutOverview(path, "TOP", options);
        if (!hasDecodedLayout(db, "TOP")) {
            return fail(2, "overview must always decode the root cell");
        }
        if (hasDecodedLayout(db, "MID") || hasDecodedLayout(db, "LEAF")) {
            return fail(3, "zero budget must not decode children");
        }
        if (!db.lib().hasIndex()) {
            return fail(4, "overview must attach the persisted index");
        }
        const auto bbox = db.lib().index().cellBboxes.find("MID");
        if (bbox == db.lib().index().cellBboxes.end() || bbox->second.empty()) {
            return fail(5, "index must carry MID bbox for box drawing");
        }
    }

    // Large budget: breadth-first reaches the leaves.
    {
        const room::Database db = room::loadLayoutOverview(path, "TOP");
        if (!hasDecodedLayout(db, "MID") || !hasDecodedLayout(db, "LEAF")) {
            return fail(6, "default budget must decode the full small hierarchy");
        }
    }

    // Start from a child cell (expanded tree item).
    {
        const room::Database db = room::loadLayoutOverview(path, "MID");
        if (!hasDecodedLayout(db, "MID") || hasDecodedLayout(db, "TOP")) {
            return fail(7, "overview of MID must not decode its parent");
        }
    }

    // Unknown cell is an error (callers fall back to primary cell).
    try {
        (void)room::loadLayoutOverview(path, "NO_SUCH_CELL");
        return fail(8, "unknown cell must throw");
    } catch (const std::runtime_error &) {
    }

    std::cout << "layout overview OK\n";
    return 0;
}
