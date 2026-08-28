#pragma once

#include "block.h"
#include "database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The QucsImporter class reads Qucs schematic (.sch) files into a CORE Database.
 *****************************************************************************************/
class QucsImporter {
public:
    /*! \brief Import options for library and cell naming. */
    struct Options {
        std::string libName = "qucs_import";
        std::string cellName; /*!< Empty = derive cell name from .sch file name. */
    };

    QucsImporter();
    explicit QucsImporter(const Options &options);

    Database                                            importFile(const std::string &schPath) const;
    Database                                            importText(const std::string &text, const std::string &cellName = {}) const;

    struct WireRecord {
        std::int64_t x1 = 0;
        std::int64_t y1 = 0;
        std::int64_t x2 = 0;
        std::int64_t y2 = 0;
        std::string label;
        std::int64_t labelX = 0;
        std::int64_t labelY = 0;
        std::int64_t dist = 0;
        std::string nodeSet;
    };

    Instance                                            parseComponentLinePublic(const std::string &line) const;
    WireRecord                                          parseWireLinePublic(const std::string &line) const;
    void                                                importWireLines(Block &block, const std::vector<std::string> &lines,
                                                                        double dbuPerEditorUnit) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
