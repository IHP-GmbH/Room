#include "database.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    if (argc < 4) {
        std::cerr << "usage: make_empty_core_views <cell> <layout.core> <schematic.core>\n";
        return 1;
    }

    const std::string cell = argv[1];
    try {
        core::Database layoutDb;
        layoutDb.setGenerator("fixture");
        core::Cell &layoutCell = layoutDb.lib().getOrCreateCell(cell);
        layoutCell.getOrCreateContent(core::ViewType::Layout);
        layoutDb.lib().refreshIndex(core::ViewType::Layout);
        layoutDb.saveToFile(argv[2], core::ViewType::Layout);

        core::Database schematicDb;
        schematicDb.setGenerator("fixture");
        core::Cell &schematicCell = schematicDb.lib().getOrCreateCell(cell);
        schematicCell.getOrCreateContent(core::ViewType::Schematic);
        schematicDb.saveToFile(argv[3], core::ViewType::Schematic);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 2;
    }

    return 0;
}
