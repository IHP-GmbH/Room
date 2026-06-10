#include "database.h"
#include "serialization.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include <kj/std/iostream.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief Constructs an empty Database with default format version and library name.
 *****************************************************************************************/
Database::Database() = default;

/*!****************************************************************************************
 * \brief Serializes this database to a binary .core file.
 *
 * Recomputes cell bounding boxes and refreshes the library index before writing.
 *
 * \param path     Output file path.
 * \param options  Encoding options; compact geometry is the default.
 *****************************************************************************************/
void Database::saveToFile(const std::string &path, SaveOptions options)
{
    m_lib.recomputeAllBBoxes();
    m_lib.refreshIndex();

    capnp::MallocMessageBuilder message;
    writeDatabase(message.initRoot<schema::Database>(), *this, options);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }
    kj::std::StdOutputStream kjOut(out);
    capnp::writeMessage(kjOut, message);
}

/*!****************************************************************************************
 * \brief Loads a database from a binary .core file.
 *
 * Auto-detects compact vs verbose geometry in each view payload.
 *
 * \param path     Input .core file path.
 * \return         Deserialized database.
 *****************************************************************************************/
Database Database::loadFromFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }

    const std::streamsize fileSize = in.tellg();
    if (fileSize <= 0) {
        throw std::runtime_error("Cannot read file size: " + path);
    }
    in.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<std::size_t>(fileSize));
    if (!in.read(buffer.data(), fileSize)) {
        throw std::runtime_error("Failed to read file: " + path);
    }

    kj::ArrayInputStream inputStream(
        kj::arrayPtr(reinterpret_cast<const kj::byte *>(buffer.data()), buffer.size()));
    capnp::InputStreamMessageReader reader(inputStream);
    return readDatabase(reader.getRoot<schema::Database>());
}

} // namespace core
