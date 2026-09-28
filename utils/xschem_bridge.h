#pragma once

#include "xschem_importer.h"

#include <string>
#include <vector>

namespace room::xschem_bridge {

struct Status {
    bool                                                ok = true;
    std::vector<std::string>                            errors;
    std::vector<std::string>                            warnings;
};

std::vector<std::string> listCells(const std::string &roomPath, Status &status);

Status exportCell(const std::string &roomPath, const std::string &cellName, const std::string &outputPath);

std::string exportCellToString(const std::string &roomPath, const std::string &cellName, Status &status);

Status exportAll(const std::string &roomPath, const std::string &outputDir, std::size_t &exportedCount);

Status importIntoCore(const std::string &inputPath, const std::string &roomPath, const XschemImporter::Options &options);

Status importTextIntoCore(const std::string &text, const std::string &extension, const std::string &roomPath,
                          const XschemImporter::Options &options);

} // namespace room::xschem_bridge
