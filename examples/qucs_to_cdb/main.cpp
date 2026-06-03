#include "database.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "text_dump.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultSch = "examples/qucs_to_cdb/data/rc_lowpass.sch";
constexpr const char *kDefaultCdb = "examples/qucs_to_cdb/output/schematic.cdb";
constexpr const char *kDefaultDump = "examples/qucs_to_cdb/output/schematic.txt";
constexpr const char *kDefaultRoundtrip = "examples/qucs_to_cdb/output/schematic_roundtrip.sch";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [input.sch] [output.cdb] [dump.txt] [roundtrip.sch]\n"
              << "\n"
              << "  input.sch      Qucs schematic (default: " << kDefaultSch << ")\n"
              << "  output.cdb     CommonDB file (default: " << kDefaultCdb << ")\n"
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
    const std::string cdbPath = (argc >= 3) ? argv[2] : kDefaultCdb;
    const std::string dumpPath = (argc >= 4) ? argv[3] : kDefaultDump;
    const std::string roundtripPath = (argc >= 5) ? argv[4] : kDefaultRoundtrip;

    cdb::QucsImporter::Options importOpts;
    importOpts.libName = "qucs_import";
    cdb::QucsImporter importer(importOpts);

    std::cout << "Importing Qucs schematic: " << schPath << '\n';
    cdb::Database db = importer.importFile(schPath);
    printMessages("warning", importer.warnings());
    printMessages("error", importer.errors());
    if (!importer.errors().empty()) {
        return 2;
    }

    db.setGenerator("CommonDB qucs_to_cdb");
    db.setTechnology("qucs");

    db.saveToFile(cdbPath);
    std::cout << "Saved CommonDB file: " << cdbPath << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    cdb::TextDumper dumper;
    dumper.dumpToFile(db, dumpPath);
    std::cout << "Text dump: " << dumpPath << '\n';

    const cdb::Database reloaded = cdb::Database::loadFromFile(cdbPath);
    std::cout << "Reload check: " << reloaded.lib().cells().size() << " cells\n";

    if (!reloaded.lib().cells().empty()) {
        cdb::QucsExporter exporter;
        exporter.exportCell(reloaded, reloaded.lib().cells().front().name(), roundtripPath);
        printMessages("warning", exporter.warnings());
        printMessages("error", exporter.errors());
        if (!exporter.errors().empty()) {
            return 3;
        }
        std::cout << "Round-trip Qucs export: " << roundtripPath << '\n';
    }

    return 0;
}
