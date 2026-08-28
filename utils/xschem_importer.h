#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief Imports Xschem .sch/.sym files into a CORE Database via the schematic/symbol API.
 *****************************************************************************************/
class XschemImporter {
public:
    struct Options {
        std::string libName = "xschem";
        std::string cellName; /*!< Empty = derive from file name. */
        bool updateExisting = true;
    };

    XschemImporter();
    explicit XschemImporter(const Options &options);

    Database                                            importFile(const std::string &path) const;
    Database                                            importFileInto(Database db, const std::string &path) const;
    Database                                            importText(const std::string &text, const std::string &extension,
                                                                 const std::string &cellName = {}) const;
    Database                                            importTextInto(Database db, const std::string &text, const std::string &extension,
                                                                      const std::string &cellName = {}) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Database                                            importTextInto(Database db, const std::vector<std::string> &records,
                                                                      const std::string &extension,
                                                                      const std::string &cellName,
                                                                      const std::string &sourcePath) const;

    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
