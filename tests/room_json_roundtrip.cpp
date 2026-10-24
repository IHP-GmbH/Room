#include "database.h"
#include "room_json_io.h"
#include "shape.h"

#include <cmath>
#include <cstdio>
#include <fstream>
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

room::Database makeSchematicFixture()
{
    room::Database db;
    db.setGenerator("room_json_roundtrip");
    db.lib().setName("JsonTest");
    room::Cell &cell = db.lib().getOrCreateCell("inv_tb");
    room::CellContent &sch = cell.getOrCreateContent(room::ViewType::Schematic, 1000.0);
    sch.setDbuPerEditorUnit(1.0);
    sch.layers().push_back(room::LayerSpec(1, 0, "wire", room::LayerPurpose::Wire));
    sch.layers().push_back(room::LayerSpec(2, 0, "pin", room::LayerPurpose::Pin));
    sch.properties().emplace_back("schematic.source", "unit-test");

    room::Shape::PathData wire;
    wire.layerId = 0;
    wire.width = 20;
    wire.points = {room::Point(0, 0), room::Point(1000, 0)};
    sch.block().shapes().push_back(room::Shape(wire));

    room::Instance inst("inverter", room::Transform(500, 200, room::Orient::R0, 1.0));
    inst.properties().emplace_back("name", "X1");
    sch.block().instances().push_back(std::move(inst));

    room::Net net("out", room::SigType::Signal);
    net.terms().emplace_back("out", 1, room::Point(1000, 0));
    sch.block().nets().push_back(std::move(net));
    sch.block().recomputeBBox();
    return db;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string outDir = argc > 1 ? argv[1] : std::string("build/tests");
    ensureDirectory(outDir);

    const std::string room1 = outDir + "/inv_tb.schematic.room";
    const std::string json1 = outDir + "/inv_tb.schematic.json";
    const std::string room2 = outDir + "/inv_tb_from_json.schematic.room";
    const std::string json2 = outDir + "/inv_tb_roundtrip.schematic.json";

    {
        room::Database db = makeSchematicFixture();
        db.saveToFile(room1, room::ViewType::Schematic);
    }

    room::RoomJsonIo::Result r1 = room::RoomJsonIo::roomFileToJson(room1, json1);
    if (!r1.ok) {
        std::cerr << "export failed: " << r1.message << '\n';
        return 1;
    }

    room::RoomJsonIo::Result r2 = room::RoomJsonIo::jsonFileToRoom(json1, room2);
    if (!r2.ok) {
        std::cerr << "import failed: " << r2.message << '\n';
        return 2;
    }

    room::Database reloaded = room::Database::loadFromFile(room2);
    const room::Cell *cell = reloaded.lib().findCell("inv_tb");
    if (cell == nullptr) {
        std::cerr << "missing cell after import\n";
        return 3;
    }
    const room::CellContent *sch = cell->findContent(room::ViewType::Schematic);
    if (sch == nullptr) {
        std::cerr << "missing schematic view\n";
        return 4;
    }
    if (sch->block().instances().size() != 1 || sch->block().shapes().size() != 1 ||
        sch->block().nets().size() != 1) {
        std::cerr << "geometry counts mismatch: shapes=" << sch->block().shapes().size()
                  << " instances=" << sch->block().instances().size()
                  << " nets=" << sch->block().nets().size() << '\n';
        return 5;
    }
    if (sch->block().instances().front().cellName() != "inverter") {
        std::cerr << "instance cellName mismatch\n";
        return 6;
    }
    if (std::abs(sch->dbuPerEditorUnit() - 1.0) > 1e-9) {
        std::cerr << "dbuPerEditorUnit mismatch\n";
        return 7;
    }

    room::RoomJsonIo::Result r3 = room::RoomJsonIo::exportToFile(reloaded, "inv_tb", room::ViewType::Schematic, json2);
    if (!r3.ok) {
        std::cerr << "second export failed: " << r3.message << '\n';
        return 8;
    }

    // Reject layout
    {
        room::Database layoutDb;
        room::Cell &c = layoutDb.lib().getOrCreateCell("L");
        c.getOrCreateContent(room::ViewType::Layout, 1000.0);
        layoutDb.saveToFile(outDir + "/L.layout.room", room::ViewType::Layout);
        room::RoomJsonIo::Result bad =
            room::RoomJsonIo::roomFileToJson(outDir + "/L.layout.room", outDir + "/L.json");
        if (bad.ok) {
            std::cerr << "layout export should fail\n";
            return 9;
        }
    }

    // In-place convert: same *.room path, JSON ↔ binary via isJsonRoom
    {
        const std::string inplace = outDir + "/inv_tb_inplace.schematic.room";
        {
            room::Database db = makeSchematicFixture();
            db.saveToFile(inplace, room::ViewType::Schematic);
        }
        if (room::RoomJsonIo::isJsonRoom(inplace)) {
            std::cerr << "binary .room should not sniff as JSON\n";
            return 10;
        }
        room::RoomJsonIo::Result toJson = room::RoomJsonIo::convertInPlaceToJson(inplace);
        if (!toJson.ok || !room::RoomJsonIo::isJsonRoom(inplace)) {
            std::cerr << "convertInPlaceToJson failed: " << toJson.message << '\n';
            return 11;
        }
        room::Database fromJson;
        room::RoomJsonIo::Result loaded = room::RoomJsonIo::loadRoomOrJson(inplace, fromJson);
        if (!loaded.ok || fromJson.lib().findCell("inv_tb") == nullptr) {
            std::cerr << "loadRoomOrJson JSON-in-.room failed\n";
            return 12;
        }
        room::RoomJsonIo::Result toBin = room::RoomJsonIo::convertInPlaceToBinary(inplace);
        if (!toBin.ok || room::RoomJsonIo::isJsonRoom(inplace)) {
            std::cerr << "convertInPlaceToBinary failed: " << toBin.message << '\n';
            return 13;
        }
    }

    std::cout << "room_json_roundtrip OK\n";
    return 0;
}
