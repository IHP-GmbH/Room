#include "database.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "text_dump.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultSch = "examples/qucs_to_room/data/rc_lowpass.sch";
constexpr const char *kDefaultRoom = "examples/qucs_to_room/output/schematic.room";
constexpr const char *kDefaultDump = "examples/qucs_to_room/output/schematic.txt";
constexpr const char *kDefaultRoundtrip = "examples/qucs_to_room/output/schematic_roundtrip.sch";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [input.sch] [output.room] [dump.txt] [roundtrip.sch]\n"
              << "\n"
              << "  input.sch      Qucs schematic (default: " << kDefaultSch << ")\n"
              << "  output.room     ROOM file (default: " << kDefaultRoom << ")\n"
              << "  dump.txt       text dump (default: " << kDefaultDump << ")\n"
              << "  roundtrip.sch  export schematic back to Qucs format\n";
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

    const std::string schPath = (argc >= 2) ? argv[1] : kDefaultSch;
    const std::string roomPath = (argc >= 3) ? argv[2] : kDefaultRoom;
    const bool dumpRequested = argc >= 4;
    const bool roundtripRequested = argc >= 5;
    const std::string dumpPath = dumpRequested ? argv[3] : kDefaultDump;
    const std::string roundtripPath = roundtripRequested ? argv[4] : kDefaultRoundtrip;

    room::QucsImporter::Options importOpts;
    importOpts.libName = "qucs_import";
    room::QucsImporter importer(importOpts);

    std::cout << "Importing Qucs schematic: " << schPath << '\n';
    room::Database db = importer.importFile(schPath);
    printMessages("warning", importer.warnings());
    printMessages("error", importer.errors());
    if (!importer.errors().empty()) {
        return 2;
    }

    db.setGenerator("ROOM qucs_to_room");
    db.setTechnology("qucs");

    room::ViewType saveView = room::ViewType::Schematic;
    if (!db.lib().cells().empty()) {
        const room::Cell &cell = db.lib().cells().front();
        if (cell.findContent(room::ViewType::Symbol) != nullptr
            && cell.findContent(room::ViewType::Schematic) == nullptr) {
            saveView = room::ViewType::Symbol;
        }
    }

    db.saveToFile(roomPath, saveView);
    std::cout << "Saved ROOM file: " << roomPath
              << (saveView == room::ViewType::Symbol ? " (symbol)" : " (schematic)") << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    if (dumpRequested) {
        room::TextDumper dumper;
        dumper.dumpToFile(db, dumpPath);
        std::cout << "Text dump: " << dumpPath << '\n';
    }

    if (roundtripRequested) {
        const room::Database reloaded = room::Database::loadFromFile(roomPath);
        std::cout << "Reload check: " << reloaded.lib().cells().size() << " cells\n";

        if (!reloaded.lib().cells().empty()) {
            room::QucsExporter exporter;
            exporter.exportCell(reloaded, reloaded.lib().cells().front().name(), roundtripPath);
            printMessages("warning", exporter.warnings());
            printMessages("error", exporter.errors());
            if (!exporter.errors().empty()) {
                return 3;
            }
            std::cout << "Round-trip Qucs export: " << roundtripPath << '\n';
        }
    }

    return 0;
}
