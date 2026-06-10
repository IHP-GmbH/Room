#pragma once

#include "block.h"
#include "layer_spec.h"
#include "types.h"

namespace core {

/*!****************************************************************************************
 * \brief The CellContent class holds one view of a cell: type, DBU scale, properties, and topology.
 *
 * In C++, block() is the ergonomic accessor for shapes, instances, and nets. On disk, topology
 * is stored in payload (compact or verbose block) according to SaveOptions.
 *****************************************************************************************/
class CellContent {
public:
    CellContent(ViewType viewType, double dbuPerMicron = 1000.0);

    ViewType                                            viewType() const { return m_viewType; }
    void                                                setViewType(ViewType type) { m_viewType = type; }

    double                                              dbuPerMicron() const { return m_dbuPerMicron; }
    void                                                setDbuPerMicron(double value) { m_dbuPerMicron = value; }

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

    Block &                                             block() { return m_block; }
    const Block &                                       block() const { return m_block; }

    std::vector<LayerSpec> &                            layers() { return m_layers; }
    const std::vector<LayerSpec> &                      layers() const { return m_layers; }

private:
    ViewType                                            m_viewType;
    double                                              m_dbuPerMicron;
    std::vector<Property>                               m_properties;
    std::vector<LayerSpec>                              m_layers;
    Block                                               m_block;
};

} // namespace core
