#include "instance.h"

namespace core {

Instance::Instance(std::string cellName, Transform transform)
    : m_cellName(std::move(cellName)), m_transform(transform) {}

} // namespace core
