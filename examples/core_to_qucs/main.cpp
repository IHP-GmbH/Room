#include "database.h"
#include "qucs_exporter.h"

#include <iostream>
#include <string>

namespace {

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <input.core> <cell> <output.sch>\n";
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

    if (argc != 4) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string corePath = argv[1];
    const std::string cellName = argv[2];
    const std::string schPath = argv[3];

    try {
        const core::Database db = core::Database::loadFromFile(corePath);
        core::QucsExporter exporter;
        exporter.exportCell(db, cellName, schPath);
        printMessages("warning", exporter.warnings());
        printMessages("error", exporter.errors());
        if (!exporter.errors().empty()) {
            return 2;
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 3;
    }

    std::cout << "Exported cell " << cellName << " to " << schPath << '\n';
    return 0;
}
