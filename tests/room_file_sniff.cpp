#include "database.h"
#include "file_summary.h"
#include "xschem_importer.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    const std::string path = (argc > 1) ? argv[1] : "examples/xschem_to_room/data/test.sch";
    const std::string roomPath = (argc > 2) ? argv[2] : "build/tests/sniff_fixture.schematic.room";

    room::XschemImporter importer;
    room::Database db = importer.importFile(path);
    if (!importer.errors().empty()) {
        return 1;
    }
    db.saveToFile(roomPath, room::ViewType::Schematic);

    const room::RoomFileInfo info = room::sniffRoomFile(roomPath);
    if (info.summary.view != room::ViewType::Schematic) {
        std::cerr << "error: unexpected view\n";
        return 3;
    }

    std::cout << "sniff OK view=" << room::viewTypeToString(info.summary.view) << '\n';
    return 0;
}
