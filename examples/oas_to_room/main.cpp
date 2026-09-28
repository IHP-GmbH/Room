#include "database.h"
#include "oas_importer.h"
#include "text_dump.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultOas = "examples/oas_to_room/data/sample.oas";
constexpr const char *kDefaultRoom = "examples/oas_to_room/output/layout.room";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <input.oas|dir> <output.room> [dump.txt]\n"
              << "\n"
              << "  input       OASIS file or directory of *.oas files\n"
              << "  output.room ROOM layout file path\n"
              << "  dump.txt    optional text dump\n";
}

std::string defaultDumpPath(const std::string &roomPath)
{
    if (roomPath.size() >= 5 && roomPath.compare(roomPath.size() - 5, 5, ".room") == 0) {
        return roomPath.substr(0, roomPath.size() - 5) + ".txt";
    }
    return roomPath + ".txt";
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 2 && std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string roomPath = argv[2];
    const std::string dumpPath = (argc >= 4) ? argv[3] : defaultDumpPath(roomPath);

    room::OasImporter::Options opts;
    opts.libName = "oas_import";
    room::OasImporter importer(opts);

    std::cout << "Importing OASIS: " << inputPath << '\n';
    room::Database db = importer.importFile(inputPath);
    for (const auto &w : importer.warnings()) {
        std::cerr << "warning: " << w << '\n';
    }
    for (const auto &e : importer.errors()) {
        std::cerr << "error: " << e << '\n';
    }
    if (!importer.errors().empty()) {
        return 2;
    }

    db.setGenerator("ROOM oas_to_room");
    db.saveToFile(roomPath, room::ViewType::Layout);
    std::cout << "Saved ROOM file: " << roomPath << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    room::TextDumper dumper;
    dumper.dumpToFile(db, dumpPath);
    std::cout << "Text dump: " << dumpPath << '\n';

    return 0;
}
