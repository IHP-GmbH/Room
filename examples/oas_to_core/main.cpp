#include "database.h"
#include "oas_importer.h"
#include "text_dump.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kDefaultOas = "examples/oas_to_core/data/sample.oas";
constexpr const char *kDefaultCore = "examples/oas_to_core/output/layout.core";

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <input.oas|dir> <output.core> [dump.txt]\n"
              << "\n"
              << "  input       OASIS file or directory of *.oas files\n"
              << "  output.core CORE layout file path\n"
              << "  dump.txt    optional text dump\n";
}

std::string defaultDumpPath(const std::string &corePath)
{
    if (corePath.size() >= 5 && corePath.compare(corePath.size() - 5, 5, ".core") == 0) {
        return corePath.substr(0, corePath.size() - 5) + ".txt";
    }
    return corePath + ".txt";
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 2 && std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string corePath = argv[2];
    const std::string dumpPath = (argc >= 4) ? argv[3] : defaultDumpPath(corePath);

    core::OasImporter::Options opts;
    opts.libName = "oas_import";
    core::OasImporter importer(opts);

    std::cout << "Importing OASIS: " << inputPath << '\n';
    core::Database db = importer.importFile(inputPath);
    for (const auto &w : importer.warnings()) {
        std::cerr << "warning: " << w << '\n';
    }
    for (const auto &e : importer.errors()) {
        std::cerr << "error: " << e << '\n';
    }
    if (!importer.errors().empty()) {
        return 2;
    }

    db.setGenerator("CORE oas_to_core");
    db.saveToFile(corePath, core::ViewType::Layout);
    std::cout << "Saved CORE file: " << corePath << '\n';
    std::cout << "  cells: " << db.lib().cells().size() << '\n';

    core::TextDumper dumper;
    dumper.dumpToFile(db, dumpPath);
    std::cout << "Text dump: " << dumpPath << '\n';

    return 0;
}
