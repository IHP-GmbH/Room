#pragma once

#include "database.h"
#include "compact_codec.h"

#include <database.capnp.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief Writes a Database into a Cap'n Proto Database builder.
 * \param root     Cap'n Proto builder for the root Database message.
 * \param db       In-memory database to serialize.
 * \param options  Save options (compact vs verbose geometry).
 *****************************************************************************************/
void writeDatabase(schema::Database::Builder root, const Database &db, SaveOptions options = {});

/*!****************************************************************************************
 * \brief Reads a Database from a Cap'n Proto Database reader.
 * \param root     Cap'n Proto reader for the root Database message.
 * \return         Populated in-memory Database.
 *****************************************************************************************/
Database readDatabase(schema::Database::Reader root);

/*!****************************************************************************************
 * \brief One cell header seen while streaming a layout file.
 *****************************************************************************************/
struct VisitedCell {
    std::string name;
    double dbuPerMicron = 1000.0;
    std::vector<LayerSpec> layers;
    std::vector<Property> cellProperties;
    std::vector<Property> contentProperties;
};

/*!****************************************************************************************
 * \brief One placement. columns or rows of 0 means a single reference.
 *****************************************************************************************/
struct VisitedInstance {
    std::string cellName;
    Transform transform;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
    std::vector<Point> offsets;
    std::vector<Property> properties;
};

/*!****************************************************************************************
 * \brief Callbacks for a two-pass layout walk. onCell runs for every cell first.
 *
 * Geometry is taken from the verbose block when that block has shapes or placements,
 * and from the compact block otherwise. Shapes are handed over one at a time.
 *****************************************************************************************/
struct LayoutVisitHandlers {
    std::function<void(const std::string &libName, const std::vector<Property> &libProperties)> onFile;
    std::function<void(const VisitedCell &cell)> onCell;
    std::function<void(const std::string &cellName, Shape shape)> onShape;
    std::function<void(const std::string &cellName, VisitedInstance instance)> onInstance;
    /*! Receivers for compact repetitions. When set, identical members without properties stay one array. */
    std::function<void(const std::string &cellName, const CompactRectArrayRef &array)> onRectArray;
    std::function<void(const std::string &cellName, const CompactRectGroupRef &group)> onRectGroup;
    std::function<void(const std::string &cellName, const CompactPolygonRepeatRef &repeat)> onPolygonRepeat;
    std::function<void(const std::string &cellName, const CompactPathRepeatRef &repeat)> onPathRepeat;
};

/*!****************************************************************************************
 * \brief Reads a ROOM file in parts and delivers layout cells without keeping every shape.
 * \param path      Path to a .room file, compact or verbose.
 * \param handlers  Callbacks. onCell is invoked for all cells before any onShape.
 *****************************************************************************************/
void visitLayoutFile(const std::string &path, const LayoutVisitHandlers &handlers);

} // namespace room
