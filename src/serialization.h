#pragma once

#include "database.h"

#include <database.capnp.h>

namespace room {

/*!****************************************************************************************
 * \brief Writes a Database into a Cap'n Proto Database builder.
 * \param root     Cap'n Proto builder for the root Database message.
 * \param db       In-memory database to serialize.
 * \param options  Save options (compact vs verbose geometry).
 *****************************************************************************************/
void writeDatabase(schema::Database::Builder root, const Database &db, SaveOptions options = {});

/*!****************************************************************************************
 * \brief Reads a Database from a Cap'n Proto Database reader.
 * \param root     Cap'n Proto reader for the root Database message.
 * \return         Populated in-memory Database.
 *****************************************************************************************/
Database readDatabase(schema::Database::Reader root);

} // namespace room
