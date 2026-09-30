#ifndef ROOM_C_H
#define ROOM_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef ROOM_C_EXPORTS
#    define ROOM_C_API __declspec(dllexport)
#  else
#    define ROOM_C_API __declspec(dllimport)
#  endif
#else
#  define ROOM_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RoomDb RoomDb;

typedef struct RoomFileInfoC {
    char version[64];
    char generator[256];
    char technology[256];
    char lib_name[256];
    int view; /* 0=layout, 1=schematic, 2=symbol, 3=abstract */
    uint32_t cell_count;
    char primary_cell[256];
} RoomFileInfoC;

typedef struct RoomBoxC {
    int64_t llx;
    int64_t lly;
    int64_t urx;
    int64_t ury;
    int empty;
} RoomBoxC;

typedef struct RoomRectC {
    uint32_t layer_id;
    int64_t llx;
    int64_t lly;
    int64_t urx;
    int64_t ury;
} RoomRectC;

typedef struct RoomInstanceC {
    char cell_name[256];
    int64_t x;
    int64_t y;
    int orient; /* Orient enum ordinal */
    double mag;
} RoomInstanceC;

typedef struct RoomLayerC {
    uint16_t layer_num;
    uint16_t data_type;
    char name[128];
    int purpose; /* LayerPurpose ordinal */
} RoomLayerC;

/* ---- lifecycle ---- */
ROOM_C_API RoomDb *room_db_create(void);
ROOM_C_API RoomDb *room_db_open(const char *path, char *err, size_t err_len);
ROOM_C_API int room_db_save(RoomDb *db, const char *path, int view, char *err, size_t err_len);
ROOM_C_API void room_db_free(RoomDb *db);

/* ---- metadata ---- */
ROOM_C_API const char *room_db_version(const RoomDb *db);
ROOM_C_API const char *room_db_generator(const RoomDb *db);
ROOM_C_API const char *room_db_technology(const RoomDb *db);
ROOM_C_API int room_db_file_view(const RoomDb *db);
ROOM_C_API void room_db_set_version(RoomDb *db, const char *value);
ROOM_C_API void room_db_set_generator(RoomDb *db, const char *value);
ROOM_C_API void room_db_set_technology(RoomDb *db, const char *value);
ROOM_C_API void room_db_set_file_view(RoomDb *db, int view);

ROOM_C_API const char *room_db_lib_name(const RoomDb *db);
ROOM_C_API void room_db_set_lib_name(RoomDb *db, const char *name);

ROOM_C_API uint32_t room_db_summary_cell_count(const RoomDb *db);
ROOM_C_API const char *room_db_summary_primary_cell(const RoomDb *db);
ROOM_C_API int room_db_summary_view(const RoomDb *db);

/* ---- sniff (no full decode of geometry) ---- */
ROOM_C_API int room_sniff(const char *path, RoomFileInfoC *out, char *err, size_t err_len);

/* ---- layers (library-wide) ---- */
ROOM_C_API int room_db_layer_count(const RoomDb *db);
ROOM_C_API int room_db_layer_get(const RoomDb *db, int index, RoomLayerC *out);
ROOM_C_API int room_db_layer_add(RoomDb *db, uint16_t layer_num, uint16_t data_type,
                                 const char *name, int purpose);

/* ---- cells ---- */
ROOM_C_API int room_db_cell_count(const RoomDb *db);
ROOM_C_API const char *room_db_cell_name(const RoomDb *db, int cell_index);
ROOM_C_API int room_db_find_cell(const RoomDb *db, const char *name);
ROOM_C_API int room_db_add_cell(RoomDb *db, const char *name);

ROOM_C_API int room_db_cell_content_count(const RoomDb *db, int cell_index);
ROOM_C_API int room_db_cell_content_view(const RoomDb *db, int cell_index, int content_index);
ROOM_C_API double room_db_cell_content_dbu(const RoomDb *db, int cell_index, int content_index);
ROOM_C_API int room_db_cell_ensure_content(RoomDb *db, int cell_index, int view, double dbu_per_micron);
ROOM_C_API int room_db_cell_bbox(const RoomDb *db, int cell_index, int content_index, RoomBoxC *out);

/* ---- shapes ---- */
ROOM_C_API int room_db_shape_count(const RoomDb *db, int cell_index, int content_index);
ROOM_C_API int room_db_shape_type(const RoomDb *db, int cell_index, int content_index, int shape_index);
ROOM_C_API int room_db_shape_rect(const RoomDb *db, int cell_index, int content_index, int shape_index,
                                  RoomRectC *out);
ROOM_C_API int room_db_add_rect(RoomDb *db, int cell_index, int content_index, uint32_t layer_id,
                                int64_t llx, int64_t lly, int64_t urx, int64_t ury);

/* ---- instances ---- */
ROOM_C_API int room_db_instance_count(const RoomDb *db, int cell_index, int content_index);
ROOM_C_API int room_db_instance_get(const RoomDb *db, int cell_index, int content_index, int inst_index,
                                    RoomInstanceC *out);
ROOM_C_API int room_db_add_instance(RoomDb *db, int cell_index, int content_index, const char *cell_name,
                                    int64_t x, int64_t y, int orient, double mag);

/* ---- convert ---- */
ROOM_C_API RoomDb *room_from_gds(const char *gds_path, char *err, size_t err_len);
ROOM_C_API int room_to_gds(const RoomDb *db, const char *gds_path, char *err, size_t err_len);

ROOM_C_API RoomDb *room_from_qucs(const char *sch_path, char *err, size_t err_len);
ROOM_C_API int room_to_qucs(const RoomDb *db, const char *sch_path, const char *cell_name,
                            char *err, size_t err_len);

ROOM_C_API RoomDb *room_from_xschem(const char *path, char *err, size_t err_len);
ROOM_C_API int room_to_xschem(const RoomDb *db, const char *output_path, const char *cell_name,
                              char *err, size_t err_len);

ROOM_C_API int room_has_oas(void);
ROOM_C_API RoomDb *room_from_oas(const char *oas_path, char *err, size_t err_len);
ROOM_C_API int room_to_oas(const RoomDb *db, const char *oas_path, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif

#endif /* ROOM_C_H */
