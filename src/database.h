#pragma once

#include "file_summary.h"
#include "lib.h"

#include <string>

namespace room {

/*!****************************************************************************************
 * \brief Options controlling how a Database is serialized to a .room file.
 *****************************************************************************************/
struct SaveOptions {
    bool compactGeometry = true; /*!< When true, geometry is written to payload.compact. */
    /*! When true (default), compact encode re-detects rect/poly/path repetitions.
     *  Set false when the caller already supplied columns/rows/copies (e.g. KLayout LStream). */
    bool detectRepetition = true;
    /*! When true, wrap the Cap'n Proto payload in a zlib layer (ROOMZ magic). */
    bool compressFile = true;
    /*! zlib level 0..9 (0=store/fastest zlib wrapper, 1=Z_BEST_SPEED, 9=Z_BEST_COMPRESSION).
     *  Ignored when compressFile is false. Default 1. */
    int compressLevel = 1;
};

/*!****************************************************************************************
 * \brief The Database class is the root container for a ROOM design stored in a .room file.
 *
 * A Database holds format metadata (version, generator, technology) and a single Lib with
 * cells, layers, and optional derived index. Use saveToFile() and loadFromFile() for I/O.
 *****************************************************************************************/
class Database {
public:
    Database();

    const std::string &                                 version() const { return m_version; }
    const std::string &                                 generator() const { return m_generator; }
    const std::string &                                 technology() const { return m_technology; }

    void                                                setGenerator(std::string value) { m_generator = std::move(value); }
    void                                                setTechnology(std::string value) { m_technology = std::move(value); }
    void                                                setVersion(std::string value) { m_version = std::move(value); }

    Lib &                                               lib() { return m_lib; }
    const Lib &                                         lib() const { return m_lib; }

    ViewType                                            fileView() const { return m_fileView; }
    void                                                setFileView(ViewType view) { m_fileView = view; }

    const FileSummary &                                 fileSummary() const { return m_fileSummary; }
    void                                                setFileSummary(FileSummary summary);

    void                                                saveToFile(const std::string &path, ViewType fileView, SaveOptions options = {});
    static Database                                     loadFromFile(const std::string &path);

private:
    void                                                recomputeFileSummary(ViewType fileView);

    std::string                                         m_version = "1.0";
    std::string                                         m_generator;
    std::string                                         m_technology;
    ViewType                                            m_fileView = ViewType::Layout;
    FileSummary                                         m_fileSummary;
    Lib                                                 m_lib{"default"};
};

} // namespace room
