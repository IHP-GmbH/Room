// Exported Xschem pin boxes keep their size and only move to put the pin center on the
// editor-unit grid, so wires that end on grid points stay connected.

#include "database.h"
#include "xschem_exporter.h"
#include "xschem_importer.h"

#include <iostream>
#include <string>

namespace {

bool exportsPin(const std::string &pin, const std::string &expected)
{
    const std::string symbol = "v {xschem version=3.4.5 file_version=1.2}\nK {type=subcircuit}\n"
                               "B 5 " + pin + " {name=P dir=inout pinnumber=1}\n";
    room::XschemImporter importer;
    const room::Database db = importer.importText(symbol, ".sym", "pin");
    if (!importer.errors().empty()) {
        std::cerr << "import failed: " << importer.errors().front() << '\n';
        return false;
    }
    room::XschemExporter exporter;
    const std::string text = exporter.exportCellToString(db, "pin");
    if (text.find("B 5 " + expected + " {") == std::string::npos) {
        std::cerr << "pin " << pin << " should export as " << expected << ", got:\n" << text << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    // res.sym: half-unit edges, center already on the grid.
    if (!exportsPin("-2.5 -32.5 2.5 -27.5", "-2.5 -32.5 2.5 -27.5")) {
        return 1;
    }
    // Off-grid center snaps to the grid.
    if (!exportsPin("-2.3 -2.3 2.7 2.7", "-2.5 -2.5 2.5 2.5")) {
        return 2;
    }

    std::cout << "xschem pin center OK\n";
    return 0;
}
