#include "instance.h"

namespace cdb {

Instance::Instance(std::string cellName, Transform transform)
    : cellName_(std::move(cellName)), transform_(transform) {}

} // namespace cdb
