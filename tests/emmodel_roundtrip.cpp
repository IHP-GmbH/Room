#include "database.h"
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
    const std::string path = outDir + "/inductor.emmodel.room";

    if (room::roomFileName("inductor", room::ViewType::EmModel) != "inductor.emmodel.room") {
        std::cerr << "roomFileName emmodel failed\n";
        return 1;
    }
    if (!room::isViewRoomFile(path, room::ViewType::EmModel)) {
        std::cerr << "isViewRoomFile emmodel failed for " << path << '\n';
        return 2;
    }

    room::EmModelViewData model;
    model.defaultVariant = "nominal";
    model.snpPath = "inductor.emsetup/nominal/result.s2p";
    model.tool = "palace";
    model.emstudioPath = "inductor.emsetup/nominal/model.py";
    model.z0 = 50.0;
    model.ports.push_back({"P1", 1});
    model.ports.push_back({"P2", 2});
    model.topology.layoutPath = "inductor.layout.room";
    model.topology.layoutHash = "abc123";
    model.topology.topCell = "inductor";
    model.setup.variant = "nominal";
    model.setup.modelPath = "inductor.emsetup/nominal/model.py";
    model.setup.modelHash = "def456";
    model.setup.substratePath = "stackup.xml";
    model.setup.substrateHash = "ghi789";
    model.setup.tool = "palace";
    model.snpHash = "snp000";
    model.publishedAt = "2026-10-06T12:00:00Z";

    {
        room::Database db;
        db.setGenerator("emmodel_roundtrip");
        room::Cell &cell = db.lib().getOrCreateCell("inductor");
        room::CellContent &content = cell.getOrCreateContent(room::ViewType::EmModel);
        content.setEmModel(model);
        db.saveToFile(path, room::ViewType::EmModel);
    }

    const room::Database loaded = room::Database::loadFromFile(path);
    if (loaded.fileView() != room::ViewType::EmModel) {
        std::cerr << "fileView is not EmModel\n";
        return 3;
    }
    const room::Cell *cell = loaded.lib().findCell("inductor");
    if (cell == nullptr) {
        std::cerr << "cell missing\n";
        return 4;
    }
    const room::CellContent *content = cell->findContent(room::ViewType::EmModel);
    if (content == nullptr || !content->hasEmModelPayload()) {
        std::cerr << "emmodel content missing\n";
        return 5;
    }
    const room::EmModelViewData &got = content->emModel();
    if (got.snpPath != model.snpPath || got.ports.size() != 2 || got.ports[0].name != "P1"
        || got.ports[1].index != 2 || got.topology.layoutHash != "abc123"
        || got.setup.modelHash != "def456" || got.z0 != 50.0
        || got.publishedAt != model.publishedAt) {
        std::cerr << "emmodel payload mismatch after round-trip\n";
        return 6;
    }

    std::cout << "emmodel_roundtrip OK\n";
    return 0;
}
