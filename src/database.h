#pragma once

#include "lib.h"

#include <string>

namespace core {

/*!****************************************************************************************
 * \brief Options controlling how a Database is serialized to a .core file.
 *****************************************************************************************/
struct SaveOptions {
    bool compactGeometry = true; /*!< When true, geometry is written to payload.compact. */
};

/*!****************************************************************************************
 * \brief The Database class is the root container for a CORE design stored in a .core file.
 *
 * A Database holds format metadata (version, generator, technology) and a single Lib with
 * cells, layers, and optional derived index. Use saveToFile() and loadFromFile() for I/O.
 *****************************************************************************************/
class Database {
public:
    Database();

    const std::string &                                 version() const { return m_version; }
    const std::string &                                 generator() const { return m_generator; }
    const std::string &                                 technology() const { return m_technology; }

    void                                                setGenerator(std::string value) { m_generator = std::move(value); }
    void                                                setTechnology(std::string value) { m_technology = std::move(value); }
    void                                                setVersion(std::string value) { m_version = std::move(value); }

    Lib &                                               lib() { return m_lib; }
    const Lib &                                         lib() const { return m_lib; }

    void                                                saveToFile(const std::string &path, SaveOptions options = {});
    static Database                                     loadFromFile(const std::string &path);

private:
    std::string                                         m_version = "1.0";
    std::string                                         m_generator;
    std::string                                         m_technology;
    Lib                                                 m_lib{"default"};
};

} // namespace core
