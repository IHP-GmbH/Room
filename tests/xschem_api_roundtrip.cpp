#include "database.h"

#include "xschem_exporter.h"

#include "xschem_importer.h"



#include <cstdio>

#include <fstream>

#include <iostream>

#include <sstream>

#include <stdexcept>

#include <string>
#include <vector>
#include <algorithm>



#ifdef _WIN32

#include <direct.h>

#else

#include <sys/stat.h>

#endif



namespace {



void ensureDirectory(const std::string &dirPath)

{

    std::string partial;

    for (char ch : dirPath) {

        partial.push_back(ch);

        if (ch != '/' && ch != '\\') {

            continue;

        }

        if (partial.size() <= 1) {

            continue;

        }

#ifdef _WIN32

        _mkdir(partial.c_str());

#else

        mkdir(partial.c_str(), 0755);

#endif

    }

#ifdef _WIN32

    _mkdir(partial.c_str());

#else

    mkdir(partial.c_str(), 0755);

#endif

}



const core::CellContent *findView(const core::Cell &cell)

{

    if (const core::CellContent *view = cell.findContent(core::ViewType::Schematic)) {

        return view;

    }

    return cell.findContent(core::ViewType::Symbol);

}



std::vector<std::string> collectInstanceNames(const core::Block &block)

{

    std::vector<std::string> names;

    for (const core::Instance &inst : block.instances()) {

        const std::string *name = nullptr;

        for (const core::Property &prop : inst.properties()) {

            if (prop.name == "name") {

                name = &prop.value;

                break;

            }

        }

        names.push_back(name != nullptr ? *name : inst.cellName());

    }

    std::sort(names.begin(), names.end());

    return names;

}



std::vector<std::string> collectNetLabels(const core::Block &block)

{

    std::vector<std::string> labels;

    for (const core::Net &net : block.nets()) {

        labels.push_back(net.name());

    }

    std::sort(labels.begin(), labels.end());

    return labels;

}



} // namespace



int main(int argc, char *argv[])

{

    const std::string schPath = (argc > 1) ? argv[1] : "examples/xschem_to_core/data/test.sch";

    const std::string corePath = (argc > 2) ? argv[2] : "build/tests/xschem_api_roundtrip.core";

    const std::string exportPath = (argc > 3) ? argv[3] : "build/tests/xschem_api_roundtrip.out.sch";



    core::XschemImporter importer;

    core::Database original = importer.importFile(schPath);

    if (!importer.errors().empty()) {

        for (const auto &msg : importer.errors()) {

            std::cerr << "error: " << msg << '\n';

        }

        return 1;

    }



    if (original.lib().cells().empty()) {

        std::cerr << "error: no cells imported\n";

        return 2;

    }



    const core::CellContent *view = findView(original.lib().cells().front());

    if (view == nullptr || view->block().instances().empty() || view->block().nets().empty()) {

        std::cerr << "error: schematic API import missing instances or nets\n";

        return 3;

    }



    const std::size_t slash = corePath.find_last_of("/\\");

    if (slash != std::string::npos) {

        ensureDirectory(corePath.substr(0, slash));

    }

    const std::size_t exportSlash = exportPath.find_last_of("/\\");

    if (exportSlash != std::string::npos) {

        ensureDirectory(exportPath.substr(0, exportSlash));

    }



    original.saveToFile(corePath, core::ViewType::Schematic);

    const core::Database reloaded = core::Database::loadFromFile(corePath);

    const core::Cell *cell = reloaded.lib().findCell(original.lib().cells().front().name());

    const core::CellContent *reloadedView = cell != nullptr ? findView(*cell) : nullptr;

    if (reloadedView == nullptr || reloadedView->block().instances().size() != view->block().instances().size() ||

        reloadedView->block().nets().size() != view->block().nets().size()) {

        std::cerr << "error: CORE reload changed schematic topology\n";

        return 4;

    }



    core::XschemExporter exporter;

    exporter.exportCell(reloaded, cell->name(), exportPath);

    if (!exporter.errors().empty()) {

        for (const auto &msg : exporter.errors()) {

            std::cerr << "error: " << msg << '\n';

        }

        return 5;

    }



    core::XschemImporter roundTripImporter;

    core::Database roundTrip = roundTripImporter.importFile(exportPath);

    if (!roundTripImporter.errors().empty() || roundTrip.lib().cells().empty()) {

        std::cerr << "error: failed to re-import exported schematic\n";

        return 6;

    }



    const core::CellContent *roundTripView = findView(roundTrip.lib().cells().front());

    if (roundTripView == nullptr) {

        std::cerr << "error: missing view after re-import\n";

        return 7;

    }



    const auto originalNames = collectInstanceNames(view->block());

    const auto roundTripNames = collectInstanceNames(roundTripView->block());

    const auto originalNets = collectNetLabels(view->block());

    const auto roundTripNets = collectNetLabels(roundTripView->block());



    if (originalNames != roundTripNames || originalNets != roundTripNets) {

        std::cerr << "error: semantic round-trip mismatch\n";

        return 8;

    }



    if (reloadedView->sourceInfo().format() != "xschem") {

        std::cerr << "error: missing source format metadata\n";

        return 9;

    }



    std::cout << "xschem API round-trip OK (" << view->block().instances().size() << " instances, "

              << view->block().nets().size() << " nets)\n";

    std::remove(corePath.c_str());

    std::remove(exportPath.c_str());

    return 0;

}

