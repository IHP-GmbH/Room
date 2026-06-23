#include "core_paths.h"

#include <iostream>
#include <string>

int main()
{
    const std::string path = core::coreFileName("top", core::ViewType::Schematic);
    if (path != "top.schematic.core") {
        std::cerr << "coreFileName failed: " << path << '\n';
        return 1;
    }

    const core::ParsedCorePath parsed = core::parseCoreFilePath("lib/primitives.symbol.core");
    if (!parsed.valid || parsed.cellName != "primitives" || parsed.view != core::ViewType::Symbol) {
        std::cerr << "parseCoreFilePath failed\n";
        return 2;
    }

    if (!core::isViewCoreFile("cell.layout.core", core::ViewType::Layout)) {
        std::cerr << "isViewCoreFile layout failed\n";
        return 3;
    }

    if (core::isViewCoreFile("cell.schematic.core", core::ViewType::Layout)) {
        std::cerr << "isViewCoreFile should reject schematic as layout\n";
        return 4;
    }

    if (core::coreFileGlob(core::ViewType::Symbol) != "*.symbol.core") {
        std::cerr << "coreFileGlob failed\n";
        return 5;
    }

    std::cout << "core_paths OK\n";
    return 0;
}
