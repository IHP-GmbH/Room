#include "database.h"
#include "file_summary.h"
#include "xschem_importer.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    const std::string path = (argc > 1) ? argv[1] : "examples/xschem_to_core/data/test.sch";
    const std::string corePath = (argc > 2) ? argv[2] : "build/tests/sniff_fixture.schematic.core";

    core::XschemImporter importer;
    core::Database db = importer.importFile(path);
    if (!importer.errors().empty()) {
        return 1;
    }
    db.saveToFile(corePath, core::ViewType::Schematic);

    const core::CoreFileInfo info = core::sniffCoreFile(corePath);
    if (info.summary.view != core::ViewType::Schematic) {
        std::cerr << "error: unexpected view\n";
        return 3;
    }

    std::cout << "sniff OK view=" << core::viewTypeToString(info.summary.view) << '\n';
    return 0;
}
