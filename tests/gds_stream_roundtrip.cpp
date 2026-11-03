#include "database.h"
#include "gds_importer.h"
#include "gds_stream_import.h"

#include <iostream>
#include <string>

namespace {

std::size_t shapeCount(const room::Database &db)
{
    std::size_t count = 0;
    for (const room::Cell &cell : db.lib().cells()) {
        for (const room::CellContent &content : cell.contents()) {
            count += content.block().shapes().size();
        }
    }
    return count;
}

std::size_t instanceCount(const room::Database &db)
{
    std::size_t count = 0;
    for (const room::Cell &cell : db.lib().cells()) {
        for (const room::CellContent &content : cell.contents()) {
            count += content.block().instances().size();
        }
    }
    return count;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string gdsPath = argc > 1 ? argv[1] : "testdata/sample.gds";
    const std::string roomPath = argc > 2 ? argv[2] : "build/tests/gds_stream_roundtrip.room";

    room::GdsImporter importer;
    const room::Database original = importer.importFile(gdsPath);
    if (!importer.errors().empty()) {
        for (const auto &error : importer.errors()) {
            std::cerr << "error: " << error << '\n';
        }
        return 1;
    }

    const room::GdsStreamImportResult streamed = room::importGdsStreamToRoom(gdsPath, roomPath);
    if (!streamed.errors.empty()) {
        for (const auto &error : streamed.errors) {
            std::cerr << "error: " << error << '\n';
        }
        return 2;
    }

    const room::Database loaded = room::Database::loadFromFile(roomPath);
    const room::GdsStreamStats stats = room::readCompactRoomStats(roomPath);

    const std::size_t originalShapes = shapeCount(original);
    const std::size_t originalInstances = instanceCount(original);
    const std::size_t loadedShapes = shapeCount(loaded);
    const std::size_t loadedInstances = instanceCount(loaded);
    const std::uint64_t statShapes = stats.rects + stats.polygons + stats.paths + stats.texts;
    const std::uint64_t statInstances = stats.srefs + stats.arefs;
    const std::uint64_t inputPlaces = streamed.stats.srefs + streamed.stats.arefMembers;
    const std::uint64_t filePlaces = stats.srefs + stats.arefMembers;

    std::cout << "cells original " << original.lib().cells().size()
              << " loaded " << loaded.lib().cells().size()
              << " stats " << stats.cells << '\n';
    std::cout << "shapes original " << originalShapes << " loaded " << loadedShapes
              << " stats " << statShapes << '\n';
    std::cout << "instances original " << originalInstances << " loaded " << loadedInstances
              << " stats " << statInstances << '\n';
    std::cout << "compact rects " << stats.rects << " polygons " << stats.polygons
              << " paths " << stats.paths << " texts " << stats.texts
              << " srefs " << stats.srefs << " arefs " << stats.arefs << '\n';

    if (original.lib().cells().size() != loaded.lib().cells().size() ||
        original.lib().cells().size() != stats.cells) {
        std::cerr << "error: cell count mismatch\n";
        return 3;
    }
    if (originalShapes != loadedShapes || originalShapes != statShapes) {
        std::cerr << "error: shape count mismatch\n";
        return 4;
    }
    if (originalInstances != streamed.stats.srefs + streamed.stats.arefs) {
        std::cerr << "error: instance count mismatch\n";
        return 5;
    }
    if (loadedInstances != statInstances || inputPlaces != filePlaces) {
        std::cerr << "error: packed instance count mismatch\n";
        return 6;
    }
    std::cout << "stream round-trip counts match\n";
    return 0;
}
