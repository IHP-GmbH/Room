#include "database.h"
#include "em_lookalike_symbol.h"
#include "enums.h"
#include "room_paths.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

void ensureDirectory(const std::string &dirPath)
{
#ifdef _WIN32
    _mkdir(dirPath.c_str());
#else
    mkdir(dirPath.c_str(), 0755);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string outDir = argc > 1 ? argv[1] : std::string("build/tests");
    ensureDirectory(outDir);

    const std::string layoutPath = outDir + "/lookalike_dut.layout.room";
    const std::string symbolPath = outDir + "/lookalike_dut.symbol.room";

    {
        room::Database db;
        db.setGenerator("em_lookalike_symbol_unit");
        room::Cell &cell = db.lib().getOrCreateCell("lookalike_dut");
        room::CellContent &layout = cell.getOrCreateContent(room::ViewType::Layout, 1000.0);
        room::Shape::RectData metal;
        metal.layerId = 0;
        metal.box = room::Box(0, 0, 10000, 4000); // 10 µm × 4 µm at 1000 dbu/µm
        layout.block().shapes().push_back(room::Shape(metal));
        layout.block().recomputeBBox();
        db.saveToFile(layoutPath, room::ViewType::Layout);
    }

    room::EmLookalikeInput in;
    in.cellName = "lookalike_dut";
    in.outputPath = symbolPath;
    std::string err;
    if (!room::fillLookalikeOutlineFromLayoutRoom(layoutPath, "lookalike_dut", in, &err)) {
        std::cerr << "fillLookalikeOutlineFromLayoutRoom failed: " << err << '\n';
        return 1;
    }
    if (!in.hasBBox || in.outlinePolysUm.empty()) {
        std::cerr << "outline empty after layout load\n";
        return 2;
    }

    in.ports.push_back({"P1", 1, 0.0, 2.0, true});
    in.ports.push_back({"P2", 2, 10.0, 2.0, true});

    err = room::writeLookalikeSymbolRoom(in);
    if (!err.empty()) {
        std::cerr << err << '\n';
        return 3;
    }
    if (!room::isViewRoomFile(symbolPath, room::ViewType::Symbol)) {
        std::cerr << "symbol path not recognized\n";
        return 4;
    }

    const room::Database loaded = room::Database::loadFromFile(symbolPath);
    if (loaded.fileView() != room::ViewType::Symbol) {
        std::cerr << "fileView is not Symbol\n";
        return 5;
    }
    const room::Cell *cell = loaded.lib().findCell("lookalike_dut");
    if (cell == nullptr) {
        std::cerr << "cell missing\n";
        return 6;
    }
    const room::CellContent *sym = cell->findContent(room::ViewType::Symbol);
    if (sym == nullptr) {
        std::cerr << "symbol content missing\n";
        return 7;
    }

    int pinCount = 0;
    int drawingCount = 0;
    for (const room::Shape &shape : sym->block().shapes()) {
        if (shape.type() != room::Shape::Type::Rect && shape.type() != room::Shape::Type::Polygon)
            continue;
        bool isPin = false;
        for (const room::Property &p : shape.properties()) {
            if (p.name == "pinnumber") {
                isPin = true;
                break;
            }
        }
        if (isPin)
            ++pinCount;
        else
            ++drawingCount;
    }
    if (pinCount != 2) {
        std::cerr << "expected 2 pins, got " << pinCount << '\n';
        return 8;
    }
    if (drawingCount < 1) {
        std::cerr << "expected lookalike body geometry\n";
        return 9;
    }
    if (sym->block().nets().size() != 2) {
        std::cerr << "expected 2 nets\n";
        return 10;
    }

    bool hasLookalike = false;
    for (const room::Property &p : sym->properties()) {
        if (p.name == "em.lookalike" && p.value == "1")
            hasLookalike = true;
    }
    if (!hasLookalike) {
        std::cerr << "em.lookalike property missing\n";
        return 11;
    }

    std::cout << "em_lookalike_symbol_unit OK\n";
    return 0;
}
