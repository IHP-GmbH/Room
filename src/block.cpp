#include "block.h"

namespace cdb {

void Block::recomputeBBox()
{
    bbox_ = Box{};
    for (const auto &shape : shapes_) {
        if (const auto *r = shape.rect()) {
            bbox_.expand(r->box);
        } else if (const auto *p = shape.polygon()) {
            for (const auto &pt : p->points) {
                bbox_.expand(pt.x, pt.y);
            }
        } else if (const auto *path = shape.path()) {
            for (const auto &pt : path->points) {
                bbox_.expand(pt.x, pt.y);
            }
        } else if (const auto *text = shape.text()) {
            bbox_.expand(text->position.x, text->position.y);
        }
    }
}

} // namespace cdb
