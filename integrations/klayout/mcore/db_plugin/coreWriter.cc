#include "coreWriter.h"
#include "propertyBridge.h"

#include "block.h"
#include "cell.h"
#include "cell_content.h"
#include "database.h"
#include "instance.h"
#include "layer_utils.h"
#include "serialization.h"
#include "shape.h"
#include "types.h"

#include "dbBox.h"
#include "dbCell.h"
#include "dbInstances.h"
#include "dbLayout.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbShape.h"
#include "dbText.h"
#include "dbTrans.h"

#include "tlException.h"
#include "tlFileUtils.h"
#include "tlInternational.h"
#include "tlStream.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace coredb
{

namespace {

std::int64_t cast_coord (db::Coord value)
{
  return static_cast<std::int64_t> (value);
}

core::Point make_point (const db::Point &point)
{
  return core::Point { cast_coord (point.x ()), cast_coord (point.y ()) };
}

core::Orient orient_from_fp (int code)
{
  switch (code) {
  case db::FTrans::r0: return core::Orient::R0;
  case db::FTrans::r90: return core::Orient::R90;
  case db::FTrans::r180: return core::Orient::R180;
  case db::FTrans::r270: return core::Orient::R270;
  case db::FTrans::m0: return core::Orient::MY;
  case db::FTrans::m45: return core::Orient::MY90;
  case db::FTrans::m90: return core::Orient::MX;
  case db::FTrans::m135: return core::Orient::MX90;
  default: return core::Orient::R0;
  }
}

core::Transform make_transform (const db::ICplxTrans &trans)
{
  core::Transform result;
  const db::Vector disp = trans.disp ();
  result.x = cast_coord (disp.x ());
  result.y = cast_coord (disp.y ());
  result.mag = trans.mag ();
  result.orient = orient_from_fp (trans.fp_trans ().rot ());
  return result;
}

core::LayerSpec layer_spec_from_properties (const db::LayerProperties &properties)
{
  core::LayerSpec spec;
  if (properties.layer >= 0) {
    spec.layerNum = static_cast<std::uint16_t> (properties.layer);
  }
  if (properties.datatype >= 0) {
    spec.dataType = static_cast<std::uint16_t> (properties.datatype);
  }
  if (! properties.name.empty ()) {
    spec.name = properties.name;
  }
  return spec;
}

void write_database_to_stream (core::Database &database, tl::OutputStream &stream)
{
  database.lib ().recomputeAllBBoxes ();
  database.lib ().refreshIndex ();
  database.setFileSummary (core::FileSummary::fromLib (database.lib (), core::ViewType::Layout));

  core::SaveOptions options;
  capnp::MallocMessageBuilder message;
  writeDatabase (message.initRoot<core::schema::Database> (), database, options);

  kj::VectorOutputStream vector_stream;
  capnp::writeMessage (vector_stream, message);
  const kj::ArrayPtr<const kj::byte> bytes = vector_stream.getArray ();
  stream.put_raw (reinterpret_cast<const char *> (bytes.begin ()), bytes.size ());
}

std::string resolve_lib_name (const db::Layout &layout, const db::SaveLayoutOptions &options, const tl::OutputStream &stream)
{
  std::string lib_name = options.libname ();
  if (lib_name.empty () && layout.has_meta_info ("libname")) {
    const tl::Variant libname = layout.meta_info ("libname").value;
    if (libname.is_a_string ()) {
      lib_name = libname.to_stdstring ();
    }
  }
  if (lib_name.empty ()) {
    const std::string path = stream.path ();
    if (! path.empty ()) {
      lib_name = tl::basename (path);
    }
  }
  if (lib_name.empty ()) {
    lib_name = "default";
  }
  return lib_name;
}

} // namespace

void Writer::write (db::Layout &layout, tl::OutputStream &stream, const db::SaveLayoutOptions &options)
{
  const std::string lib_name = resolve_lib_name (layout, options, stream);
  layout.add_meta_info ("libname", db::MetaInfo (tl::to_string (tr ("Library name")), lib_name));

  core::Database database;
  database.setGenerator ("KLayout CORE writer");
  database.lib () = core::Lib (lib_name);
  database.lib ().properties () = properties_from_klayout (layout.prop_id ());

  const double layout_dbu = layout.dbu ();
  const double dbu_per_micron = (layout_dbu > 0.0) ? (1.0 / layout_dbu) : 1000.0;

  std::map<std::pair<std::uint16_t, std::uint16_t>, core::LayerSpec> lib_layer_catalog;
  for (unsigned int li = 0; li < layout.layers (); ++li) {
    if (! layout.is_valid_layer (li)) {
      continue;
    }
    const db::LayerProperties &properties = layout.get_properties (li);
    if (properties.layer < 0 || properties.datatype < 0) {
      continue;
    }
    const auto key = std::make_pair (static_cast<std::uint16_t> (properties.layer),
                                     static_cast<std::uint16_t> (properties.datatype));
    if (lib_layer_catalog.find (key) == lib_layer_catalog.end ()) {
      lib_layer_catalog[key] = layer_spec_from_properties (properties);
    }
  }
  for (const auto &entry : lib_layer_catalog) {
    database.lib ().layers ().push_back (entry.second);
  }

  for (db::Layout::const_iterator cit = layout.begin (); cit != layout.end (); ++cit) {
    const db::Cell &cell = *cit;
    const db::cell_index_type cell_index = cell.cell_index ();
    const char *cell_name = layout.cell_name (cell_index);
    if (cell_name == nullptr || *cell_name == '\0') {
      continue;
    }

    core::Cell &core_cell = database.lib ().getOrCreateCell (cell_name);
    core_cell.properties () = properties_from_klayout (cell.prop_id ());
    core::CellContent &content = core_cell.getOrCreateContent (core::ViewType::Layout, dbu_per_micron);
    content.setDbuPerMicron (dbu_per_micron);
    content.layers ().clear ();

    auto layer_id_for = [&content, &database](const db::LayerProperties &properties) -> std::uint32_t {
      return findOrAddViewLayer (content, layer_spec_from_properties (properties), database.lib ().layers ());
    };

    for (unsigned int li = 0; li < layout.layers (); ++li) {
      if (! layout.is_valid_layer (li)) {
        continue;
      }
      const db::LayerProperties &properties = layout.get_properties (li);
      if (properties.layer < 0 || properties.datatype < 0) {
        continue;
      }

      const std::uint32_t layer_id = layer_id_for (properties);
      db::ShapeIterator shape_it = cell.shapes (li).begin (
        db::ShapeIterator::Boxes | db::ShapeIterator::Polygons | db::ShapeIterator::Paths | db::ShapeIterator::Texts);

      while (! shape_it.at_end ()) {
        const db::Shape &shape = *shape_it;

        if (shape.is_box ()) {
          db::Box box;
          shape.box (box);
          core::Shape::RectData rect;
          rect.layerId = layer_id;
          rect.box = core::Box {
            cast_coord (box.left ()), cast_coord (box.bottom ()),
            cast_coord (box.right ()), cast_coord (box.top ())
          };
          content.block ().shapes ().emplace_back (rect);
          content.block ().shapes ().back ().properties () = properties_from_klayout (shape.prop_id ());
        } else if (shape.is_polygon () || shape.is_simple_polygon ()) {
          core::Shape::PolygonData polygon;
          polygon.layerId = layer_id;
          if (shape.is_polygon ()) {
            db::Polygon poly;
            shape.polygon (poly);
            for (auto p = poly.begin_hull (); p != poly.end_hull (); ++p) {
              polygon.points.push_back (make_point (*p));
            }
          } else {
            db::SimplePolygon simple;
            shape.simple_polygon (simple);
            for (auto p = simple.begin_hull (); p != simple.end_hull (); ++p) {
              polygon.points.push_back (make_point (*p));
            }
          }
          if (! polygon.points.empty ()) {
            content.block ().shapes ().emplace_back (polygon);
            content.block ().shapes ().back ().properties () = properties_from_klayout (shape.prop_id ());
          }
        } else if (shape.is_path ()) {
          db::Path path;
          shape.path (path);
          core::Shape::PathData path_data;
          path_data.layerId = layer_id;
          path_data.width = static_cast<std::uint32_t> (std::max<db::Coord> (0, path.width ()));
          for (auto p = path.begin (); p != path.end (); ++p) {
            path_data.points.push_back (make_point (*p));
          }
          if (! path_data.points.empty ()) {
            content.block ().shapes ().emplace_back (path_data);
            content.block ().shapes ().back ().properties () = properties_from_klayout (shape.prop_id ());
          }
        } else if (shape.is_text ()) {
          db::Text text;
          shape.text (text);
          core::Shape::TextData label;
          label.layerId = layer_id;
          label.text = text.string ();
          const db::Vector disp = text.trans ().disp ();
          label.position = core::Point { cast_coord (disp.x ()), cast_coord (disp.y ()) };
          label.height = static_cast<std::uint32_t> (std::max<db::Coord> (0, text.size ()));
          content.block ().shapes ().emplace_back (label);
          content.block ().shapes ().back ().properties () = properties_from_klayout (shape.prop_id ());
        }

        ++shape_it;
      }
    }

    for (db::Cell::const_iterator inst = cell.begin (); ! inst.at_end (); ++inst) {
      const db::cell_index_type child_index = inst->cell_index ();
      const char *child_name = layout.cell_name (child_index);
      if (child_name == nullptr || *child_name == '\0') {
        continue;
      }

      const db::CellInstArray &arr = inst->cell_inst ();
      for (db::CellInstArray::iterator it = arr.begin (); ! it.at_end (); ++it) {
        const db::ICplxTrans tr = arr.complex_trans (*it);
        content.block ().instances ().emplace_back (child_name, make_transform (tr));
        content.block ().instances ().back ().properties () = properties_from_klayout (inst->prop_id ());
      }
    }

    content.block ().recomputeBBox ();
  }

  write_database_to_stream (database, stream);
}

} // namespace coredb
