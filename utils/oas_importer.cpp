/*!****************************************************************************************
 * \file oas_importer.cpp
 * \brief Native OASIS import into a CORE Database.
 *****************************************************************************************/

#include "oas_importer.h"

#include "cell.h"
#include "oas_reader.h"
#include "oas_strict.h"
#include "property.h"

#include <fstream>
#include <string>
#include <unordered_map>

namespace core {
namespace {

bool fileExists(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

void setProperty(std::vector<Property> &properties, const std::string &name, const std::string &value)
{
    for (Property &prop : properties) {
        if (prop.name == name) {
            prop.value = value;
            return;
        }
    }
    properties.emplace_back(name, value);
}

const std::string *findProperty(const std::vector<Property> &properties, const std::string &name)
{
    for (const Property &prop : properties) {
        if (prop.name == name) {
            return &prop.value;
        }
    }
    return nullptr;
}

void attachStrictPayload(Database &db,
                         const std::string &oasPath,
                         const std::unordered_map<std::uint64_t, std::string> &cellNamesByRef,
                         std::vector<std::string> &warnings)
{
    const std::size_t cellCount = db.lib().cells().size();
    if (cellCount == 0) {
        return;
    }

    std::vector<std::uint8_t> header;
    std::vector<std::uint8_t> tail;
    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> geometryByRef;
    std::vector<std::string> extractErrors;
    if (!extractStrictGeometryFromFile(oasPath, cellCount, header, tail, geometryByRef, extractErrors)) {
        for (const std::string &msg : extractErrors) {
            warnings.push_back(msg);
        }
        return;
    }

    setProperty(db.lib().properties(), kStrictHeaderProp, base64Encode(header));
    setProperty(db.lib().properties(), kStrictTailProp, base64Encode(tail));

    for (const auto &geomEntry : geometryByRef) {
        const auto nameIt = cellNamesByRef.find(geomEntry.first);
        if (nameIt == cellNamesByRef.end()) {
            continue;
        }
        Cell *cell = db.lib().findCell(nameIt->second);
        if (cell == nullptr) {
            continue;
        }
        setProperty(cell->properties(), kStrictRefProp, std::to_string(geomEntry.first));
        setProperty(cell->properties(), kStrictGeometryProp, base64Encode(geomEntry.second));
    }
}

} // namespace

OasImporter::OasImporter() = default;

OasImporter::OasImporter(const Options &options)
    : m_options(options)
{
}

Database OasImporter::importFile(const std::string &oasPath) const
{
    m_warnings.clear();
    m_errors.clear();

    if (!fileExists(oasPath)) {
        m_errors.push_back("OAS file not found: " + oasPath);
        return {};
    }

    Database db;
    OasReader reader(oasPath);
    if (!reader.importDatabase(db, m_options.libName, m_options.defaultDbuPerMicron)) {
        m_errors.insert(m_errors.end(), reader.errors().begin(), reader.errors().end());
        return {};
    }

    m_warnings.insert(m_warnings.end(), reader.warnings().begin(), reader.warnings().end());
    attachStrictPayload(db, oasPath, reader.cellNamesByRef(), m_warnings);
    return db;
}

} // namespace core
