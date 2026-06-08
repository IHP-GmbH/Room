#include "oas_importer.h"

#include "gds_importer.h"
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

OasImporter::OasImporter() = default;

OasImporter::OasImporter(const Options &options)
    : options_(options)
{
}

Database OasImporter::importFile(const std::string &oasPath) const
{
    warnings_.clear();
    errors_.clear();

    if (!fileExists(oasPath)) {
        errors_.push_back("OAS file not found: " + oasPath);
        return {};
    }

    const std::string tempGds = makeTempPath(".gds");
    std::vector<std::string> klayoutErrors;
    const bool converted = runKLayoutBatch(
        "klayout_convert_layout.drc",
        {{"in", oasPath}, {"out", tempGds}},
        klayoutErrors);

    if (!converted) {
        errors_.insert(errors_.end(), klayoutErrors.begin(), klayoutErrors.end());
        return {};
    }
    if (!fileExists(tempGds)) {
        errors_.push_back("KLayout did not create temp GDS: " + tempGds);
        return {};
    }

    GdsImporter::Options gdsOptions;
    gdsOptions.libName = options_.libName;
    gdsOptions.defaultDbuPerMicron = options_.defaultDbuPerMicron;
    GdsImporter gdsImporter(gdsOptions);
    Database db = gdsImporter.importFile(tempGds);

    warnings_.insert(warnings_.end(), gdsImporter.warnings().begin(), gdsImporter.warnings().end());
    errors_.insert(errors_.end(), gdsImporter.errors().begin(), gdsImporter.errors().end());

    removeFile(tempGds);

    return db;
}

} // namespace core
