#include "database.h"
#include "serialization.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/std/iostream.h>

#include <fstream>
#include <stdexcept>

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
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }

    kj::std::StdInputStream kjIn(in);
    capnp::InputStreamMessageReader reader(kjIn);
    return readDatabase(reader.getRoot<schema::Database>());
}

} // namespace core
