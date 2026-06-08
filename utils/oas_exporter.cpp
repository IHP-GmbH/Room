#include "oas_exporter.h"

#include "gds_exporter.h"
#include "klayout_util.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace core {
namespace {

bool fileExists(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

std::string tempDirectory()
{
    if (const char *tmp = std::getenv("TEMP")) {
        return tmp;
    }
    if (const char *tmp = std::getenv("TMP")) {
        return tmp;
    }
    if (const char *tmp = std::getenv("TMPDIR")) {
        return tmp;
    }
    return ".";
}

std::string makeTempPath(const std::string &suffix)
{
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream name;
    name << tempDirectory();
#ifdef _WIN32
    if (!name.str().empty() && name.str().back() != '\\' && name.str().back() != '/') {
        name << '\\';
    }
#else
    if (!name.str().empty() && name.str().back() != '/') {
        name << '/';
    }
#endif
    name << "core_oas_" << rng() << suffix;
    return name.str();
}

void removeFile(const std::string &path)
{
    if (!path.empty()) {
        std::remove(path.c_str());
    }
}

} // namespace

void OasExporter::exportFile(const Database &db, const std::string &oasPath) const
{
    warnings_.clear();
    errors_.clear();

    const std::string tempGds = makeTempPath(".gds");
    GdsExporter gdsExporter;
    gdsExporter.exportFile(db, tempGds);

    warnings_.insert(warnings_.end(), gdsExporter.warnings().begin(), gdsExporter.warnings().end());
    if (!gdsExporter.errors().empty()) {
        errors_.insert(errors_.end(), gdsExporter.errors().begin(), gdsExporter.errors().end());
        removeFile(tempGds);
        return;
    }

    std::vector<std::string> klayoutErrors;
    const bool converted = runKLayoutBatch(
        "klayout_convert_layout.drc",
        {{"in", tempGds}, {"out", oasPath}},
        klayoutErrors);

    removeFile(tempGds);

    if (!converted) {
        errors_.insert(errors_.end(), klayoutErrors.begin(), klayoutErrors.end());
        return;
    }
    if (!fileExists(oasPath)) {
        errors_.push_back("KLayout did not create OAS file: " + oasPath);
    }
}

} // namespace core
