#include "roomReader.h"
#include "propertyBridge.h"

#include "database.h"
#include "layer_utils.h"
#include "types.h"

#include "dbBox.h"
#include "dbLayout.h"
#include "dbObjectWithProperties.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbText.h"
#include "dbTrans.h"
#include "dbInstElement.h"

#include "tlException.h"
#include "tlInternational.h"
#include "tlLog.h"
#include "tlString.h"

#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace roomdb
{

namespace {

db::Coord cast_coord (std::int64_t value)
{
  if (value > std::numeric_limits<db::Coord>::max () ||
      value < std::numeric_limits<db::Coord>::min ()) {
    throw tl::Exception (tl::to_string (tr ("Coordinate overflow: ")) + tl::to_string (value));
  }
  return db::Coord (value);
}

db::Point make_point (const room::Point &point)
{
  return db::Point (cast_coord (point.x), cast_coord (point.y));
}

db::Box make_box (const room::Box &box)
{
  return db::Box (make_point ({box.llx, box.lly}), make_point ({box.urx, box.ury}));
}

db::ICplxTrans make_transform (const room::Transform &transform)
{
  db::FTrans rot (db::FTrans::r0);
  switch (transform.orient) {
  case room::Orient::R0: rot = db::FTrans (db::FTrans::r0); break;
  case room::Orient::R90: rot = db::FTrans (db::FTrans::r90); break;
  case room::Orient::R180: rot = db::FTrans (db::FTrans::r180); break;
  case room::Orient::R270: rot = db::FTrans (db::FTrans::r270); break;
  case room::Orient::MY: rot = db::FTrans (db::FTrans::m0); break;
  case room::Orient::MX: rot = db::FTrans (db::FTrans::m90); break;
  case room::Orient::MX90: rot = db::FTrans (db::FTrans::m135); break;
  case room::Orient::MY90: rot = db::FTrans (db::FTrans::m45); break;
  }

  db::ICplxTrans result = db::ICplxTrans (rot, db::Vector (cast_coord (transform.x), cast_coord (transform.y)));
  if (transform.mag != 1.0) {
    result = result * db::ICplxTrans (transform.mag);
  }
  return result;
}

class LayerMapper
{
public:
  explicit LayerMapper (db::Layout &layout)
    : m_layout (layout)
  {
  }

  unsigned int layer_index (const room::LayerSpec &spec)
  {
    const LayerKey key { spec.layerNum, spec.dataType };
    const auto it = m_map.find (key);
    if (it != m_map.end ()) {
      return it->second;
    }

    db::LayerProperties properties;
    properties.layer = spec.layerNum;
    properties.datatype = spec.dataType;
    if (! spec.name.empty ()) {
      properties.name = spec.name;
    }

    const unsigned int index = m_layout.insert_layer (properties);
    m_map[key] = index;
    return index;
  }

private:
  struct LayerKey {
    std::uint16_t layer = 0;
    std::uint16_t dataType = 0;

    bool operator< (const LayerKey &other) const
    {
      if (layer != other.layer) {
        return layer < other.layer;
      }
      return dataType < other.dataType;
    }
  };

  db::Layout &m_layout;
  std::map<LayerKey, unsigned int> m_map;
};

template <class Obj>
void insert_shape (db::Shapes &shapes, const Obj &obj, db::properties_id_type prop_id)
{
  if (prop_id == 0) {
    shapes.insert (obj);
  } else {
    shapes.insert (db::object_with_properties<Obj> (obj, prop_id));
  }
}

void apply_core_properties (db::Cell &cell, const std::vector<room::Property> &cell_props,
                            const std::vector<room::Property> &content_props)
{
  db::PropertiesSet property_set;
  if (! cell_props.empty ()) {
    const db::properties_id_type cell_id = properties_id_from_core (cell_props);
    if (cell_id != 0) {
      property_set.merge (db::properties (cell_id));
    }
  }
  if (! content_props.empty ()) {
    const db::properties_id_type content_id = properties_id_from_core (content_props);
    if (content_id != 0) {
      property_set.merge (db::properties (content_id));
    }
  }
  if (! property_set.empty ()) {
    cell.prop_id (db::properties_id (property_set));
  }
}

} // namespace

Reader::Reader (tl::InputStream &stream)
  : m_stream (stream)
{
}

Reader::~Reader () noexcept
{
}

void Reader::common_reader_error (const std::string &msg)
{
  throw tl::Exception (msg);
}

void Reader::common_reader_warn (const std::string &msg, int /*warn_level*/)
{
  tl::warn << msg;
}

void Reader::do_read (db::Layout &layout)
{
  const std::string path = m_stream.absolute_file_path ();
  if (path.empty ()) {
    throw tl::Exception (tl::to_string (tr ("ROOM reader requires a file path")));
  }

  room::Database database;
  try {
    database = room::Database::loadFromFile (path);
  } catch (const std::exception &ex) {
    throw tl::Exception (tl::to_string (tr ("Failed to load ROOM file: ")) + ex.what ());
  }

  const room::Lib &lib = database.lib ();
  if (lib.cells ().empty ()) {
    return;
  }

  double dbu_per_micron = 1000.0;
  for (const room::Cell &cell : lib.cells ()) {
    if (const room::CellContent *content = cell.findContent (room::ViewType::Layout)) {
      if (content->dbuPerMicron () > 0.0) {
        dbu_per_micron = content->dbuPerMicron ();
        break;
      }
    }
  }
  layout.dbu (1.0 / dbu_per_micron);

  if (! lib.name ().empty ()) {
    layout.add_meta_info ("libname", db::MetaInfo (tl::to_string (tr ("Library name")), lib.name ()));
  }

  const db::properties_id_type lib_prop_id = properties_id_from_core (lib.properties ());
  if (lib_prop_id != 0) {
    layout.prop_id (lib_prop_id);
  }

  std::map<std::string, db::cell_index_type> cell_index_by_name;
  for (const room::Cell &cell : lib.cells ()) {
    cell_index_by_name[cell.name ()] = layout.add_cell (cell.name ().c_str ());
  }

  LayerMapper layer_mapper (layout);

  for (const room::Cell &cell : lib.cells ()) {
    const room::CellContent *content = cell.findContent (room::ViewType::Layout);
    if (content == nullptr) {
      continue;
    }

    const std::vector<room::LayerSpec> view_layers = room::resolveViewLayers (*content, lib);
    auto layer_spec_by_id = [&view_layers](std::uint32_t layerId) -> room::LayerSpec {
      if (layerId < view_layers.size ()) {
        return view_layers[layerId];
      }
      room::LayerSpec fallback;
      fallback.layerNum = 0;
      fallback.dataType = 0;
      fallback.name = "unknown";
      return fallback;
    };

    db::Cell &target_cell = layout.cell (cell_index_by_name.at (cell.name ()));
    apply_core_properties (target_cell, cell.properties (), content->properties ());
    const room::Block &block = content->block ();

    for (const room::Shape &shape : block.shapes ()) {
      const db::properties_id_type prop_id = properties_id_from_core (shape.properties ());
      switch (shape.type ()) {
      case room::Shape::Type::Rect: {
        if (const room::Shape::RectData *rect = shape.rect ()) {
          const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (rect->layerId));
          insert_shape (target_cell.shapes (li), make_box (rect->box), prop_id);
        }
        break;
      }
      case room::Shape::Type::Polygon: {
        if (const room::Shape::PolygonData *polygon = shape.polygon ()) {
          const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (polygon->layerId));
          std::vector<db::Point> hull;
          hull.reserve (polygon->points.size ());
          for (const room::Point &point : polygon->points) {
            hull.push_back (make_point (point));
          }
          if (! hull.empty ()) {
            db::Polygon poly;
            poly.assign_hull (hull.begin (), hull.end ());
            insert_shape (target_cell.shapes (li), poly, prop_id);
          }
        }
        break;
      }
      case room::Shape::Type::Path: {
        if (const room::Shape::PathData *path = shape.path ()) {
          if (path->points.empty ()) {
            break;
          }
          const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (path->layerId));
          std::vector<db::Point> path_points;
          path_points.reserve (path->points.size ());
          for (const room::Point &point : path->points) {
            path_points.push_back (make_point (point));
          }
          db::Path db_path;
          db_path.assign (path_points.begin (), path_points.end ());
          if (path->width > 0) {
            db_path.width (cast_coord (path->width));
          }
          insert_shape (target_cell.shapes (li), db_path, prop_id);
        }
        break;
      }
      case room::Shape::Type::Text: {
        if (const room::Shape::TextData *text = shape.text ()) {
          const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (text->layerId));
          const db::Point position = make_point (text->position);
          db::Text label (text->text, db::Trans (db::Vector (position)));
          if (text->height > 0) {
            label.size (cast_coord (text->height));
          }
          insert_shape (target_cell.shapes (li), label, prop_id);
        }
        break;
      }
      }
    }

    for (const room::Instance &instance : block.instances ()) {
      const auto child_it = cell_index_by_name.find (instance.cellName ());
      if (child_it == cell_index_by_name.end ()) {
        common_reader_warn (tl::to_string (tr ("Missing child cell: ")) + instance.cellName ());
        continue;
      }

      const db::CellInstArray arr (
        db::CellInst (child_it->second),
        make_transform (instance.transform ()));
      const db::properties_id_type prop_id = properties_id_from_core (instance.properties ());
      if (prop_id == 0) {
        target_cell.insert (arr);
      } else {
        target_cell.insert (db::object_with_properties<db::CellInstArray> (arr, prop_id));
      }
    }
  }
}

} // namespace roomdb
