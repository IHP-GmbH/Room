#pragma once

// Facade header: include the small type headers for convenience.

#include "enums.h"
#include "point.h"
#include "box.h"
#include "transform.h"
#include "property.h"
#include "layer_spec.h"

namespace cdb {

// All types live in individual headers. This file remains a single
// include point for code that uses `#include "types.h"`.

} // namespace cdb
