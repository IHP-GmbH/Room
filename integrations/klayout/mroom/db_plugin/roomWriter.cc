#include "roomWriter.h"
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

#include "lstrCompressed.h"

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

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace roomdb
{

namespace {

constexpr unsigned int k_oasis_compression_level = 2;
constexpr bool k_oasis_recompress = false;

std::int64_t cast_coord (db::Coord value)
{
  return static_cast<std::int64_t> (value);
}

room::Point make_point (const db::Point &point)
{
  return room::Point { cast_coord (point.x ()), cast_coord (point.y ()) };
}

room::Point make_point (const db::Vector &vector)
{
  return room::Point { cast_coord (vector.x ()), cast_coord (vector.y ()) };
}

room::Orient orient_from_fp (int code)
{
  switch (code) {
  case db::FTrans::r0: return room::Orient::R0;
  case db::FTrans::r90: return room::Orient::R90;
  case db::FTrans::r180: return room::Orient::R180;
  case db::FTrans::r270: return room::Orient::R270;
  case db::FTrans::m0: return room::Orient::MY;
  case db::FTrans::m45: return room::Orient::MY90;
  case db::FTrans::m90: return room::Orient::MX;
  case db::FTrans::m135: return room::Orient::MX90;
  default: return room::Orient::R0;
  }
}

room::Transform make_transform (const db::ICplxTrans &trans)
{
  room::Transform result;
  const db::Vector disp = trans.disp ();
  result.x = cast_coord (disp.x ());
  result.y = cast_coord (disp.y ());
  result.mag = trans.mag ();
  result.orient = orient_from_fp (trans.fp_trans ().rot ());
  return result;
}

room::LayerSpec layer_spec_from_properties (const db::LayerProperties &properties)
{
  room::LayerSpec spec;
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

void write_database_to_stream (room::Database &database, tl::OutputStream &stream)
{
  database.lib ().recomputeAllBBoxes ();
  database.lib ().refreshIndex ();
  database.setFileSummary (room::FileSummary::fromLib (database.lib (), room::ViewType::Layout));

  room::SaveOptions options;
  options.compactGeometry = true;
  capnp::MallocMessageBuilder message;
  writeDatabase (message.initRoot<room::schema::Database> (), database, options);

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

struct RepMaps
{
  std::unordered_map<std::uint64_t, lstr::RegularArray> regular;
  std::unordered_map<std::uint64_t, const std::vector<db::Vector> *> irregular;
};

RepMaps build_rep_maps (const lstr::Compressed &compressed)
{
  RepMaps maps;
  for (auto it = compressed.begin_regular_arrays (); it != compressed.end_regular_arrays (); ++it) {
    maps.regular.emplace (it->second, it->first);
  }
  for (auto it = compressed.begin_irregular_arrays (); it != compressed.end_irregular_arrays (); ++it) {
    maps.irregular.emplace (it->second, &it->first);
  }
  return maps;
}

template <class Data>
void apply_repetition (Data &data, std::uint64_t rep_id, const RepMaps &maps)
{
  if (rep_id == 0) {
    return;
  }

  const auto regular_it = maps.regular.find (rep_id);
  if (regular_it != maps.regular.end ()) {
    const lstr::RegularArray &array = regular_it->second;
    if (! array.is_null () && (array.na () > 1 || array.nb () > 1)) {
      data.columns = static_cast<std::uint32_t> (array.na ());
      data.rows = static_cast<std::uint32_t> (array.nb ());
      data.stepX = cast_coord (array.a ().x ());
      data.colY = cast_coord (array.a ().y ());
      data.rowX = cast_coord (array.b ().x ());
      data.stepY = cast_coord (array.b ().y ());
    }
    return;
  }

  const auto irregular_it = maps.irregular.find (rep_id);
  if (irregular_it != maps.irregular.end () && irregular_it->second != nullptr && ! irregular_it->second->empty ()) {
    // lstr irregular offsets omit (0,0); ROOM copies include the origin like OASIS.
    data.copies.clear ();
    data.copies.reserve (irregular_it->second->size () + 1);
    data.copies.push_back (room::Point { 0, 0 });
    for (const db::Vector &offset : *irregular_it->second) {
      data.copies.push_back (make_point (offset));
    }
  }
}

template <class Poly>
std::vector<room::Point> polygon_points (const Poly &poly)
{
  std::vector<room::Point> points;
  for (auto p = poly.begin_hull (); p != poly.end_hull (); ++p) {
    points.push_back (make_point (*p));
  }
  return points;
}

std::vector<room::Point> path_points (const db::Path &path)
{
  std::vector<room::Point> points;
  for (auto p = path.begin (); p != path.end (); ++p) {
    points.push_back (make_point (*p));
  }
  return points;
}

void emit_box (room::Block &block, std::uint32_t layer_id, const db::Box &box,
               db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  if (box.empty ()) {
    return;
  }
  room::Shape::RectData rect;
  rect.layerId = layer_id;
  rect.box = room::Box {
    cast_coord (box.left ()), cast_coord (box.bottom ()),
    cast_coord (box.right ()), cast_coord (box.top ())
  };
  apply_repetition (rect, rep_id, maps);
  block.shapes ().emplace_back (rect);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

bool polygon_as_box (const std::vector<room::Point> &points, room::Box &box)
{
  if (points.size () < 4) {
    return false;
  }
  std::int64_t llx = points[0].x;
  std::int64_t lly = points[0].y;
  std::int64_t urx = points[0].x;
  std::int64_t ury = points[0].y;
  for (const room::Point &point : points) {
    llx = std::min (llx, point.x);
    lly = std::min (lly, point.y);
    urx = std::max (urx, point.x);
    ury = std::max (ury, point.y);
  }
  if (urx <= llx || ury <= lly) {
    return false;
  }
  unsigned corners = 0;
  unsigned axis = 0;
  for (const room::Point &point : points) {
    const bool on_x = (point.x == llx || point.x == urx);
    const bool on_y = (point.y == lly || point.y == ury);
    if (on_x && on_y) {
      ++corners;
    } else if (on_x || on_y) {
      ++axis;
    } else {
      return false;
    }
  }
  if (corners < 4) {
    return false;
  }
  (void) axis;
  box = room::Box { llx, lly, urx, ury };
  return true;
}

void emit_polygon (room::Block &block, std::uint32_t layer_id, std::vector<room::Point> points,
                   db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  if (points.size () < 3) {
    return;
  }

  room::Box box;
  if (polygon_as_box (points, box)) {
    room::Shape::RectData rect;
    rect.layerId = layer_id;
    rect.box = box;
    apply_repetition (rect, rep_id, maps);
    block.shapes ().emplace_back (rect);
    if (prop_id != 0) {
      block.shapes ().back ().properties () = properties_from_klayout (prop_id);
    }
    return;
  }

  room::Shape::PolygonData polygon;
  polygon.layerId = layer_id;
  polygon.points = std::move (points);
  apply_repetition (polygon, rep_id, maps);
  block.shapes ().emplace_back (polygon);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_path (room::Block &block, std::uint32_t layer_id, const db::Path &path,
                db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  std::vector<room::Point> points = path_points (path);
  if (points.empty ()) {
    return;
  }
  room::Shape::PathData path_data;
  path_data.layerId = layer_id;
  path_data.points = std::move (points);
  path_data.width = static_cast<std::uint32_t> (std::max<db::Coord> (0, path.width ()));
  path_data.bgnExt = static_cast<std::int32_t> (path.bgn_ext ());
  path_data.endExt = static_cast<std::int32_t> (path.end_ext ());
  path_data.round = path.round ();
  apply_repetition (path_data, rep_id, maps);
  block.shapes ().emplace_back (path_data);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_text (room::Block &block, std::uint32_t layer_id, const db::Text &text,
                db::properties_id_type prop_id)
{
  room::Shape::TextData label;
  label.layerId = layer_id;
  label.text = text.string ();
  const db::Vector disp = text.trans ().disp ();
  label.position = make_point (disp);
  label.height = static_cast<std::uint32_t> (std::max<db::Coord> (0, text.size ()));
  block.shapes ().emplace_back (label);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_text_with_rep (room::Block &block, std::uint32_t layer_id, const db::Text &text,
                         db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  // ROOM text records have no repetition fields; expand like a flat label list.
  if (rep_id == 0) {
    emit_text (block, layer_id, text, prop_id);
    return;
  }

  const auto regular_it = maps.regular.find (rep_id);
  if (regular_it != maps.regular.end () && ! regular_it->second.is_null ()) {
    const lstr::RegularArray &array = regular_it->second;
    for (size_t ia = 0; ia < array.na (); ++ia) {
      for (size_t ib = 0; ib < array.nb (); ++ib) {
        db::Text copy (text);
        copy.transform (db::Trans (array.a () * long (ia) + array.b () * long (ib)));
        emit_text (block, layer_id, copy, prop_id);
      }
    }
    return;
  }

  const auto irregular_it = maps.irregular.find (rep_id);
  if (irregular_it != maps.irregular.end () && irregular_it->second != nullptr) {
    emit_text (block, layer_id, text, prop_id);
    for (const db::Vector &offset : *irregular_it->second) {
      db::Text copy (text);
      copy.transform (db::Trans (offset));
      emit_text (block, layer_id, copy, prop_id);
    }
    return;
  }

  emit_text (block, layer_id, text, prop_id);
}

template <class Obj, class EmitPlain>
void emit_container (const lstr::Compressed::compressed_container<Obj> &container,
                     EmitPlain &&emit_plain)
{
  for (const Obj &obj : container.plain) {
    emit_plain (obj, 0, 0);
  }
  for (const auto &entry : container.with_properties) {
    emit_plain (entry.first, entry.second, 0);
  }
  for (const auto &entry : container.array) {
    emit_plain (entry.first, 0, entry.second);
  }
  for (const auto &entry : container.array_with_properties) {
    emit_plain (entry.first.first, entry.first.second, entry.second);
  }
}

void emit_compressed_shapes (room::Block &block, std::uint32_t layer_id, lstr::Compressed &compressed)
{
  const RepMaps maps = build_rep_maps (compressed);

  emit_container (compressed.get_container<db::Box> (),
                  [&] (const db::Box &box, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_box (block, layer_id, box, prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::Polygon> (),
                  [&] (const db::Polygon &poly, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_polygon (block, layer_id, polygon_points (poly), prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::SimplePolygon> (),
                  [&] (const db::SimplePolygon &poly, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_polygon (block, layer_id, polygon_points (poly), prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::Path> (),
                  [&] (const db::Path &path, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_path (block, layer_id, path, prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::Text> (),
                  [&] (const db::Text &text, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_text_with_rep (block, layer_id, text, prop_id, rep_id, maps);
                  });
}

void emit_instance (room::Block &block, const db::Layout &layout, const db::CellInstArray &arr,
                    db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  // compress_instances always delivers size-1 CellInstArray objects; repetition is in rep_id.
  const char *child_name = layout.cell_name (arr.object ().cell_index ());
  if (child_name == nullptr || *child_name == '\0') {
    return;
  }

  room::Instance instance (child_name, make_transform (arr.complex_trans ()));

  if (rep_id != 0) {
    const auto regular_it = maps.regular.find (rep_id);
    if (regular_it != maps.regular.end () && ! regular_it->second.is_null ()
        && (regular_it->second.na () > 1 || regular_it->second.nb () > 1)) {
      const lstr::RegularArray &array = regular_it->second;
      instance.setArray (static_cast<std::uint32_t> (array.na ()), static_cast<std::uint32_t> (array.nb ()),
                         cast_coord (array.a ().x ()), cast_coord (array.b ().y ()),
                         cast_coord (array.a ().y ()), cast_coord (array.b ().x ()));
    } else {
      const auto irregular_it = maps.irregular.find (rep_id);
      if (irregular_it != maps.irregular.end () && irregular_it->second != nullptr
          && ! irregular_it->second->empty ()) {
        std::vector<room::Point> copies;
        copies.reserve (irregular_it->second->size () + 1);
        copies.push_back (room::Point { 0, 0 });
        for (const db::Vector &offset : *irregular_it->second) {
          copies.push_back (make_point (offset));
        }
        instance.setOffsets (std::move (copies));
      }
    }
  }

  block.instances ().emplace_back (std::move (instance));
  if (prop_id != 0) {
    block.instances ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_compressed_instances (room::Block &block, const db::Layout &layout, lstr::Compressed &compressed)
{
  const RepMaps maps = build_rep_maps (compressed);
  emit_container (compressed.get_container<db::CellInstArray> (),
                  [&] (const db::CellInstArray &arr, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_instance (block, layout, arr, prop_id, rep_id, maps);
                  });
}

} // namespace

void Writer::write (db::Layout &layout, tl::OutputStream &stream, const db::SaveLayoutOptions &options)
{
  const std::string lib_name = resolve_lib_name (layout, options, stream);
  layout.add_meta_info ("libname", db::MetaInfo (tl::to_string (tr ("Library name")), lib_name));

  room::Database database;
  database.setGenerator ("KLayout ROOM writer");
  database.lib () = room::Lib (lib_name);
  database.lib ().properties () = properties_from_klayout (layout.prop_id ());

  const double layout_dbu = layout.dbu ();
  const double dbu_per_micron = (layout_dbu > 0.0) ? (1.0 / layout_dbu) : 1000.0;

  std::map<std::pair<std::uint16_t, std::uint16_t>, room::LayerSpec> lib_layer_catalog;
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

  std::set<db::cell_index_type> cells_to_write;
  for (db::Layout::const_iterator cit = layout.begin (); cit != layout.end (); ++cit) {
    cells_to_write.insert (cit->cell_index ());
  }

  for (db::Layout::const_iterator cit = layout.begin (); cit != layout.end (); ++cit) {
    const db::Cell &cell = *cit;
    const db::cell_index_type cell_index = cell.cell_index ();
    const char *cell_name = layout.cell_name (cell_index);
    if (cell_name == nullptr || *cell_name == '\0') {
      continue;
    }

    room::Cell &core_cell = database.lib ().getOrCreateCell (cell_name);
    core_cell.properties () = properties_from_klayout (cell.prop_id ());
    room::CellContent &content = core_cell.getOrCreateContent (room::ViewType::Layout, dbu_per_micron);
    content.setDbuPerMicron (dbu_per_micron);
    content.layers ().clear ();

    auto layer_id_for = [&content, &database] (const db::LayerProperties &properties) -> std::uint32_t {
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
      if (cell.shapes (li).empty ()) {
        continue;
      }

      const std::uint32_t layer_id = layer_id_for (properties);
      lstr::Compressed compressed;
      compressed.compress_shapes (cell.shapes (li), k_oasis_compression_level, k_oasis_recompress);
      emit_compressed_shapes (content.block (), layer_id, compressed);
    }

    {
      lstr::Compressed compressed;
      compressed.compress_instances (cell.begin (), cells_to_write, k_oasis_compression_level);
      emit_compressed_instances (content.block (), layout, compressed);
    }

    content.block ().recomputeBBox ();
  }

  write_database_to_stream (database, stream);
}

} // namespace roomdb
