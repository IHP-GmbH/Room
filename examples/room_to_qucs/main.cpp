#include "room_paths.h"
#include "database.h"
#include "qucs_exporter.h"

#include <iostream>
#include <string>

namespace {

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <input.room> <cell> <output.sch>\n";
}

void printMessages(const char *kind, const std::vector<std::string> &messages)
{
    for (const auto &msg : messages) {
        std::cerr << kind << ": " << msg << '\n';
    }
}

bool isSymbolCorePath(const std::string &path)
{
    const room::ParsedRoomPath parsed = room::parseRoomFilePath(path);
    return parsed.valid && parsed.view == room::ViewType::Symbol;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2 || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }

    if (argc != 4) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string roomPath = argv[1];
    const std::string cellName = argv[2];
    const std::string schPath = argv[3];

    try {
        const room::Database db = room::Database::loadFromFile(roomPath);
        room::QucsExporter exporter;
        if (isSymbolCorePath(roomPath)) {
            exporter.exportSymbolCell(db, cellName, schPath);
        } else {
            exporter.exportCell(db, cellName, schPath);
        }
        printMessages("warning", exporter.warnings());
        printMessages("error", exporter.errors());
        if (!exporter.errors().empty()) {
            return 2;
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 3;
    }

    std::cout << "Exported cell " << cellName << " to " << schPath << '\n';
    return 0;
}
