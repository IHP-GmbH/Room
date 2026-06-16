#ifndef HDR_coredbPropertyBridge
#define HDR_coredbPropertyBridge

#include "dbTypes.h"
#include "property.h"

#include <vector>

namespace coredb
{

db::properties_id_type properties_id_from_core (const std::vector<core::Property> &properties);
std::vector<core::Property> properties_from_klayout (db::properties_id_type prop_id);

} // namespace coredb

#endif
