#pragma once

#include "database.h"

#include <database.capnp.h>

namespace core {

void writeDatabase(schema::Database::Builder root, const Database &db);
Database readDatabase(schema::Database::Reader root);

} // namespace core
