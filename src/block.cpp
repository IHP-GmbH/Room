#include "block.h"

namespace core {

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

} // namespace core
