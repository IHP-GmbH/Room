#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace room {

struct GdsStreamStats {
    std::uint64_t cells = 0;
    std::uint64_t rects = 0;
    std::uint64_t polygons = 0;
    std::uint64_t paths = 0;
    std::uint64_t texts = 0;
    std::uint64_t srefs = 0;
    std::uint64_t arefs = 0;
};

struct GdsStreamImportResult {
    GdsStreamStats stats;
    std::vector<std::string> errors;
};

/*!****************************************************************************************
 * \brief Streams a GDSII file into one compact .room without building a Shape per element.
 *
 * Rectangles are stored as four coordinates, other polygons and paths as zigzag-varint
 * deltas. SREF and AREF keep a cell-name index. AREF keeps columns, rows, and step.
 *****************************************************************************************/
GdsStreamImportResult importGdsStreamToRoom(const std::string &gdsPath,
                                            const std::string &roomPath,
                                            const std::string &libName = "gds_import");

/*!****************************************************************************************
 * \brief Counts compact layout records without expanding them into Shape objects.
 *****************************************************************************************/
GdsStreamStats readCompactRoomStats(const std::string &roomPath);

} // namespace room
