#include "database.h"
#include "xschem_exporter.h"
#include "xschem_importer.h"
#include "xschem_format.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultSch = "examples/xschem_to_core/data/test.sch";
constexpr const char *kDefaultCore = "examples/xschem_to_core/output/schematic.core";
constexpr const char *kDefaultRoundtrip = "examples/xschem_to_core/output/schematic_roundtrip.sch";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [input.sch|sym] [output.core] [roundtrip.out]\n"
              << "\n"
              << "  input.sch|sym   Xschem schematic or symbol (default: " << kDefaultSch << ")\n"
              << "  output.core     CORE file with opaque payload (default: " << kDefaultCore << ")\n"
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
    const std::string corePath = (argc >= 3) ? argv[2] : kDefaultCore;
    const std::string roundtripPath = (argc >= 4) ? argv[3] : kDefaultRoundtrip;

    core::XschemImporter importer;
    std::cout << "Importing Xschem file: " << inputPath << '\n';
    core::Database db = importer.importFile(inputPath);
    printMessages("warning", importer.warnings());
    printMessages("error", importer.errors());
    if (!importer.errors().empty()) {
        return 2;
    }

    const std::size_t dot = inputPath.find_last_of('.');
    const std::string ext = dot == std::string::npos ? ".sch" : inputPath.substr(dot);
    const core::ViewType fileView = core::xschem::viewTypeForExtension(ext);
    db.saveToFile(corePath, fileView);
    std::cout << "Saved CORE file: " << corePath << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    const core::Database reloaded = core::Database::loadFromFile(corePath);
    std::cout << "Reload check: " << reloaded.lib().cells().size() << " cells\n";

    if (!reloaded.lib().cells().empty()) {
        core::XschemExporter exporter;
        exporter.exportCell(reloaded, reloaded.lib().cells().front().name(), roundtripPath);
        printMessages("warning", exporter.warnings());
        printMessages("error", exporter.errors());
        if (!exporter.errors().empty()) {
            return 3;
        }
        std::cout << "Round-trip Xschem export: " << roundtripPath << '\n';
    }

    return 0;
}
