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

Database::Database() = default;

void Database::saveToFile(const std::string &path) const
{
    capnp::MallocMessageBuilder message;
    writeDatabase(message.initRoot<schema::Database>(), *this);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }
    kj::std::StdOutputStream kjOut(out);
    capnp::writeMessage(kjOut, message);
}

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
