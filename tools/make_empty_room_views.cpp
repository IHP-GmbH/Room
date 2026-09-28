#include "database.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    if (argc < 4) {
        std::cerr << "usage: make_empty_room_views <cell> <layout.room> <schematic.room>\n";
        return 1;
    }

    const std::string cell = argv[1];
    try {
        room::Database layoutDb;
        layoutDb.setGenerator("fixture");
        room::Cell &layoutCell = layoutDb.lib().getOrCreateCell(cell);
        layoutCell.getOrCreateContent(room::ViewType::Layout);
        layoutDb.lib().refreshIndex(room::ViewType::Layout);
        layoutDb.saveToFile(argv[2], room::ViewType::Layout);

        room::Database schematicDb;
        schematicDb.setGenerator("fixture");
        room::Cell &schematicCell = schematicDb.lib().getOrCreateCell(cell);
        schematicCell.getOrCreateContent(room::ViewType::Schematic);
        schematicDb.saveToFile(argv[3], room::ViewType::Schematic);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 2;
    }

    return 0;
}
