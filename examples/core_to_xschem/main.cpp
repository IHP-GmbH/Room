#include "database.h"
#include "xschem_exporter.h"
#include "xschem_importer.h"
#include "xschem_format.h"

#include <fstream>
#include <iostream>
#include <string>

namespace {

void printUsage(const char *prog)
{
    std::cerr << "Usage:\n"
              << "  " << prog << " <input.core> <cell> <output.sch|sym>\n"
              << "  " << prog << " --all <input.core> <output_dir>\n"
              << "  " << prog << " --list-cells <input.core>\n"
              << "  " << prog << " --save <input.sch|sym> <output.core> [--cell name] [--lib name]\n";
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
    if (argc < 2 || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }

    if (std::string(argv[1]) == "--save") {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }

        core::XschemImporter::Options opts;
        const std::string inputPath = argv[2];
        const std::string corePath = argv[3];
        for (int i = 4; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            if (flag == "--cell") {
                opts.cellName = argv[i + 1];
            } else if (flag == "--lib") {
                opts.libName = argv[i + 1];
            }
        }

        core::Database db;
        if (std::ifstream(corePath).good()) {
            db = core::Database::loadFromFile(corePath);
        } else {
            db.lib() = core::Lib(opts.libName);
        }

        core::XschemImporter importer(opts);
        db = importer.importFileInto(std::move(db), inputPath);
        printMessages("warning", importer.warnings());
        printMessages("error", importer.errors());
        if (!importer.errors().empty()) {
            return 2;
        }

        const std::size_t dot = inputPath.find_last_of('.');
        const std::string ext = dot == std::string::npos ? ".sch" : inputPath.substr(dot);
        db.saveToFile(corePath, core::xschem::viewTypeForExtension(ext));
        std::cout << "Saved CORE file: " << corePath << '\n';
        return 0;
    }

    if (std::string(argv[1]) == "--list-cells") {
        if (argc < 3) {
            printUsage(argv[0]);
            return 1;
        }

        const core::Database db = core::Database::loadFromFile(argv[2]);
        for (const auto &cell : db.lib().cells()) {
            std::cout << cell.name() << '\n';
        }
        return 0;
    }

    if (std::string(argv[1]) == "--all") {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }

        const core::Database db = core::Database::loadFromFile(argv[2]);
        core::XschemExporter exporter;
        const std::size_t count = exporter.exportAll(db, argv[3]);
        printMessages("warning", exporter.warnings());
        printMessages("error", exporter.errors());
        if (!exporter.errors().empty()) {
            return 2;
        }
        std::cout << "Exported " << count << " Xschem payload file(s) to " << argv[3] << '\n';
        return 0;
    }

    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    const core::Database db = core::Database::loadFromFile(argv[1]);
    core::XschemExporter exporter;
    exporter.exportCell(db, argv[2], argv[3]);
    printMessages("warning", exporter.warnings());
    printMessages("error", exporter.errors());
    if (!exporter.errors().empty()) {
        return 2;
    }

    std::cout << "Exported cell " << argv[2] << " to " << argv[3] << '\n';
    return 0;
}
