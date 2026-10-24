#include "room_json_io.h"

#include <iostream>
#include <string>

namespace {

void printUsage(const char *prog)
{
    std::cerr << "Usage:\n"
              << "  " << prog << " <input.schematic.json|symbol.json> <output.room>\n";
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3 || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return argc < 3 ? 1 : 0;
    }

    const std::string jsonPath = argv[1];
    const std::string roomPath = argv[2];

    const room::RoomJsonIo::Result result = room::RoomJsonIo::jsonFileToRoom(jsonPath, roomPath);
    if (!result.ok) {
        std::cerr << "json_to_room failed: " << result.message << '\n';
        return 2;
    }
    std::cout << "Wrote " << result.message << '\n';
    return 0;
}
