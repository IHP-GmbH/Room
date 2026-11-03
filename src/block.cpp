#include "block.h"

namespace room {

/*!****************************************************************************************
 * \brief Computes the axis-aligned bounding box enclosing all shapes in a block.
 * \param block    Block whose shapes are scanned.
 * \return         Bounding box in database units (empty if no geometry).
 *****************************************************************************************/
Box Block::computeBBox(const Block &block)
{
    Box bbox;
    for (const auto &shape : block.shapes()) {
        if (const auto *r = shape.rect()) {
            bbox.expand(r->box);
            if (r->columns > 1 || r->rows > 1) {
                const std::int64_t spanX = static_cast<std::int64_t>(r->columns > 0 ? r->columns - 1 : 0) * r->stepX;
                const std::int64_t spanY = static_cast<std::int64_t>(r->rows > 0 ? r->rows - 1 : 0) * r->stepY;
                bbox.expand(r->box.llx + spanX, r->box.lly + spanY);
                bbox.expand(r->box.urx + spanX, r->box.ury + spanY);
            }
        } else if (const auto *p = shape.polygon()) {
            for (const auto &pt : p->points) {
                bbox.expand(pt.x, pt.y);
            }
        } else if (const auto *path = shape.path()) {
            for (const auto &pt : path->points) {
                bbox.expand(pt.x, pt.y);
            }
        } else if (const auto *text = shape.text()) {
            bbox.expand(text->position.x, text->position.y);
        } else if (const auto *arc = shape.arc()) {
            const std::int64_t r = static_cast<std::int64_t>(arc->radius * 1000.0);
            bbox.expand(arc->center.x - r, arc->center.y - r);
            bbox.expand(arc->center.x + r, arc->center.y + r);
        }
    }
    return bbox;
}

/*!****************************************************************************************
 * \brief Updates the cached bbox from current shapes.
 *****************************************************************************************/
void Block::recomputeBBox()
{
    m_bbox = computeBBox(*this);
}

} // namespace room
