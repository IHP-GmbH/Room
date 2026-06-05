#pragma once

#include "types.h"

#include <string>
#include <vector>

namespace core {

class Instance {
public:
    Instance(std::string cellName, Transform transform);

    const std::string &cellName() const { return cellName_; }
    Transform &transform() { return transform_; }
    const Transform &transform() const { return transform_; }

    std::vector<Property> &properties() { return properties_; }
    const std::vector<Property> &properties() const { return properties_; }

private:
    std::string cellName_;
    Transform transform_;
    std::vector<Property> properties_;
};

} // namespace core
