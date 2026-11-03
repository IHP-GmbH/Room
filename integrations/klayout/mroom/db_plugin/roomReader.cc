#include "roomReader.h"
#include "propertyBridge.h"

#include "database.h"
#include "layer_utils.h"
#include "serialization.h"
#include "types.h"

#include "dbBox.h"
#include "dbLayout.h"
#include "dbObjectWithProperties.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbText.h"
#include "dbTrans.h"
#include "dbInstElement.h"
#include "dbShape.h"

#include "tlException.h"
#include "tlInternational.h"
#include "tlLog.h"
#include "tlString.h"

#include <algorithm>
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

struct Placement
{
  std::int64_t x = 0;
  std::int64_t y = 0;
};

struct RegularGrid
{
  std::int64_t x0 = 0;
  std::int64_t y0 = 0;
  std::int64_t dx = 0;
  std::int64_t dy = 0;
  std::uint64_t nx = 1;
  std::uint64_t ny = 1;
};

bool arithmetic_step (const std::vector<std::int64_t> &values, std::int64_t &step)
{
  if (values.size () <= 1) {
    step = 0;
    return true;
  }
  step = values[1] - values[0];
  if (step == 0) {
    return false;
  }
  for (std::size_t i = 2; i < values.size (); ++i) {
    if (values[i] - values[i - 1] != step) {
      return false;
    }
  }
  return true;
}

bool as_regular_grid (std::vector<Placement> points, RegularGrid &grid)
{
  if (points.empty ()) {
    return false;
  }
  const std::size_t original_count = points.size ();
  std::sort (points.begin (), points.end (), [] (const Placement &a, const Placement &b) {
    if (a.x != b.x) {
      return a.x < b.x;
    }
    return a.y < b.y;
  });
  points.erase (std::unique (points.begin (), points.end (), [] (const Placement &a, const Placement &b) {
    return a.x == b.x && a.y == b.y;
  }), points.end ());
  if (points.size () != original_count) {
    return false;
  }

  std::vector<std::int64_t> xs;
  std::vector<std::int64_t> ys;
  xs.reserve (points.size ());
  ys.reserve (points.size ());
  for (const Placement &point : points) {
    if (xs.empty () || xs.back () != point.x) {
      xs.push_back (point.x);
    }
    ys.push_back (point.y);
  }
  std::sort (ys.begin (), ys.end ());
  ys.erase (std::unique (ys.begin (), ys.end ()), ys.end ());
  if (static_cast<std::uint64_t> (xs.size ()) * static_cast<std::uint64_t> (ys.size ()) != points.size ()) {
    return false;
  }
  std::int64_t dx = 0;
  std::int64_t dy = 0;
  if (! arithmetic_step (xs, dx) || ! arithmetic_step (ys, dy)) {
    return false;
  }
  std::size_t index = 0;
  for (std::int64_t x : xs) {
    for (std::int64_t y : ys) {
      if (points[index].x != x || points[index].y != y) {
        return false;
      }
      ++index;
    }
  }
  grid.x0 = xs.front ();
  grid.y0 = ys.front ();
  grid.dx = dx;
  grid.dy = dy;
  grid.nx = xs.size ();
  grid.ny = ys.size ();
  return true;
}

void insert_box_grid (db::Shapes &shapes, db::Layout &layout,
                      std::int64_t llx, std::int64_t lly, std::int64_t width, std::int64_t height,
                      std::int64_t step_x, std::int64_t step_y, std::uint64_t columns, std::uint64_t rows)
{
  if (columns == 0 || rows == 0) {
    return;
  }
  const db::Box box (db::Point (cast_coord (llx), cast_coord (lly)),
                     db::Point (cast_coord (llx + width), cast_coord (lly + height)));
  if (columns == 1 && rows == 1) {
    shapes.insert (box);
    return;
  }
  shapes.insert (db::Shape::box_array_type (box, db::UnitTrans (), layout.array_repository (),
                                            db::Vector (cast_coord (step_x), 0),
                                            db::Vector (0, cast_coord (step_y)),
                                            columns, rows));
}

void insert_box_points (db::Shapes &shapes, db::Layout &layout, std::int64_t width, std::int64_t height,
                        const std::vector<std::int64_t> &xs, const std::vector<std::int64_t> &ys)
{
  const std::size_t count = std::min (xs.size (), ys.size ());
  std::vector<Placement> points;
  points.reserve (count);
  for (std::size_t i = 0; i < count; ++i) {
    points.push_back (Placement { xs[i], ys[i] });
  }
  RegularGrid grid;
  if (as_regular_grid (points, grid) && (grid.nx > 1 || grid.ny > 1)) {
    insert_box_grid (shapes, layout, grid.x0, grid.y0, width, height, grid.dx, grid.dy, grid.nx, grid.ny);
    return;
  }

  std::sort (points.begin (), points.end (), [] (const Placement &a, const Placement &b) {
    if (a.y != b.y) {
      return a.y < b.y;
    }
    return a.x < b.x;
  });
  std::size_t begin = 0;
  while (begin < points.size ()) {
    std::size_t end = begin + 1;
    std::int64_t step = 0;
    if (end < points.size () && points[end].y == points[begin].y && points[end].x != points[begin].x) {
      step = points[end].x - points[begin].x;
      ++end;
      while (end < points.size () && points[end].y == points[begin].y &&
             points[end].x - points[end - 1].x == step) {
        ++end;
      }
    }
    const std::uint64_t run = static_cast<std::uint64_t> (end - begin);
    if (run > 1) {
      insert_box_grid (shapes, layout, points[begin].x, points[begin].y, width, height, step, 0, run, 1);
    } else {
      insert_box_grid (shapes, layout, points[begin].x, points[begin].y, width, height, 0, 0, 1, 1);
    }
    begin = end;
  }
}

template <class EmitRun>
void insert_origins (const std::vector<std::int64_t> &xs, const std::vector<std::int64_t> &ys, EmitRun &&emit_run)
{
  const std::size_t count = std::min (xs.size (), ys.size ());
  std::vector<Placement> points;
  points.reserve (count);
  for (std::size_t i = 0; i < count; ++i) {
    points.push_back (Placement { xs[i], ys[i] });
  }
  RegularGrid grid;
  if (as_regular_grid (points, grid) && (grid.nx > 1 || grid.ny > 1)) {
    emit_run (grid.x0, grid.y0, grid.dx, grid.dy, grid.nx, grid.ny);
    return;
  }

  std::sort (points.begin (), points.end (), [] (const Placement &a, const Placement &b) {
    if (a.y != b.y) {
      return a.y < b.y;
    }
    return a.x < b.x;
  });
  std::size_t begin = 0;
  while (begin < points.size ()) {
    std::size_t end = begin + 1;
    std::int64_t step = 0;
    if (end < points.size () && points[end].y == points[begin].y && points[end].x != points[begin].x) {
      step = points[end].x - points[begin].x;
      ++end;
      while (end < points.size () && points[end].y == points[begin].y &&
             points[end].x - points[end - 1].x == step) {
        ++end;
      }
    }
    const std::uint64_t run = static_cast<std::uint64_t> (end - begin);
    if (run > 1) {
      emit_run (points[begin].x, points[begin].y, step, 0, run, 1);
    } else {
      emit_run (points[begin].x, points[begin].y, 0, 0, 1, 1);
    }
    begin = end;
  }
}

void insert_polygon_grid (db::Shapes &shapes, db::Layout &layout, const std::vector<room::Point> &prototype,
                          std::int64_t shift_x, std::int64_t shift_y, std::int64_t step_x, std::int64_t step_y,
                          std::uint64_t columns, std::uint64_t rows)
{
  if (prototype.size () < 3 || columns == 0 || rows == 0) {
    return;
  }
  std::vector<db::Point> hull;
  hull.reserve (prototype.size ());
  for (const room::Point &point : prototype) {
    hull.push_back (db::Point (cast_coord (point.x + shift_x), cast_coord (point.y + shift_y)));
  }
  db::SimplePolygon polygon;
  polygon.assign_hull (hull.begin (), hull.end ());
  if (columns == 1 && rows == 1) {
    shapes.insert (polygon);
    return;
  }
  shapes.insert (db::array<db::SimplePolygonPtr, db::Disp> (
    db::SimplePolygonPtr (polygon, layout.shape_repository ()),
    db::Disp (db::Vector (0, 0)),
    layout.array_repository (),
    db::Vector (cast_coord (step_x), 0),
    db::Vector (0, cast_coord (step_y)),
    columns, rows));
}

void insert_path_grid (db::Shapes &shapes, db::Layout &layout, const room::CompactPathRepeatRef &repeat,
                       std::int64_t shift_x, std::int64_t shift_y, std::int64_t step_x, std::int64_t step_y,
                       std::uint64_t columns, std::uint64_t rows)
{
  if (repeat.points.empty () || columns == 0 || rows == 0) {
    return;
  }
  std::vector<db::Point> path_points;
  path_points.reserve (repeat.points.size ());
  for (const room::Point &point : repeat.points) {
    path_points.push_back (db::Point (cast_coord (point.x + shift_x), cast_coord (point.y + shift_y)));
  }
  db::Path path;
  path.assign (path_points.begin (), path_points.end ());
  if (repeat.width > 0) {
    path.width (cast_coord (repeat.width));
  }
  path.extensions (cast_coord (repeat.bgnExt), cast_coord (repeat.endExt));
  if (repeat.round) {
    path.round (true);
  }
  if (columns == 1 && rows == 1) {
    shapes.insert (path);
    return;
  }
  shapes.insert (db::array<db::PathPtr, db::Disp> (
    db::PathPtr (path, layout.shape_repository ()),
    db::Disp (db::Vector (0, 0)),
    layout.array_repository (),
    db::Vector (cast_coord (step_x), 0),
    db::Vector (0, cast_coord (step_y)),
    columns, rows));
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

  std::map<std::string, db::cell_index_type> cell_index_by_name;
  std::map<std::string, std::vector<room::LayerSpec>> layers_by_cell;
  LayerMapper layer_mapper (layout);
  bool dbu_set = false;

  auto layer_spec_by_id = [&layers_by_cell](const std::string &cell_name, std::uint32_t layerId) -> room::LayerSpec {
    const auto it = layers_by_cell.find (cell_name);
    if (it != layers_by_cell.end () && layerId < it->second.size ()) {
      return it->second[layerId];
    }
    room::LayerSpec fallback;
    fallback.layerNum = 0;
    fallback.dataType = 0;
    fallback.name = "unknown";
    return fallback;
  };

  room::LayoutVisitHandlers handlers;
  handlers.onFile = [&](const std::string &lib_name, const std::vector<room::Property> &lib_props) {
    if (! lib_name.empty ()) {
      layout.add_meta_info ("libname", db::MetaInfo (tl::to_string (tr ("Library name")), lib_name));
    }
    const db::properties_id_type lib_prop_id = properties_id_from_core (lib_props);
    if (lib_prop_id != 0) {
      layout.prop_id (lib_prop_id);
    }
  };
  handlers.onCell = [&](const room::VisitedCell &cell) {
    if (! dbu_set && cell.dbuPerMicron > 0.0) {
      layout.dbu (1.0 / cell.dbuPerMicron);
      dbu_set = true;
    }
    const db::cell_index_type index = layout.add_cell (cell.name.c_str ());
    cell_index_by_name[cell.name] = index;
    layers_by_cell[cell.name] = cell.layers;
    apply_core_properties (layout.cell (index), cell.cellProperties, cell.contentProperties);
  };
  handlers.onShape = [&](const std::string &cell_name, room::Shape shape) {
    const auto cell_it = cell_index_by_name.find (cell_name);
    if (cell_it == cell_index_by_name.end ()) {
      return;
    }
    db::Cell &target_cell = layout.cell (cell_it->second);
    const db::properties_id_type prop_id = properties_id_from_core (shape.properties ());
    switch (shape.type ()) {
    case room::Shape::Type::Rect: {
      if (const room::Shape::RectData *rect = shape.rect ()) {
        const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (cell_name, rect->layerId));
        insert_shape (target_cell.shapes (li), make_box (rect->box), prop_id);
      }
      break;
    }
    case room::Shape::Type::Polygon: {
      if (const room::Shape::PolygonData *polygon = shape.polygon ()) {
        const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (cell_name, polygon->layerId));
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
        const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (cell_name, path->layerId));
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
        db_path.extensions (cast_coord (path->bgnExt), cast_coord (path->endExt));
        if (path->round) {
          db_path.round (true);
        }
        insert_shape (target_cell.shapes (li), db_path, prop_id);
      }
      break;
    }
    case room::Shape::Type::Text: {
      if (const room::Shape::TextData *text = shape.text ()) {
        const unsigned int li = layer_mapper.layer_index (layer_spec_by_id (cell_name, text->layerId));
        const db::Point position = make_point (text->position);
        db::Text label (text->text, db::Trans (db::Vector (position)));
        if (text->height > 0) {
          label.size (cast_coord (text->height));
        }
        insert_shape (target_cell.shapes (li), label, prop_id);
      }
      break;
    }
    case room::Shape::Type::Arc:
      break;
    }
  };
  handlers.onInstance = [&](const std::string &cell_name, room::VisitedInstance instance) {
    const auto parent_it = cell_index_by_name.find (cell_name);
    const auto child_it = cell_index_by_name.find (instance.cellName);
    if (parent_it == cell_index_by_name.end ()) {
      return;
    }
    if (child_it == cell_index_by_name.end ()) {
      common_reader_warn (tl::to_string (tr ("Missing child cell: ")) + instance.cellName);
      return;
    }

    const db::ICplxTrans trans = make_transform (instance.transform);
    db::CellInstArray arr (db::CellInst (child_it->second), trans);
    if (instance.columns > 1 || instance.rows > 1) {
      const unsigned long columns = instance.columns == 0 ? 1 : instance.columns;
      const unsigned long rows = instance.rows == 0 ? 1 : instance.rows;
      arr = db::CellInstArray (
        db::CellInst (child_it->second),
        trans,
        db::Vector (cast_coord (instance.stepX), 0),
        db::Vector (0, cast_coord (instance.stepY)),
        columns,
        rows);
    }
    db::Cell &target_cell = layout.cell (parent_it->second);
    const db::properties_id_type prop_id = properties_id_from_core (instance.properties);
    if (prop_id == 0) {
      target_cell.insert (arr);
    } else {
      target_cell.insert (db::object_with_properties<db::CellInstArray> (arr, prop_id));
    }
  };

  auto target_shapes = [&] (const std::string &cell_name, std::uint32_t layer_id) -> db::Shapes * {
    const auto cell_it = cell_index_by_name.find (cell_name);
    if (cell_it == cell_index_by_name.end ()) {
      return 0;
    }
    const unsigned int layer_index = layer_mapper.layer_index (layer_spec_by_id (cell_name, layer_id));
    return &layout.cell (cell_it->second).shapes (layer_index);
  };

  handlers.onRectArray = [&] (const std::string &cell_name, const room::CompactRectArrayRef &array) {
    db::Shapes *shapes = target_shapes (cell_name, array.layerId);
    if (! shapes) {
      return;
    }
    insert_box_grid (*shapes, layout, array.originLlx, array.originLly, array.width, array.height,
                     array.stepX, array.stepY, array.columns, array.rows);
  };
  handlers.onRectGroup = [&] (const std::string &cell_name, const room::CompactRectGroupRef &group) {
    db::Shapes *shapes = target_shapes (cell_name, group.layerId);
    if (! shapes) {
      return;
    }
    insert_box_points (*shapes, layout, group.width, group.height, group.llx, group.lly);
  };
  handlers.onPolygonRepeat = [&] (const std::string &cell_name, const room::CompactPolygonRepeatRef &repeat) {
    db::Shapes *shapes = target_shapes (cell_name, repeat.layerId);
    if (! shapes || repeat.originX.empty ()) {
      return;
    }
    const std::int64_t origin_x = repeat.originX.front ();
    const std::int64_t origin_y = repeat.originY.front ();
    insert_origins (repeat.originX, repeat.originY,
                    [&] (std::int64_t x, std::int64_t y, std::int64_t step_x, std::int64_t step_y,
                         std::uint64_t columns, std::uint64_t rows) {
      insert_polygon_grid (*shapes, layout, repeat.points, x - origin_x, y - origin_y, step_x, step_y, columns, rows);
    });
  };
  handlers.onPathRepeat = [&] (const std::string &cell_name, const room::CompactPathRepeatRef &repeat) {
    db::Shapes *shapes = target_shapes (cell_name, repeat.layerId);
    if (! shapes || repeat.originX.empty ()) {
      return;
    }
    const std::int64_t origin_x = repeat.originX.front ();
    const std::int64_t origin_y = repeat.originY.front ();
    insert_origins (repeat.originX, repeat.originY,
                    [&] (std::int64_t x, std::int64_t y, std::int64_t step_x, std::int64_t step_y,
                         std::uint64_t columns, std::uint64_t rows) {
      insert_path_grid (*shapes, layout, repeat, x - origin_x, y - origin_y, step_x, step_y, columns, rows);
    });
  };

  try {
    room::visitLayoutFile (path, handlers);
  } catch (const std::exception &ex) {
    throw tl::Exception (tl::to_string (tr ("Failed to load ROOM file: ")) + ex.what ());
  }

  if (! dbu_set) {
    layout.dbu (0.001);
  }
}

} // namespace roomdb
