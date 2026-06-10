#pragma once

#include "cell.h"
#include "lib_index.h"
#include "types.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The Lib class represents a design library containing cells, layers, and metadata.
 *
 * Each Database owns one Lib. Cells hold per-view CellContent; layers are library-global.
 * refreshIndex() rebuilds the derived LibIndex used for hierarchy and bounding boxes.
 *****************************************************************************************/
class Lib {
public:
    explicit Lib(std::string name);

    const std::string &                                 name() const { return m_name; }

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

    std::vector<Cell> &                                 cells() { return m_cells; }
    const std::vector<Cell> &                           cells() const { return m_cells; }

    std::vector<LayerSpec> &                            layers() { return m_layers; }
    const std::vector<LayerSpec> &                      layers() const { return m_layers; }

    Cell*                                               findCell(const std::string &name);
    const Cell*                                         findCell(const std::string &name) const;
    Cell&                                               getOrCreateCell(const std::string &name);

    void                                                recomputeAllBBoxes(ViewType view = ViewType::Layout);
    void                                                refreshIndex(ViewType view = ViewType::Layout);
    void                                                setIndex(LibIndex index);
    bool                                                hasIndex() const { return m_hasIndex; }
    const LibIndex &                                    index() const { return m_index; }

private:
    std::string                                         m_name;
    std::vector<Property>                               m_properties;
    std::vector<LayerSpec>                              m_layers;
    std::vector<Cell>                                   m_cells;
    LibIndex                                            m_index;
    bool                                                m_hasIndex = false;
};

} // namespace core
