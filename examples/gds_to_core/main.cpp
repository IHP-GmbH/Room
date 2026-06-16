#include "database.h"
#include "gds_importer.h"
#include "text_dump.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace {

constexpr const char *kDefaultGds = "examples/gds_to_core/data/sg13g2_stdcell.gds";
constexpr const char *kDefaultCore = "examples/gds_to_core/output/sg13g2_inv_2.core";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [--all-cells] <input.gds|dir> <output.core> [dump.txt] [cell-name]\n"
              << "\n"
              << "  input       GDSII file or directory of *.gds files\n"
              << "  output.core CORE file path\n"
              << "  dump.txt    optional text dump (default: output name with .txt)\n"
              << "  cell-name   optional single cell to keep (default: auto-detect inv2)\n"
              << "  --all-cells import every cell (skip inv2 filter)\n"
              << "\n"
              << "With no arguments, runs the bundled sg13g2 demo (single inv2 cell).\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " --all-cells C:/pdk/stdcell/gds out/stdcell.core\n"
              << "  " << prog << " data/chip.gds out/layout.core out/layout.txt TOP\n";
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

bool isGdsExtension(const std::string &extLower)
{
    return extLower == ".gds" || extLower == ".gds2";
}

bool looksLikeGdsFile(const std::string &path)
{
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    return isGdsExtension(toLower(path.substr(dot)));
}

std::vector<std::string> collectGdsInputs(const std::string &input)
{
    std::vector<std::string> files;
    if (looksLikeGdsFile(input)) {
        files.push_back(input);
        return files;
    }

#ifdef _WIN32
    const std::string pattern = input;
    const std::string glob = (pattern.empty() || pattern.back() == '\\' || pattern.back() == '/')
                                 ? pattern + "*.gds"
                                 : pattern + "\\*.gds";
    WIN32_FIND_DATAA entry{};
    const HANDLE handle = FindFirstFileA(glob.c_str(), &entry);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                const char sep = (pattern.empty() || pattern.back() == '\\' || pattern.back() == '/') ? '\0' : '\\';
                files.push_back(sep ? pattern + sep + entry.cFileName : pattern + entry.cFileName);
            }
        } while (FindNextFileA(handle, &entry));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(input.c_str());
    if (dir != nullptr) {
        while (dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (looksLikeGdsFile(name)) {
                files.push_back(input + "/" + name);
            }
        }
        closedir(dir);
    }
#endif

    std::sort(files.begin(), files.end());
    return files;
}

void appendImportedLib(core::Database &target, core::Database &&source)
{
    for (core::Cell &cell : source.lib().cells()) {
        if (target.lib().findCell(cell.name()) != nullptr) {
            std::cerr << "warning: duplicate cell skipped: " << cell.name() << "\n";
            continue;
        }
        target.lib().cells().push_back(std::move(cell));
    }

    for (const core::LayerSpec &layer : source.lib().layers()) {
        const auto exists = std::any_of(target.lib().layers().begin(), target.lib().layers().end(),
                                        [&](const core::LayerSpec &existing) {
                                            return existing.layerNum == layer.layerNum &&
                                                   existing.dataType == layer.dataType;
                                        });
        if (!exists) {
            target.lib().layers().push_back(layer);
        }
    }
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

    bool allCells = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all-cells") {
            allCells = true;
        } else {
            positional.push_back(arg);
        }
    }

    const bool useDefaults = positional.empty();
    const std::string inputPath = useDefaults ? kDefaultGds : positional[0];
    const std::string corePath = useDefaults ? kDefaultCore : positional[1];
    const std::string dumpPath =
        useDefaults ? defaultDumpPath(kDefaultCore)
                    : ((positional.size() >= 3) ? positional[2] : defaultDumpPath(corePath));
    const std::string requestedCell = (positional.size() >= 4) ? positional[3] : std::string{};

    if (!useDefaults && positional.size() < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const std::vector<std::string> gdsFiles = collectGdsInputs(inputPath);
    if (gdsFiles.empty()) {
        std::cerr << "No GDS files found: " << inputPath << "\n";
        return 1;
    }

    core::GdsImporter::Options opts;
    opts.libName = "sg13g2_stdcell";
    core::GdsImporter importer(opts);

    std::cout << "Importing GDS: " << gdsFiles.front() << "\n";
    core::Database db = importer.importFile(gdsFiles.front());
    for (std::size_t i = 1; i < gdsFiles.size(); ++i) {
        std::cout << "Importing GDS: " << gdsFiles[i] << "\n";
        appendImportedLib(db, importer.importFile(gdsFiles[i]));
    }

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
    if (!allCells) {
        if (cellName.empty()) {
            cellName = findInv2CellName(db.lib());
        } else if (db.lib().findCell(cellName) == nullptr) {
            std::cerr << "Cell not found: " << cellName << "\n";
            return 3;
        }

        if (!cellName.empty() && !keepSingleCell(db, cellName)) {
            std::cerr << "Cell not found in " << inputPath << ": " << cellName << "\n";
            std::cerr << "Available inv* cells:\n";
            for (const std::string &name : listInvCells(db.lib())) {
                std::cerr << "  " << name << "\n";
            }
            return 3;
        }
    } else {
        cellName.clear();
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
