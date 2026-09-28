#include "room_paths.h"

#include <iostream>
#include <string>

int main()
{
    const std::string path = room::roomFileName("top", room::ViewType::Schematic);
    if (path != "top.schematic.room") {
        std::cerr << "roomFileName failed: " << path << '\n';
        return 1;
    }

    const room::ParsedRoomPath parsed = room::parseRoomFilePath("lib/primitives.symbol.room");
    if (!parsed.valid || parsed.cellName != "primitives" || parsed.view != room::ViewType::Symbol) {
        std::cerr << "parseRoomFilePath failed\n";
        return 2;
    }

    if (!room::isViewRoomFile("cell.layout.room", room::ViewType::Layout)) {
        std::cerr << "isViewRoomFile layout failed\n";
        return 3;
    }

    if (room::isViewRoomFile("cell.schematic.room", room::ViewType::Layout)) {
        std::cerr << "isViewRoomFile should reject schematic as layout\n";
        return 4;
    }

    if (room::roomFileGlob(room::ViewType::Symbol) != "*.symbol.room") {
        std::cerr << "roomFileGlob failed\n";
        return 5;
    }

    std::cout << "room_paths OK\n";
    return 0;
}
