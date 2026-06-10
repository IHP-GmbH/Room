#include "database.h"
#include "gds_importer.h"

#include <iostream>
#include <string>

namespace {

bool layerPurposeEqual(const core::LayerSpec &a, const core::LayerSpec &b)
{
    return a.layerNum == b.layerNum && a.dataType == b.dataType && a.name == b.name && a.purpose == b.purpose;
}

bool layersEqual(const std::vector<core::LayerSpec> &a, const std::vector<core::LayerSpec> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!layerPurposeEqual(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string gdsPath = (argc > 1) ? argv[1] : "testdata/sample.gds";
    const std::string corePath = (argc > 2) ? argv[2] : "build/tests/encapsulation_roundtrip.core";

    core::GdsImporter importer;
    core::Database original = importer.importFile(gdsPath);
    if (!importer.errors().empty()) {
        for (const auto &msg : importer.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 1;
    }
    if (original.lib().cells().empty()) {
        std::cerr << "error: no cells imported\n";
        return 1;
    }

    core::Cell &cell = original.lib().cells().front();
    const core::CellContent *layout = cell.findContent(core::ViewType::Layout);
    if (layout == nullptr || layout->layers().empty()) {
        std::cerr << "error: layout view missing per-view layer table\n";
        return 2;
    }

    bool sawLabel = false;
    bool sawBoundary = false;
    for (const core::LayerSpec &layer : layout->layers()) {
        if (layer.purpose == core::LayerPurpose::Label) {
            sawLabel = true;
        }
        if (layer.purpose == core::LayerPurpose::Boundary) {
            sawBoundary = true;
        }
    }
    if (!sawBoundary) {
        std::cerr << "error: expected boundary layer purpose in layout view\n";
        return 3;
    }

    cell.aliases() = {"TOP_ALIAS", "legacy_top"};
    cell.pCell().setMasterName("stdcell_master");
    cell.pCell().parameters().push_back({"w", "1.0"});
    cell.pCell().parameters().push_back({"h", "2.0"});

    original.setVersion("1.0");
    original.saveToFile(corePath);

    const core::Database reloaded = core::Database::loadFromFile(corePath);
    const core::Cell *reloadedCell = reloaded.lib().findCell(cell.name());
    if (reloadedCell == nullptr) {
        std::cerr << "error: cell missing after reload\n";
        return 4;
    }
    if (reloadedCell->aliases() != cell.aliases()) {
        std::cerr << "error: cell aliases mismatch after round-trip\n";
        return 5;
    }
    if (!reloadedCell->pCell().isPCell() || reloadedCell->pCell().masterName() != cell.pCell().masterName() ||
        reloadedCell->pCell().parameters().size() != cell.pCell().parameters().size()) {
        std::cerr << "error: PCell metadata mismatch after round-trip\n";
        return 6;
    }

    const core::CellContent *reloadedLayout = reloadedCell->findContent(core::ViewType::Layout);
    if (reloadedLayout == nullptr || !layersEqual(reloadedLayout->layers(), layout->layers())) {
        std::cerr << "error: per-view layer table mismatch after round-trip\n";
        return 7;
    }
    if (sawLabel) {
        bool reloadedLabel = false;
        for (const core::LayerSpec &layer : reloadedLayout->layers()) {
            if (layer.purpose == core::LayerPurpose::Label) {
                reloadedLabel = true;
            }
        }
        if (!reloadedLabel) {
            std::cerr << "error: label layer purpose lost after round-trip\n";
            return 8;
        }
    }

    std::cout << "encapsulation round-trip OK: viewLayers=" << reloadedLayout->layers().size()
              << " aliases=" << reloadedCell->aliases().size()
              << " pcell=" << reloadedCell->pCell().masterName() << '\n';
    return 0;
}
