#include "shape.h"

namespace core {

Shape::Shape(RectData data) : m_type(Type::Rect), m_rect(std::move(data)) {}
Shape::Shape(PolygonData data) : m_type(Type::Polygon), m_polygon(std::move(data)) {}
Shape::Shape(PathData data) : m_type(Type::Path), m_path(std::move(data)) {}
Shape::Shape(TextData data) : m_type(Type::Text), m_text(std::move(data)) {}

const Shape::RectData *Shape::rect() const
{
    return m_type == Type::Rect ? &m_rect : nullptr;
}

const Shape::PolygonData *Shape::polygon() const
{
    return m_type == Type::Polygon ? &m_polygon : nullptr;
}

const Shape::PathData *Shape::path() const
{
    return m_type == Type::Path ? &m_path : nullptr;
}

const Shape::TextData *Shape::text() const
{
    return m_type == Type::Text ? &m_text : nullptr;
}

} // namespace core
