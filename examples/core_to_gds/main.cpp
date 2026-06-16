#include "database.h"
#include "gds_exporter.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.core> <output.gds>\n";
        return 1;
    }

    const std::string corePath = argv[1];
    const std::string gdsPath = argv[2];

    try {
        const core::Database db = core::Database::loadFromFile(corePath);
        core::GdsExporter exporter;
        exporter.exportFile(db, gdsPath);
        for (const auto &msg : exporter.warnings()) {
            std::cerr << "warning: " << msg << '\n';
        }
        for (const auto &msg : exporter.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        if (!exporter.errors().empty()) {
            return 2;
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 3;
    }

    std::cout << "Exported GDS: " << gdsPath << '\n';
    return 0;
}
