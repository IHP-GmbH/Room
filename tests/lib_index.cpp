#include "block.h"
#include "gds_importer.h"
#include "lib_index.h"

#include <iostream>
#include <string>

namespace {

bool layoutBboxesValid(const core::Database &db)
{
    for (const auto &cell : db.lib().cells()) {
        const core::CellContent *content = cell.findContent(core::ViewType::Layout);
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

    core::GdsImporter importer;
    core::Database db = importer.importFile(gdsPath);
    if (!importer.errors().empty()) {
        for (const auto &msg : importer.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 1;
    }

    if (!layoutBboxesValid(db)) {
        std::cerr << "error: layout cells with shapes must have a bbox after import\n";
        return 2;
    }

    const core::LibIndex index = core::LibIndex::build(db.lib());
    std::cout << "cells: " << db.lib().cells().size() << '\n';
    std::cout << "top cells: " << index.topCells.size() << '\n';
    std::cout << "placements: " << index.placementCount << '\n';

    if (index.topCells.empty()) {
        std::cerr << "error: expected at least one top cell\n";
        return 3;
    }

    if (db.lib().cells().size() > 1 && index.placementCount == 0 &&
        index.topCells.size() == db.lib().cells().size()) {
        std::cout << "note: flat library (no instances)\n";
    } else if (index.placementCount == 0) {
        std::cerr << "error: expected at least one placement\n";
        return 4;
    }

    std::cout << "top:";
    for (const auto &name : index.topCells) {
        std::cout << ' ' << name;
    }
    std::cout << '\n';

    return 0;
}
