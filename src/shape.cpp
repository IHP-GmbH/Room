#include "shape.h"

namespace core {

/*!****************************************************************************************
 * \brief Constructs a rectangle shape.
 * \param data     Rectangle geometry and layer id.
 *****************************************************************************************/
Shape::Shape(RectData data) : m_type(Type::Rect), m_rect(std::move(data)) {}

/*!****************************************************************************************
 * \brief Constructs a polygon shape.
 * \param data     Polygon vertices and layer id.
 *****************************************************************************************/
Shape::Shape(PolygonData data) : m_type(Type::Polygon), m_polygon(std::move(data)) {}

/*!****************************************************************************************
 * \brief Constructs a path shape.
 * \param data     Path centerline, width, and layer id.
 *****************************************************************************************/
Shape::Shape(PathData data) : m_type(Type::Path), m_path(std::move(data)) {}

/*!****************************************************************************************
 * \brief Constructs a text shape.
 * \param data     Label position, string, height, and layer id.
 *****************************************************************************************/
Shape::Shape(TextData data) : m_type(Type::Text), m_text(std::move(data)) {}

Shape::Shape(ArcData data) : m_type(Type::Arc), m_arc(std::move(data)) {}

/*!****************************************************************************************
 * \brief Returns rectangle data when type() is Rect.
 * \return         Pointer to rect data, or nullptr for other types.
 *****************************************************************************************/
const Shape::RectData *Shape::rect() const
{
    return m_type == Type::Rect ? &m_rect : nullptr;
}

/*!****************************************************************************************
 * \brief Returns polygon data when type() is Polygon.
 * \return         Pointer to polygon data, or nullptr for other types.
 *****************************************************************************************/
const Shape::PolygonData *Shape::polygon() const
{
    return m_type == Type::Polygon ? &m_polygon : nullptr;
}

/*!****************************************************************************************
 * \brief Returns path data when type() is Path.
 * \return         Pointer to path data, or nullptr for other types.
 *****************************************************************************************/
const Shape::PathData *Shape::path() const
{
    return m_type == Type::Path ? &m_path : nullptr;
}

/*!****************************************************************************************
 * \brief Returns text data when type() is Text.
 * \return         Pointer to text data, or nullptr for other types.
 *****************************************************************************************/
const Shape::TextData *Shape::text() const
{
    return m_type == Type::Text ? &m_text : nullptr;
}

const Shape::ArcData *Shape::arc() const
{
    return m_type == Type::Arc ? &m_arc : nullptr;
}

} // namespace core
