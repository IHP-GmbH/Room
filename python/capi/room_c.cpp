#include "room_c.h"

#include "database.h"
#include "file_summary.h"
#include "gds_exporter.h"
#include "gds_importer.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "xschem_exporter.h"
#include "xschem_importer.h"

#ifndef ROOM_HAS_OAS
#define ROOM_HAS_OAS 0
#endif

#if ROOM_HAS_OAS
#include "oas_exporter.h"
#include "oas_importer.h"
#endif

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

struct RoomDb {
    room::Database db;
};

namespace {

void set_error(char *err, size_t err_len, const std::string &msg)
{
    if (!err || err_len == 0) {
        return;
    }
    std::snprintf(err, err_len, "%s", msg.c_str());
}

void clear_error(char *err, size_t err_len)
{
    if (err && err_len > 0) {
        err[0] = '\0';
    }
}

void copy_str(char *dst, size_t dst_len, const std::string &src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    std::snprintf(dst, dst_len, "%s", src.c_str());
}

room::ViewType to_view(int view)
{
    switch (view) {
    case 1:
        return room::ViewType::Schematic;
    case 2:
        return room::ViewType::Symbol;
    case 3:
        return room::ViewType::Abstract;
    case 4:
        return room::ViewType::EmModel;
    default:
        return room::ViewType::Layout;
    }
}

int from_view(room::ViewType view)
{
    switch (view) {
    case room::ViewType::Schematic:
        return 1;
    case room::ViewType::Symbol:
        return 2;
    case room::ViewType::Abstract:
        return 3;
    case room::ViewType::EmModel:
        return 4;
    default:
        return 0;
    }
}

room::CellContent *content_at(room::Database &db, int cell_index, int content_index)
{
    auto &cells = db.lib().cells();
    if (cell_index < 0 || static_cast<size_t>(cell_index) >= cells.size()) {
        return nullptr;
    }
    auto &contents = cells[static_cast<size_t>(cell_index)].contents();
    if (content_index < 0 || static_cast<size_t>(content_index) >= contents.size()) {
        return nullptr;
    }
    return &contents[static_cast<size_t>(content_index)];
}

const room::CellContent *content_at(const room::Database &db, int cell_index, int content_index)
{
    const auto &cells = db.lib().cells();
    if (cell_index < 0 || static_cast<size_t>(cell_index) >= cells.size()) {
        return nullptr;
    }
    const auto &contents = cells[static_cast<size_t>(cell_index)].contents();
    if (content_index < 0 || static_cast<size_t>(content_index) >= contents.size()) {
        return nullptr;
    }
    return &contents[static_cast<size_t>(content_index)];
}

std::string pick_cell_name(const room::Database &db, const char *cell_name)
{
    if (cell_name && cell_name[0] != '\0') {
        return cell_name;
    }
    if (!db.fileSummary().primaryCell.empty()) {
        return db.fileSummary().primaryCell;
    }
    if (!db.lib().cells().empty()) {
        return db.lib().cells().front().name();
    }
    throw std::runtime_error("database has no cells to export");
}

} // namespace

extern "C" {

RoomDb *room_db_create(void)
{
    try {
        return new RoomDb{};
    } catch (...) {
        return nullptr;
    }
}

RoomDb *room_db_open(const char *path, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!path) {
        set_error(err, err_len, "path is null");
        return nullptr;
    }
    try {
        auto *handle = new RoomDb{};
        handle->db = room::Database::loadFromFile(path);
        return handle;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return nullptr;
    } catch (...) {
        set_error(err, err_len, "unknown error opening .room file");
        return nullptr;
    }
}

int room_db_save(RoomDb *db, const char *path, int view, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!db || !path) {
        set_error(err, err_len, "null argument");
        return -1;
    }
    try {
        db->db.saveToFile(path, to_view(view));
        return 0;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return -1;
    } catch (...) {
        set_error(err, err_len, "unknown error saving .room file");
        return -1;
    }
}

void room_db_free(RoomDb *db)
{
    delete db;
}

const char *room_db_version(const RoomDb *db)
{
    return db ? db->db.version().c_str() : "";
}

const char *room_db_generator(const RoomDb *db)
{
    return db ? db->db.generator().c_str() : "";
}

const char *room_db_technology(const RoomDb *db)
{
    return db ? db->db.technology().c_str() : "";
}

int room_db_file_view(const RoomDb *db)
{
    return db ? from_view(db->db.fileView()) : 0;
}

void room_db_set_version(RoomDb *db, const char *value)
{
    if (db && value) {
        db->db.setVersion(value);
    }
}

void room_db_set_generator(RoomDb *db, const char *value)
{
    if (db && value) {
        db->db.setGenerator(value);
    }
}

void room_db_set_technology(RoomDb *db, const char *value)
{
    if (db && value) {
        db->db.setTechnology(value);
    }
}

void room_db_set_file_view(RoomDb *db, int view)
{
    if (db) {
        db->db.setFileView(to_view(view));
    }
}

const char *room_db_lib_name(const RoomDb *db)
{
    return db ? db->db.lib().name().c_str() : "";
}

void room_db_set_lib_name(RoomDb *db, const char *name)
{
    if (db && name) {
        db->db.lib().setName(name);
    }
}

uint32_t room_db_summary_cell_count(const RoomDb *db)
{
    return db ? db->db.fileSummary().cellCount : 0;
}

const char *room_db_summary_primary_cell(const RoomDb *db)
{
    return db ? db->db.fileSummary().primaryCell.c_str() : "";
}

int room_db_summary_view(const RoomDb *db)
{
    return db ? from_view(db->db.fileSummary().view) : 0;
}

int room_sniff(const char *path, RoomFileInfoC *out, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!path || !out) {
        set_error(err, err_len, "null argument");
        return -1;
    }
    try {
        const room::RoomFileInfo info = room::sniffRoomFile(path);
        std::memset(out, 0, sizeof(*out));
        copy_str(out->version, sizeof(out->version), info.version);
        copy_str(out->generator, sizeof(out->generator), info.generator);
        copy_str(out->technology, sizeof(out->technology), info.technology);
        copy_str(out->lib_name, sizeof(out->lib_name), info.libName);
        out->view = from_view(info.summary.view);
        out->cell_count = info.summary.cellCount;
        copy_str(out->primary_cell, sizeof(out->primary_cell), info.summary.primaryCell);
        return 0;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return -1;
    } catch (...) {
        set_error(err, err_len, "unknown error sniffing .room file");
        return -1;
    }
}

int room_db_layer_count(const RoomDb *db)
{
    return db ? static_cast<int>(db->db.lib().layers().size()) : 0;
}

int room_db_layer_get(const RoomDb *db, int index, RoomLayerC *out)
{
    if (!db || !out) {
        return -1;
    }
    const auto &layers = db->db.lib().layers();
    if (index < 0 || static_cast<size_t>(index) >= layers.size()) {
        return -1;
    }
    const auto &layer = layers[static_cast<size_t>(index)];
    out->layer_num = layer.layerNum;
    out->data_type = layer.dataType;
    copy_str(out->name, sizeof(out->name), layer.name);
    out->purpose = static_cast<int>(layer.purpose);
    return 0;
}

int room_db_layer_add(RoomDb *db, uint16_t layer_num, uint16_t data_type, const char *name, int purpose)
{
    if (!db) {
        return -1;
    }
    room::LayerPurpose p = room::LayerPurpose::Drawing;
    if (purpose >= 0 && purpose <= static_cast<int>(room::LayerPurpose::Other)) {
        p = static_cast<room::LayerPurpose>(purpose);
    }
    db->db.lib().layers().emplace_back(layer_num, data_type, name ? name : "", p);
    return static_cast<int>(db->db.lib().layers().size()) - 1;
}

int room_db_cell_count(const RoomDb *db)
{
    return db ? static_cast<int>(db->db.lib().cells().size()) : 0;
}

const char *room_db_cell_name(const RoomDb *db, int cell_index)
{
    if (!db) {
        return "";
    }
    const auto &cells = db->db.lib().cells();
    if (cell_index < 0 || static_cast<size_t>(cell_index) >= cells.size()) {
        return "";
    }
    return cells[static_cast<size_t>(cell_index)].name().c_str();
}

int room_db_find_cell(const RoomDb *db, const char *name)
{
    if (!db || !name) {
        return -1;
    }
    const auto &cells = db->db.lib().cells();
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].name() == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int room_db_add_cell(RoomDb *db, const char *name)
{
    if (!db || !name) {
        return -1;
    }
    room::Cell &cell = db->db.lib().getOrCreateCell(name);
    return room_db_find_cell(db, cell.name().c_str());
}

int room_db_cell_content_count(const RoomDb *db, int cell_index)
{
    if (!db) {
        return 0;
    }
    const auto &cells = db->db.lib().cells();
    if (cell_index < 0 || static_cast<size_t>(cell_index) >= cells.size()) {
        return 0;
    }
    return static_cast<int>(cells[static_cast<size_t>(cell_index)].contents().size());
}

int room_db_cell_content_view(const RoomDb *db, int cell_index, int content_index)
{
    if (!db) {
        return -1;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    return content ? from_view(content->viewType()) : -1;
}

double room_db_cell_content_dbu(const RoomDb *db, int cell_index, int content_index)
{
    if (!db) {
        return 0.0;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    return content ? content->dbuPerMicron() : 0.0;
}

int room_db_cell_ensure_content(RoomDb *db, int cell_index, int view, double dbu_per_micron)
{
    if (!db) {
        return -1;
    }
    auto &cells = db->db.lib().cells();
    if (cell_index < 0 || static_cast<size_t>(cell_index) >= cells.size()) {
        return -1;
    }
    room::CellContent &content =
        cells[static_cast<size_t>(cell_index)].getOrCreateContent(to_view(view), dbu_per_micron);
    const auto &contents = cells[static_cast<size_t>(cell_index)].contents();
    for (size_t i = 0; i < contents.size(); ++i) {
        if (&contents[i] == &content) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int room_db_cell_bbox(const RoomDb *db, int cell_index, int content_index, RoomBoxC *out)
{
    if (!db || !out) {
        return -1;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    if (!content) {
        return -1;
    }
    const room::Box &box = content->block().bbox();
    out->llx = box.llx;
    out->lly = box.lly;
    out->urx = box.urx;
    out->ury = box.ury;
    out->empty = box.empty() ? 1 : 0;
    return 0;
}

int room_db_shape_count(const RoomDb *db, int cell_index, int content_index)
{
    if (!db) {
        return 0;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    return content ? static_cast<int>(content->block().shapes().size()) : 0;
}

int room_db_shape_type(const RoomDb *db, int cell_index, int content_index, int shape_index)
{
    if (!db) {
        return -1;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    if (!content) {
        return -1;
    }
    const auto &shapes = content->block().shapes();
    if (shape_index < 0 || static_cast<size_t>(shape_index) >= shapes.size()) {
        return -1;
    }
    return static_cast<int>(shapes[static_cast<size_t>(shape_index)].type());
}

int room_db_shape_rect(const RoomDb *db, int cell_index, int content_index, int shape_index, RoomRectC *out)
{
    if (!db || !out) {
        return -1;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    if (!content) {
        return -1;
    }
    const auto &shapes = content->block().shapes();
    if (shape_index < 0 || static_cast<size_t>(shape_index) >= shapes.size()) {
        return -1;
    }
    const room::Shape::RectData *rect = shapes[static_cast<size_t>(shape_index)].rect();
    if (!rect) {
        return -1;
    }
    out->layer_id = rect->layerId;
    out->llx = rect->box.llx;
    out->lly = rect->box.lly;
    out->urx = rect->box.urx;
    out->ury = rect->box.ury;
    return 0;
}

int room_db_add_rect(RoomDb *db, int cell_index, int content_index, uint32_t layer_id, int64_t llx,
                     int64_t lly, int64_t urx, int64_t ury)
{
    if (!db) {
        return -1;
    }
    room::CellContent *content = content_at(db->db, cell_index, content_index);
    if (!content) {
        return -1;
    }
    room::Shape::RectData data;
    data.layerId = layer_id;
    data.box = room::Box(llx, lly, urx, ury);
    content->block().shapes().emplace_back(std::move(data));
    content->block().recomputeBBox();
    return static_cast<int>(content->block().shapes().size()) - 1;
}

int room_db_instance_count(const RoomDb *db, int cell_index, int content_index)
{
    if (!db) {
        return 0;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    return content ? static_cast<int>(content->block().instances().size()) : 0;
}

int room_db_instance_get(const RoomDb *db, int cell_index, int content_index, int inst_index,
                         RoomInstanceC *out)
{
    if (!db || !out) {
        return -1;
    }
    const room::CellContent *content = content_at(db->db, cell_index, content_index);
    if (!content) {
        return -1;
    }
    const auto &instances = content->block().instances();
    if (inst_index < 0 || static_cast<size_t>(inst_index) >= instances.size()) {
        return -1;
    }
    const room::Instance &inst = instances[static_cast<size_t>(inst_index)];
    copy_str(out->cell_name, sizeof(out->cell_name), inst.cellName());
    out->x = inst.transform().x;
    out->y = inst.transform().y;
    out->orient = static_cast<int>(inst.transform().orient);
    out->mag = inst.transform().mag;
    return 0;
}

int room_db_add_instance(RoomDb *db, int cell_index, int content_index, const char *cell_name, int64_t x,
                         int64_t y, int orient, double mag)
{
    if (!db) {
        return -1;
    }
    room::CellContent *content = content_at(db->db, cell_index, content_index);
    if (!content || !cell_name) {
        return -1;
    }
    room::Orient o = room::Orient::R0;
    if (orient >= 0 && orient <= static_cast<int>(room::Orient::MY90)) {
        o = static_cast<room::Orient>(orient);
    }
    content->block().instances().emplace_back(cell_name, room::Transform(x, y, o, mag));
    return static_cast<int>(content->block().instances().size()) - 1;
}

RoomDb *room_from_gds(const char *gds_path, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!gds_path) {
        set_error(err, err_len, "path is null");
        return nullptr;
    }
    try {
        room::GdsImporter importer;
        auto *handle = new RoomDb{};
        handle->db = importer.importFile(gds_path);
        if (!importer.errors().empty()) {
            set_error(err, err_len, importer.errors().front());
            delete handle;
            return nullptr;
        }
        return handle;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return nullptr;
    } catch (...) {
        set_error(err, err_len, "unknown error importing GDS");
        return nullptr;
    }
}

int room_to_gds(const RoomDb *db, const char *gds_path, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!db || !gds_path) {
        set_error(err, err_len, "null argument");
        return -1;
    }
    try {
        room::GdsExporter exporter;
        exporter.exportFile(db->db, gds_path);
        if (!exporter.errors().empty()) {
            set_error(err, err_len, exporter.errors().front());
            return -1;
        }
        return 0;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return -1;
    } catch (...) {
        set_error(err, err_len, "unknown error exporting GDS");
        return -1;
    }
}

RoomDb *room_from_qucs(const char *sch_path, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!sch_path) {
        set_error(err, err_len, "path is null");
        return nullptr;
    }
    try {
        room::QucsImporter importer;
        auto *handle = new RoomDb{};
        handle->db = importer.importFile(sch_path);
        if (!importer.errors().empty()) {
            set_error(err, err_len, importer.errors().front());
            delete handle;
            return nullptr;
        }
        return handle;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return nullptr;
    } catch (...) {
        set_error(err, err_len, "unknown error importing Qucs");
        return nullptr;
    }
}

int room_to_qucs(const RoomDb *db, const char *sch_path, const char *cell_name, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!db || !sch_path) {
        set_error(err, err_len, "null argument");
        return -1;
    }
    try {
        const std::string cell = pick_cell_name(db->db, cell_name);
        room::QucsExporter exporter;
        exporter.exportCell(db->db, cell, sch_path);
        if (!exporter.errors().empty()) {
            set_error(err, err_len, exporter.errors().front());
            return -1;
        }
        return 0;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return -1;
    } catch (...) {
        set_error(err, err_len, "unknown error exporting Qucs");
        return -1;
    }
}

RoomDb *room_from_xschem(const char *path, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!path) {
        set_error(err, err_len, "path is null");
        return nullptr;
    }
    try {
        room::XschemImporter importer;
        auto *handle = new RoomDb{};
        handle->db = importer.importFile(path);
        if (!importer.errors().empty()) {
            set_error(err, err_len, importer.errors().front());
            delete handle;
            return nullptr;
        }
        return handle;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return nullptr;
    } catch (...) {
        set_error(err, err_len, "unknown error importing Xschem");
        return nullptr;
    }
}

int room_to_xschem(const RoomDb *db, const char *output_path, const char *cell_name, char *err, size_t err_len)
{
    clear_error(err, err_len);
    if (!db || !output_path) {
        set_error(err, err_len, "null argument");
        return -1;
    }
    try {
        room::XschemExporter exporter;
        // Directory path (ends with slash or has no extension): export all cells.
        const std::string out(output_path);
        const bool looksLikeDir =
            out.empty() || out.back() == '/' || out.back() == '\\' ||
            (out.find('.') == std::string::npos);
        if (looksLikeDir && !(cell_name && cell_name[0] != '\0')) {
            exporter.exportAll(db->db, out);
        } else {
            const std::string cell = pick_cell_name(db->db, cell_name);
            exporter.exportCell(db->db, cell, out);
        }
        if (!exporter.errors().empty()) {
            set_error(err, err_len, exporter.errors().front());
            return -1;
        }
        return 0;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return -1;
    } catch (...) {
        set_error(err, err_len, "unknown error exporting Xschem");
        return -1;
    }
}

int room_has_oas(void)
{
#if ROOM_HAS_OAS
    return 1;
#else
    return 0;
#endif
}

RoomDb *room_from_oas(const char *oas_path, char *err, size_t err_len)
{
    clear_error(err, err_len);
#if !ROOM_HAS_OAS
    set_error(err, err_len, "OAS support was not built (zlib/room_oas missing)");
    (void)oas_path;
    return nullptr;
#else
    if (!oas_path) {
        set_error(err, err_len, "path is null");
        return nullptr;
    }
    try {
        room::OasImporter importer;
        auto *handle = new RoomDb{};
        handle->db = importer.importFile(oas_path);
        if (!importer.errors().empty()) {
            set_error(err, err_len, importer.errors().front());
            delete handle;
            return nullptr;
        }
        return handle;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return nullptr;
    } catch (...) {
        set_error(err, err_len, "unknown error importing OASIS");
        return nullptr;
    }
#endif
}

int room_to_oas(const RoomDb *db, const char *oas_path, char *err, size_t err_len)
{
    clear_error(err, err_len);
#if !ROOM_HAS_OAS
    set_error(err, err_len, "OAS support was not built (zlib/room_oas missing)");
    (void)db;
    (void)oas_path;
    return -1;
#else
    if (!db || !oas_path) {
        set_error(err, err_len, "null argument");
        return -1;
    }
    try {
        room::OasExporter exporter;
        exporter.exportFile(db->db, oas_path);
        if (!exporter.errors().empty()) {
            set_error(err, err_len, exporter.errors().front());
            return -1;
        }
        return 0;
    } catch (const std::exception &ex) {
        set_error(err, err_len, ex.what());
        return -1;
    } catch (...) {
        set_error(err, err_len, "unknown error exporting OASIS");
        return -1;
    }
#endif
}

} // extern "C"
