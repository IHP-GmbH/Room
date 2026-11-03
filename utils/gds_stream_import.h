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
    /*! Sum of columns*rows over AREF records seen in the GDS stream. */
    std::uint64_t arefMembers = 0;
};

struct GdsStreamImportResult {
    GdsStreamStats stats;
    std::vector<std::string> errors;
};

/*!****************************************************************************************
 * \brief Streams a GDSII file into one compact .room without building a Shape per element.
 *
 * Identical rectangles, paths, and polygons on an exact orthogonal grid are stored as
 * arrays. Other rectangles of one size stay in a group. SREF placements of the same
 * cell, orientation, and magnification are stored as arrays when their origins form
 * an exact orthogonal grid. AREF records are kept as read.
 *****************************************************************************************/
GdsStreamImportResult importGdsStreamToRoom(const std::string &gdsPath,
                                            const std::string &roomPath,
                                            const std::string &libName = "gds_import");

/*!****************************************************************************************
 * \brief Counts compact layout records without expanding them into Shape objects.
 *****************************************************************************************/
GdsStreamStats readCompactRoomStats(const std::string &roomPath);

} // namespace room
