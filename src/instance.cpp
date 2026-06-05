#include "instance.h"

namespace core {

Instance::Instance(std::string cellName, Transform transform)
    : cellName_(std::move(cellName)), transform_(transform) {}

} // namespace core
