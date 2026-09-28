#include "database.h"
#include "xschem_exporter.h"
#include "xschem_importer.h"
#include "xschem_format.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultSch = "examples/xschem_to_room/data/test.sch";
constexpr const char *kDefaultRoom = "examples/xschem_to_room/output/schematic.room";
constexpr const char *kDefaultRoundtrip = "examples/xschem_to_room/output/schematic_roundtrip.sch";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [input.sch|sym] [output.room] [roundtrip.out]\n"
              << "\n"
              << "  input.sch|sym   Xschem schematic or symbol (default: " << kDefaultSch << ")\n"
              << "  output.room     ROOM file with opaque payload (default: " << kDefaultRoom << ")\n"
              << "  roundtrip.out   optional export path for verification\n";
}

void printMessages(const char *kind, const std::vector<std::string> &messages)
{
    for (const auto &msg : messages) {
        std::cerr << kind << ": " << msg << '\n';
    }
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 2 && std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }

    const std::string inputPath = (argc >= 2) ? argv[1] : kDefaultSch;
    const std::string roomPath = (argc >= 3) ? argv[2] : kDefaultRoom;
    const bool roundtripRequested = argc >= 4;
    const std::string roundtripPath = roundtripRequested ? argv[3] : kDefaultRoundtrip;

    room::XschemImporter importer;
    std::cout << "Importing Xschem file: " << inputPath << '\n';
    room::Database db = importer.importFile(inputPath);
    printMessages("warning", importer.warnings());
    printMessages("error", importer.errors());
    if (!importer.errors().empty()) {
        return 2;
    }

    const std::size_t dot = inputPath.find_last_of('.');
    const std::string ext = dot == std::string::npos ? ".sch" : inputPath.substr(dot);
    const room::ViewType fileView = room::xschem::viewTypeForExtension(ext);
    db.saveToFile(roomPath, fileView);
    std::cout << "Saved ROOM file: " << roomPath << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    if (roundtripRequested) {
        const room::Database reloaded = room::Database::loadFromFile(roomPath);
        std::cout << "Reload check: " << reloaded.lib().cells().size() << " cells\n";

        if (!reloaded.lib().cells().empty()) {
            room::XschemExporter exporter;
            exporter.exportCell(reloaded, reloaded.lib().cells().front().name(), roundtripPath);
            printMessages("warning", exporter.warnings());
            printMessages("error", exporter.errors());
            if (!exporter.errors().empty()) {
                return 3;
            }
            std::cout << "Round-trip Xschem export: " << roundtripPath << '\n';
        }
    }

    return 0;
}
