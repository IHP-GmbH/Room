#pragma once

#include "xschem_importer.h"

#include <string>
#include <vector>

namespace core::xschem_bridge {

struct Status {
    bool                                                ok = true;
    std::vector<std::string>                            errors;
    std::vector<std::string>                            warnings;
};

std::vector<std::string> listCells(const std::string &corePath, Status &status);

Status exportCell(const std::string &corePath, const std::string &cellName, const std::string &outputPath);

std::string exportCellToString(const std::string &corePath, const std::string &cellName, Status &status);

Status exportAll(const std::string &corePath, const std::string &outputDir, std::size_t &exportedCount);

Status importIntoCore(const std::string &inputPath, const std::string &corePath, const XschemImporter::Options &options);

Status importTextIntoCore(const std::string &text, const std::string &extension, const std::string &corePath,
                          const XschemImporter::Options &options);

} // namespace core::xschem_bridge
