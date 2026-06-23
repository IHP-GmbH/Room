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

std::size_t cellCount(const core::Database &db)
{
    return db.lib().cells().size();
}

std::size_t shapeCount(const core::Database &db)
{
    std::size_t count = 0;
    for (const core::Cell &cell : db.lib().cells()) {
        if (const core::CellContent *content = cell.findContent(core::ViewType::Layout)) {
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
                  << "  Full-library round-trip: OAS -> CORE -> OAS.\n";
        return 1;
    }

    const std::string inputOas = argv[1];
    const std::string outputOas = argv[2];
    const std::string corePath = outputOas + ".core";

    std::cout << "=== OAS -> CORE -> OAS round-trip ===\n";
    std::cout << "input:  " << inputOas << '\n';
    std::cout << "output: " << outputOas << '\n';
    std::cout << "core:   " << corePath << "\n\n";

    const Clock::time_point t0 = Clock::now();

    core::OasImporter importer;
    core::Database db = importer.importFile(inputOas);
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

    db.saveToFile(corePath, core::ViewType::Layout);
    const Clock::time_point t2 = Clock::now();
    printMs("  CORE save", t2 - t1);

    core::Database reloaded = core::Database::loadFromFile(corePath);
    const Clock::time_point t3 = Clock::now();
    printMs("  CORE load", t3 - t2);

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

    core::OasExporter exporter;
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
    printMs("CORE save", t2 - t1);
    printMs("CORE load", t3 - t2);
    printMs("OAS export", t4 - t3);
    printMs("CORE total (save+load)", t3 - t1);
    printMs("OAS total (import+export)", t4 - t0 - (t3 - t1));
    printMs("Total", t4 - t0);

    std::cout << "\nExported full library (" << reloadedCells << " cells) to " << outputOas << '\n';

    const Clock::time_point t5 = Clock::now();
    core::Database exported = importer.importFile(outputOas);
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
