#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace room {

/*! \brief One EM port mapped to a Touchstone index (and typically a symbol pin). */
struct EmPort {
    std::string   name;
    std::uint16_t index = 0;
};

/*! \brief Frozen reference to the layout topology used for a published EM model. */
struct EmTopologySnapshot {
    std::string layoutPath;
    std::string layoutHash;
    std::string topCell;
};

/*! \brief Frozen reference to the EMStudio setup (script + substrate) for a variant. */
struct EmSetupSnapshot {
    std::string variant;
    std::string modelPath;
    std::string modelHash;
    std::string substratePath;
    std::string substrateHash;
    std::string tool;
};

/*!****************************************************************************************
 * \brief Structured payload for ViewType::EmModel (published S-parameter handle).
 *
 * Touchstone and field dumps stay on disk; this struct stores paths, port map, and
 * topology/setup snapshot hashes for LibMan / schematic consumers.
 *****************************************************************************************/
struct EmModelViewData {
    std::string         defaultVariant;
    std::string         snpPath;
    std::vector<EmPort> ports;
    std::string         tool;
    std::string         emstudioPath;
    double              z0 = 50.0;
    EmTopologySnapshot  topology;
    EmSetupSnapshot     setup;
    std::string         snpHash;
    std::string         publishedAt;
};

} // namespace room
