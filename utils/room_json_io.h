#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief Semantic JSON for schematic/symbol views (git-friendly).
 *
 * Same ``*.schematic.room`` / ``*.symbol.room`` path; content is either Cap’n Proto
 * binary or room-json text. Toggle in place with convertInPlace* / DOOR menu.
 *****************************************************************************************/
class RoomJsonIo {
public:
    struct Result {
        bool ok = false;
        std::string message;
    };

    /*! Export one schematic or symbol cell view to a JSON file (any path, often ``*.room``). */
    static Result exportToFile(const Database &db,
                               const std::string &cellName,
                               ViewType view,
                               const std::string &jsonPath);

    /*! Import JSON into a Database (single cell, schematic or symbol). */
    static Result importFromFile(const std::string &jsonPath, Database &dbOut);

    /*! Convenience: load binary .room, write JSON (infers view from file when possible). */
    static Result roomFileToJson(const std::string &roomPath,
                                 const std::string &jsonPath,
                                 const std::string &cellName = {});

    /*! Convenience: JSON → binary .room on disk. */
    static Result jsonFileToRoom(const std::string &jsonPath, const std::string &roomPath);

    /*! True when file content is room-json (text starting with ``{``), regardless of extension. */
    static bool isJsonRoom(const std::string &path);

    /*! Load binary ``*.room``, JSON ``*.json``, or JSON stored inside ``*.room``. */
    static Result loadRoomOrJson(const std::string &path, Database &dbOut);

    /*! True when path ends with ``.json``. */
    static bool isRoomJsonPath(const std::string &path);

    /*! ``foo.schematic.room`` ↔ ``foo.schematic.json`` (legacy twin helper). */
    static std::string siblingTwinPath(const std::string &path);

    /*! Overwrite ``*.room`` in place with room-json text (same path). */
    static Result convertInPlaceToJson(const std::string &roomPath, const std::string &cellName = {});

    /*! Overwrite JSON-in-``*.room`` in place with Cap’n Proto binary (same path). */
    static Result convertInPlaceToBinary(const std::string &roomPath);

    /*!
     * Save schematic/symbol keeping the on-disk format of ``path``:
     * JSON content / ``*.json`` → write room-json; otherwise Cap’n Proto.
     * Layout / other views: binary ``saveToFile`` only.
     */
    static Result saveViewWithJsonTwin(Database &db, ViewType view, const std::string &path);

    const std::vector<std::string> &warnings() const { return m_warnings; }
    void clearWarnings() { m_warnings.clear(); }

private:
    mutable std::vector<std::string> m_warnings;
};

} // namespace room
