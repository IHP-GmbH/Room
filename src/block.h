#pragma once

#include "instance.h"
#include "net.h"
#include "shape.h"
#include "types.h"

#include <vector>

namespace core {

class Block {
public:
    std::vector<Shape> &shapes() { return shapes_; }
    const std::vector<Shape> &shapes() const { return shapes_; }

    std::vector<Instance> &instances() { return instances_; }
    const std::vector<Instance> &instances() const { return instances_; }

    std::vector<Net> &nets() { return nets_; }
    const std::vector<Net> &nets() const { return nets_; }

    const Box &bbox() const { return bbox_; }
    void recomputeBBox();

private:
    std::vector<Shape> shapes_;
    std::vector<Instance> instances_;
    std::vector<Net> nets_;
    Box bbox_;
};

} // namespace core
