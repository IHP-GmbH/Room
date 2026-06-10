#pragma once

#include "database.h"
#include "enums.h"
#include "types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief Mutable import context for building a Database while parsing OASIS geometry records.
 *****************************************************************************************/
class OasImportContext {
public:
    explicit OasImportContext(Database &db, std::string libName, double defaultDbuPerMicron);

    Database &                                          database() { return m_db; }
    double                                              dbuPerMicron() const { return m_dbuPerMicron; }
    void                                                setDbuPerMicron(double value);

    void                                                setModalLayer(std::uint64_t layer, std::uint64_t datatype);
    void                                                registerLayerRef(std::uint64_t ref, std::uint16_t layer, std::uint16_t datatype);
    void                                                registerDatatypeRef(std::uint64_t ref, std::uint16_t datatype);

    void                                                beginCell(const std::string &name);
    void                                                addPlacement(const std::string &cellName, std::int64_t x, std::int64_t y);
    void                                                addRectangle(std::int64_t x, std::int64_t y, std::uint64_t w, std::uint64_t h);
    void                                                addPolygon(const std::vector<Point> &points);
    void                                                addPath(const std::vector<Point> &points, std::uint32_t width);
    void                                                addText(const std::string &text, std::int64_t x, std::int64_t y, std::uint32_t height);

    void                                                finalize();

    std::vector<std::string> &                          warnings() { return m_warnings; }

private:
    void                                                syncCurrentCell();
    std::uint32_t                                       ensureViewLayer(std::uint16_t layerNum, std::uint16_t dataType, LayerPurpose purpose);
    LayerPurpose                                        rectanglePurpose() const;

    Database &                                          m_db;
    double                                              m_dbuPerMicron;
    std::string                                         m_currentCellName;
    Cell *                                              m_currentCell = nullptr;
    CellContent *                                       m_currentContent = nullptr;
    Block *                                             m_currentBlock = nullptr;

    std::uint64_t                                       m_modalLayer = 0;
    std::uint64_t                                       m_modalDatatype = 0;
    std::unordered_map<std::uint64_t, std::uint16_t>    m_layerRef;
    std::unordered_map<std::uint64_t, std::uint16_t>    m_datatypeRef;

    std::vector<std::string>                            m_warnings;
};

} // namespace core
