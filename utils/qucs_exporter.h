#pragma once

#include "database.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace core {

/*! Xschem-only schematic decoration (title blocks, wire labels, …): skip in Qucs, no <Lib>. */
bool isQucsSchematicDecoration(const std::string &cellName);

/*!****************************************************************************************
 * \brief The QucsExporter class writes a schematic cell from a CORE Database to a .sch file.
 *****************************************************************************************/
class QucsExporter {
public:
    /*! \brief Export options such as Qucs version string in the output header. */
    struct Options {
        std::string               qucsVersion = "0.0.19";
        std::vector<std::string>  primitiveCorePaths;
        std::string               techLibrary;
        std::string               qucsPrimitiveLib;
    };

    QucsExporter();
    explicit QucsExporter(const Options &options);

    void                                                exportCell(const Database &db, const std::string &cellName,
                                                                 const std::string &schPath) const;
    void                                                exportCell(const Database &db, const std::string &cellName,
                                                                 std::ostream &out) const;
    std::string                                         exportCellToString(const Database &db,
                                                                           const std::string &cellName) const;
    void                                                exportSymbolCell(const Database &db, const std::string &cellName,
                                                                         const std::string &schPath) const;
    void                                                exportSymbolCell(const Database &db, const std::string &cellName,
                                                                         std::ostream &out) const;
    std::string                                         exportSymbolCellToString(const Database &db,
                                                                                 const std::string &cellName) const;

    std::vector<std::string>                            symbolLinesForCell(const Database &db,
                                                                           const std::string &cellName) const;

    std::string                                         exportInstanceAsLine(const Instance &inst,
                                                                           double dbuPerEditorUnit,
                                                                           const std::string &sourceFormat) const;
    std::vector<std::string>                            exportBlockWiresAsLines(const Block &block,
                                                                                const std::vector<LayerSpec> &layers,
                                                                                double dbuPerEditorUnit,
                                                                                const std::string &sourceFormat) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
