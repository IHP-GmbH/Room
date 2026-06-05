#include "database.h"
#include "gds_importer.h"
#include "text_dump.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char *kDefaultGds = "examples/gds_to_core/data/sg13g2_stdcell.gds";
constexpr const char *kDefaultCore = "examples/gds_to_core/output/sg13g2_inv_2.core";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <input.gds> <output.core> [dump.txt] [cell-name]\n"
              << "\n"
              << "  input.gds   GDSII file to import\n"
              << "  output.core  CORE file path (any directory/name, e.g. layout.core)\n"
              << "  dump.txt    optional text dump (default: output name with .txt)\n"
              << "  cell-name   optional cell to keep (default: auto-detect inv2)\n"
              << "\n"
              << "With no arguments, runs the bundled sg13g2 demo.\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " data/chip.gds out/layout.core out/layout.txt TOP\n"
              << "  " << prog << " data/chip.gds D:/design/schematic.core\n";
}

std::string defaultDumpPath(const std::string &corePath)
{
    if (corePath.size() >= 5 && corePath.compare(corePath.size() - 5, 5, ".core") == 0) {
        return corePath.substr(0, corePath.size() - 5) + ".txt";
    }
    return corePath + ".txt";
}

std::string toLower(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool endsWith(const std::string &value, const std::string &suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string findInv2CellName(const core::Lib &lib)
{
    if (const core::Cell *cell = lib.findCell("sg13g2_inv_2")) {
        return cell->name();
    }

    for (const core::Cell &cell : lib.cells()) {
        const std::string lower = toLower(cell.name());
        if (lower == "inv2" || endsWith(lower, "_inv_2") || lower.find("inv_2") != std::string::npos) {
            return cell.name();
        }
    }

    return {};
}

std::vector<std::string> listInvCells(const core::Lib &lib)
{
    std::vector<std::string> names;
    for (const core::Cell &cell : lib.cells()) {
        if (toLower(cell.name()).find("inv") != std::string::npos) {
            names.push_back(cell.name());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool keepSingleCell(core::Database &db, const std::string &cellName)
{
    std::vector<core::Cell> &cells = db.lib().cells();
    const auto it = std::find_if(cells.begin(), cells.end(), [&](const core::Cell &cell) {
        return cell.name() == cellName;
    });
    if (it == cells.end()) {
        return false;
    }

    core::Cell kept = std::move(*it);
    cells.clear();
    cells.push_back(std::move(kept));
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 2 && std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }
    if (argc == 2) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string gdsPath = (argc >= 2) ? argv[1] : kDefaultGds;
    const std::string corePath = (argc >= 3) ? argv[2] : kDefaultCore;
    const std::string dumpPath = (argc >= 4) ? argv[3] : defaultDumpPath(corePath);
    const std::string requestedCell = (argc >= 5) ? argv[4] : std::string{};

    core::GdsImporter::Options opts;
    opts.libName = "gds_import";
    core::GdsImporter importer(opts);

    std::cout << "Importing GDS: " << gdsPath << "\n";
    core::Database db = importer.importFile(gdsPath);

    for (const auto &w : importer.warnings()) {
        std::cerr << "warning: " << w << "\n";
    }
    for (const auto &e : importer.errors()) {
        std::cerr << "error: " << e << "\n";
    }
    if (!importer.errors().empty()) {
        return 2;
    }

    std::string cellName = requestedCell;
    if (cellName.empty()) {
        cellName = findInv2CellName(db.lib());
    } else if (db.lib().findCell(cellName) == nullptr) {
        std::cerr << "Cell not found: " << cellName << "\n";
        return 3;
    }

    if (!cellName.empty()) {
        if (!keepSingleCell(db, cellName)) {
            std::cerr << "Cell inv2 not found in " << gdsPath << "\n";
            std::cerr << "Available inv* cells:\n";
            for (const std::string &name : listInvCells(db.lib())) {
                std::cerr << "  " << name << "\n";
            }
            return 3;
        }
    }

    db.setGenerator("CORE gds_to_core");
    db.setTechnology("sg13g2");

    db.saveToFile(corePath);
    std::cout << "Saved CORE file: " << corePath << "\n";
    if (!cellName.empty()) {
        std::cout << "  cell: " << cellName << "\n";
    } else {
        std::cout << "  cells: " << db.lib().cells().size() << "\n";
    }

    core::TextDumper dumper;
    dumper.dumpToFile(db, dumpPath);
    std::cout << "Text dump: " << dumpPath << "\n";

    dumper.dump(db, std::cout);

    return 0;
}
