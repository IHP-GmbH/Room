#include "database.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "text_dump.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultSch = "examples/qucs_to_core/data/rc_lowpass.sch";
constexpr const char *kDefaultCore = "examples/qucs_to_core/output/schematic.core";
constexpr const char *kDefaultDump = "examples/qucs_to_core/output/schematic.txt";
constexpr const char *kDefaultRoundtrip = "examples/qucs_to_core/output/schematic_roundtrip.sch";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [input.sch] [output.core] [dump.txt] [roundtrip.sch]\n"
              << "\n"
              << "  input.sch      Qucs schematic (default: " << kDefaultSch << ")\n"
              << "  output.core     CORE file (default: " << kDefaultCore << ")\n"
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
    const std::string corePath = (argc >= 3) ? argv[2] : kDefaultCore;
    const bool dumpRequested = argc >= 4;
    const bool roundtripRequested = argc >= 5;
    const std::string dumpPath = dumpRequested ? argv[3] : kDefaultDump;
    const std::string roundtripPath = roundtripRequested ? argv[4] : kDefaultRoundtrip;

    core::QucsImporter::Options importOpts;
    importOpts.libName = "qucs_import";
    core::QucsImporter importer(importOpts);

    std::cout << "Importing Qucs schematic: " << schPath << '\n';
    core::Database db = importer.importFile(schPath);
    printMessages("warning", importer.warnings());
    printMessages("error", importer.errors());
    if (!importer.errors().empty()) {
        return 2;
    }

    db.setGenerator("CORE qucs_to_core");
    db.setTechnology("qucs");

    core::ViewType saveView = core::ViewType::Schematic;
    if (!db.lib().cells().empty()) {
        const core::Cell &cell = db.lib().cells().front();
        if (cell.findContent(core::ViewType::Symbol) != nullptr
            && cell.findContent(core::ViewType::Schematic) == nullptr) {
            saveView = core::ViewType::Symbol;
        }
    }

    db.saveToFile(corePath, saveView);
    std::cout << "Saved CORE file: " << corePath
              << (saveView == core::ViewType::Symbol ? " (symbol)" : " (schematic)") << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    if (dumpRequested) {
        core::TextDumper dumper;
        dumper.dumpToFile(db, dumpPath);
        std::cout << "Text dump: " << dumpPath << '\n';
    }

    if (roundtripRequested) {
        const core::Database reloaded = core::Database::loadFromFile(corePath);
        std::cout << "Reload check: " << reloaded.lib().cells().size() << " cells\n";

        if (!reloaded.lib().cells().empty()) {
            core::QucsExporter exporter;
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
