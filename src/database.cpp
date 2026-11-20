#include "database.h"
#include "room_file_io.h"
#include "serialization.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include <kj/std/iostream.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace room {

Database::Database() = default;

void Database::recomputeFileSummary(ViewType fileView)
{
    m_fileView = fileView;
    m_fileSummary = FileSummary::fromLib(m_lib, fileView);
}

void Database::setFileSummary(FileSummary summary)
{
    m_fileSummary = std::move(summary);
    m_fileView = m_fileSummary.view;
}

void Database::saveToFile(const std::string &path, ViewType fileView, SaveOptions options)
{
    m_lib.recomputeAllBBoxes();
    m_lib.refreshIndex(fileView);
    recomputeFileSummary(fileView);

    capnp::MallocMessageBuilder message;
    writeDatabase(message.initRoot<schema::Database>(), *this, options);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }

    if (options.compressFile) {
        kj::VectorOutputStream vectorStream;
        capnp::writeMessage(vectorStream, message);
        const kj::ArrayPtr<const kj::byte> bytes = vectorStream.getArray();
        const std::vector<std::uint8_t> framed =
            compressRoomPayload(bytes.begin(), bytes.size(), options.compressLevel);
        if (!out.write(reinterpret_cast<const char *>(framed.data()),
                       static_cast<std::streamsize>(framed.size()))) {
            throw std::runtime_error("Failed to write compressed ROOM file: " + path);
        }
        return;
    }

    kj::std::StdOutputStream kjOut(out);
    capnp::writeMessage(kjOut, message);
}

Database Database::loadFromFile(const std::string &path)
{
    const std::vector<std::uint8_t> buffer = readRoomFileBytes(path);
    if (buffer.size() < sizeof(capnp::word) || buffer.size() % sizeof(capnp::word) != 0) {
        throw std::runtime_error("ROOM file too small: " + path);
    }

    // Cap'n Proto default traversalLimitInWords is 8 Mi words (~64 MiB). Wafer-scale
    // .layout.room files need a limit proportional to the message size.
    capnp::ReaderOptions readerOptions;
    readerOptions.traversalLimitInWords =
        (buffer.size() / sizeof(capnp::word)) * 2 + (1024 * 1024);
    capnp::FlatArrayMessageReader reader(
        kj::arrayPtr(reinterpret_cast<const capnp::word *>(buffer.data()),
                     buffer.size() / sizeof(capnp::word)),
        readerOptions);
    return readDatabase(reader.getRoot<schema::Database>());
}

} // namespace room
