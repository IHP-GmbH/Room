#include "database.h"
#include "oas_exporter.h"
#include "oas_importer.h"

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

std::size_t shapeCount(const room::Database &db)
{
    std::size_t count = 0;
    for (const room::Cell &cell : db.lib().cells()) {
        if (const room::CellContent *content = cell.findContent(room::ViewType::Layout)) {
            count += content->block().shapes().size();
        }
    }
    return count;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.oas> <output.oas>\n"
                  << "  Full-library round-trip: OAS -> ROOM -> OAS.\n";
        return 1;
    }

    const std::string inputOas = argv[1];
    const std::string outputOas = argv[2];
    const std::string roomPath = outputOas + ".room";

    std::cout << "=== OAS -> ROOM -> OAS round-trip ===\n";
    std::cout << "input:  " << inputOas << '\n';
    std::cout << "output: " << outputOas << '\n';
    std::cout << "core:   " << roomPath << "\n\n";

    const Clock::time_point t0 = Clock::now();

    room::OasImporter importer;
    room::Database db = importer.importFile(inputOas);
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
    printMs("  OAS import", t1 - t0);

    db.saveToFile(roomPath, room::ViewType::Layout);
    const Clock::time_point t2 = Clock::now();
    printMs("  ROOM save", t2 - t1);

    room::Database reloaded = room::Database::loadFromFile(roomPath);
    const Clock::time_point t3 = Clock::now();
    printMs("  ROOM load", t3 - t2);

    const std::size_t reloadedCells = cellCount(reloaded);
    std::cout << "Reloaded " << reloadedCells << " cell(s), " << reloaded.lib().layers().size()
              << " layer(s)\n";
    if (reloadedCells != importedCells) {
        std::cerr << "error: cell count mismatch after reload (" << reloadedCells << " vs "
                  << importedCells << ")\n";
        return 4;
    }
    if (reloaded.lib().layers().size() != layerCount) {
        std::cerr << "error: layer count mismatch after reload\n";
        return 5;
    }

    room::OasExporter exporter;
    exporter.exportFile(reloaded, outputOas);
    const Clock::time_point t4 = Clock::now();
    printMs("  OAS export", t4 - t3);

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
    printMs("OAS import", t1 - t0);
    printMs("ROOM save", t2 - t1);
    printMs("ROOM load", t3 - t2);
    printMs("OAS export", t4 - t3);
    printMs("ROOM total (save+load)", t3 - t1);
    printMs("OAS total (import+export)", t4 - t0 - (t3 - t1));
    printMs("Total", t4 - t0);

    std::cout << "\nExported full library (" << reloadedCells << " cells) to " << outputOas << '\n';

    const Clock::time_point t5 = Clock::now();
    room::Database exported = importer.importFile(outputOas);
    const Clock::time_point t6 = Clock::now();
    printMs("  OAS re-import", t6 - t5);

    for (const auto &msg : importer.errors()) {
        std::cerr << "error: " << msg << '\n';
    }
    if (!importer.errors().empty()) {
        return 6;
    }

    const std::size_t exportedCells = cellCount(exported);
    const std::size_t exportedShapes = shapeCount(exported);
    const std::size_t reloadedShapes = shapeCount(reloaded);
    std::cout << "Re-imported " << exportedCells << " cell(s), " << exportedShapes << " shape(s)\n";
    if (exportedCells != reloadedCells) {
        std::cerr << "error: exported cell count mismatch (" << exportedCells << " vs "
                  << reloadedCells << ")\n";
        return 7;
    }
    if (exportedShapes != reloadedShapes) {
        std::cerr << "warning: shape count changed after export (" << exportedShapes << " vs "
                  << reloadedShapes << ")\n";
    }

    std::cout << "Native round-trip OK (" << exportedShapes << " shapes)\n";
    return 0;
}
