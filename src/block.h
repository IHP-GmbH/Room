#pragma once

#include "instance.h"
#include "net.h"
#include "shape.h"
#include "types.h"

#include <vector>

namespace core {

class Block {
public:
    std::vector<Shape> &                                shapes() { return m_shapes; }
    const std::vector<Shape> &                          shapes() const { return m_shapes; }

    std::vector<Instance> &                             instances() { return m_instances; }
    const std::vector<Instance> &                       instances() const { return m_instances; }

    std::vector<Net> &                                  nets() { return m_nets; }
    const std::vector<Net> &                            nets() const { return m_nets; }

    const Box &                                         bbox() const { return m_bbox; }
    void                                                recomputeBBox();

    static Box                                          computeBBox(const Block &block);

private:
    std::vector<Shape>                                  m_shapes;
    std::vector<Instance>                               m_instances;
    std::vector<Net>                                    m_nets;
    Box                                                 m_bbox;
};

} // namespace core
