/*!****************************************************************************************
 * \file oas_importer.cpp
 * \brief OASIS import via KLayout conversion to GDS and GdsImporter.
 *****************************************************************************************/

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

/*!****************************************************************************************
 * \brief Constructs an OasImporter with default options.
 *****************************************************************************************/
OasImporter::OasImporter() = default;

/*!****************************************************************************************
 * \brief Constructs an OasImporter with custom import options.
 * \param options    Library name and default DBU per micron.
 *****************************************************************************************/
OasImporter::OasImporter(const Options &options)
    : m_options(options)
{
}

/*!****************************************************************************************
 * \brief Imports an OASIS file into a new Database (via temporary GDS).
 * \param oasPath    Path to the input .oas file.
 * \return           Populated database, or partial result if errors() is non-empty.
 *****************************************************************************************/
Database OasImporter::importFile(const std::string &oasPath) const
{
    m_warnings.clear();
    m_errors.clear();

    if (!fileExists(oasPath)) {
        m_errors.push_back("OAS file not found: " + oasPath);
        return {};
    }

    const std::string tempGds = makeTempPath(".gds");
    std::vector<std::string> klayoutErrors;
    const bool converted = runKLayoutBatch(
        "klayout_convert_layout.drc",
        {{"in", oasPath}, {"out", tempGds}},
        klayoutErrors);

    if (!converted) {
        m_errors.insert(m_errors.end(), klayoutErrors.begin(), klayoutErrors.end());
        return {};
    }
    if (!fileExists(tempGds)) {
        m_errors.push_back("KLayout did not create temp GDS: " + tempGds);
        return {};
    }

    GdsImporter::Options gdsOptions;
    gdsOptions.libName = m_options.libName;
    gdsOptions.defaultDbuPerMicron = m_options.defaultDbuPerMicron;
    GdsImporter gdsImporter(gdsOptions);
    Database db = gdsImporter.importFile(tempGds);

    m_warnings.insert(m_warnings.end(), gdsImporter.warnings().begin(), gdsImporter.warnings().end());
    m_errors.insert(m_errors.end(), gdsImporter.errors().begin(), gdsImporter.errors().end());

    removeFile(tempGds);

    return db;
}

} // namespace core
