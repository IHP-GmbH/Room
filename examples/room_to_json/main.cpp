#include "room_json_io.h"
#include "room_paths.h"

#include <iostream>
#include <string>

namespace {

void printUsage(const char *prog)
{
    std::cerr << "Usage:\n"
              << "  " << prog << " <input.schematic.room|symbol.room> <output.json> [--cell name]\n";
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3 || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return argc < 3 ? 1 : 0;
    }

    const std::string roomPath = argv[1];
    const std::string jsonPath = argv[2];
    std::string cellName;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (std::string(argv[i]) == "--cell") {
            cellName = argv[i + 1];
        }
    }

    const room::RoomJsonIo::Result result = room::RoomJsonIo::roomFileToJson(roomPath, jsonPath, cellName);
    if (!result.ok) {
        std::cerr << "room_to_json failed: " << result.message << '\n';
        return 2;
    }
    std::cout << "Wrote " << result.message << '\n';
    return 0;
}
