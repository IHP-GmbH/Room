#pragma once

#include "block.h"
#include "em_model_data.h"
#include "source_info.h"
#include "layer_spec.h"
#include "types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief The CellContent class holds one view of a cell: type, DBU scale, properties, and topology.
 *
 * In C++, block() is the ergonomic accessor for shapes, instances, and nets. On disk, topology
 * is stored in payload (compact or verbose block) according to SaveOptions. EmModel views use
 * emModel() instead of geometry; opaque payloads use setOpaquePayload().
 *****************************************************************************************/
class CellContent {
public:
    CellContent(ViewType viewType, double dbuPerMicron = 1000.0);

    ViewType                                            viewType() const { return m_viewType; }
    void                                                setViewType(ViewType type) { m_viewType = type; }

    double                                              dbuPerMicron() const { return m_dbuPerMicron; }
    void                                                setDbuPerMicron(double value) { m_dbuPerMicron = value; }

    /** Schematic/symbol views: integer DBU per 1.0 native editor unit (Qucs=1, Xschem=1000). 0 = infer on read. */
    double                                              dbuPerEditorUnit() const { return m_dbuPerEditorUnit; }
    void                                                setDbuPerEditorUnit(double value) { m_dbuPerEditorUnit = value; }

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

    Block &                                             block() { return m_block; }
    const Block &                                       block() const { return m_block; }

    std::vector<LayerSpec> &                            layers() { return m_layers; }
    const std::vector<LayerSpec> &                      layers() const { return m_layers; }

    bool                                                hasOpaquePayload() const { return m_opaqueHasValue; }
    const std::string &                                 opaqueMimeType() const { return m_opaqueMimeType; }
    const std::vector<std::uint8_t> &                   opaqueData() const { return m_opaqueData; }
    void                                                setOpaquePayload(std::string mimeType, std::vector<std::uint8_t> data);
    void                                                clearOpaquePayload();

    bool                                                hasEmModelPayload() const { return m_emModelHasValue; }
    EmModelViewData &                                   emModel() { return m_emModel; }
    const EmModelViewData &                             emModel() const { return m_emModel; }
    void                                                setEmModel(EmModelViewData data);
    void                                                clearEmModel();

    SourceInfo &                                        sourceInfo() { return m_sourceInfo; }
    const SourceInfo &                                  sourceInfo() const { return m_sourceInfo; }

private:
    ViewType                                            m_viewType;
    double                                              m_dbuPerMicron;
    double                                              m_dbuPerEditorUnit = 0.0;
    std::vector<Property>                               m_properties;
    std::vector<LayerSpec>                              m_layers;
    Block                                               m_block;
    SourceInfo                                          m_sourceInfo;
    bool                                                m_opaqueHasValue = false;
    std::string                                         m_opaqueMimeType;
    std::vector<std::uint8_t>                           m_opaqueData;
    bool                                                m_emModelHasValue = false;
    EmModelViewData                                     m_emModel;
};

} // namespace room
