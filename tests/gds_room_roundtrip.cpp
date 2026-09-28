#include "database.h"
#include "gds_exporter.h"
#include "gds_importer.h"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

void printMs(const char *label, Clock::duration duration)
{
    const double ms = std::chrono::duration<double, std::milli>(duration).count();
    std::cout << label << ": " << std::fixed << std::setprecision(3) << ms << " ms\n";
}

std::size_t cellCount(const room::Database &db)
{
    return db.lib().cells().size();
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.gds> <output.gds>\n"
                  << "  Full-library round-trip: GDS -> ROOM -> GDS.\n";
        return 1;
    }

    const std::string inputGds = argv[1];
    const std::string outputGds = argv[2];
    const std::string roomPath = outputGds + ".room";

    std::cout << "=== GDS -> ROOM -> GDS round-trip ===\n";
    std::cout << "input:  " << inputGds << '\n';
    std::cout << "output: " << outputGds << '\n';
    std::cout << "core:   " << roomPath << "\n\n";

    const Clock::time_point t0 = Clock::now();

    room::GdsImporter importer;
    room::Database db = importer.importFile(inputGds);
    const Clock::time_point t1 = Clock::now();

    for (const auto &msg : importer.warnings()) {
        std::cerr << "warning: " << msg << '\n';
    }
    for (const auto &msg : importer.errors()) {
        std::cerr << "error: " << msg << '\n';
    }
    if (!importer.errors().empty()) {
        return 2;
    }

    const std::size_t importedCells = cellCount(db);
    const std::size_t layerCount = db.lib().layers().size();
    std::cout << "Imported " << importedCells << " cell(s), " << layerCount << " layer(s)\n";
    printMs("  GDS import", t1 - t0);

    db.saveToFile(roomPath, room::ViewType::Layout);
    const Clock::time_point t2 = Clock::now();
    printMs("  ROOM save", t2 - t1);

    room::Database reloaded = room::Database::loadFromFile(roomPath);
    const Clock::time_point t3 = Clock::now();
    printMs("  ROOM load", t3 - t2);

    const std::size_t reloadedCells = cellCount(reloaded);
    std::cout << "Reloaded " << reloadedCells << " cell(s), " << reloaded.lib().layers().size()
              << " layer(s)\n";
    if (reloaded.lib().hasIndex()) {
        std::cout << "index: top cells=" << reloaded.lib().index().topCells.size()
                  << " placements=" << reloaded.lib().index().placementCount << '\n';
    }
    if (reloadedCells != importedCells) {
        std::cerr << "error: cell count mismatch after reload (" << reloadedCells << " vs "
                  << importedCells << ")\n";
        return 4;
    }
    if (reloaded.lib().layers().size() != layerCount) {
        std::cerr << "error: layer count mismatch after reload\n";
        return 5;
    }

    room::GdsExporter exporter;
    exporter.exportFile(reloaded, outputGds);
    const Clock::time_point t4 = Clock::now();
    printMs("  GDS export", t4 - t3);

    for (const auto &msg : exporter.warnings()) {
        std::cerr << "warning: " << msg << '\n';
    }
    for (const auto &msg : exporter.errors()) {
        std::cerr << "error: " << msg << '\n';
    }
    if (!exporter.errors().empty()) {
        return 3;
    }

    std::cout << "\n=== timing summary ===\n";
    printMs("GDS import", t1 - t0);
    printMs("ROOM save", t2 - t1);
    printMs("ROOM load", t3 - t2);
    printMs("GDS export", t4 - t3);
    printMs("ROOM total (save+load)", t3 - t1);
    printMs("GDS total (import+export)", t4 - t0 - (t3 - t1));
    printMs("Total", t4 - t0);

    std::cout << "\nExported full library (" << reloadedCells << " cells) to " << outputGds << '\n';

    return 0;
}
