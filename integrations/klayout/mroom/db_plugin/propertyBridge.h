#ifndef HDR_coredbPropertyBridge
#define HDR_coredbPropertyBridge

#include "dbTypes.h"
#include "property.h"

#include <vector>

namespace roomdb
{

db::properties_id_type properties_id_from_core (const std::vector<room::Property> &properties);
std::vector<room::Property> properties_from_klayout (db::properties_id_type prop_id);

} // namespace roomdb

#endif
