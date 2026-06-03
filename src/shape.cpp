#include "shape.h"

namespace cdb {

Shape::Shape(RectData data) : type_(Type::Rect), rect_(std::move(data)) {}
Shape::Shape(PolygonData data) : type_(Type::Polygon), polygon_(std::move(data)) {}
Shape::Shape(PathData data) : type_(Type::Path), path_(std::move(data)) {}
Shape::Shape(TextData data) : type_(Type::Text), text_(std::move(data)) {}

const Shape::RectData *Shape::rect() const
{
    return type_ == Type::Rect ? &rect_ : nullptr;
}

const Shape::PolygonData *Shape::polygon() const
{
    return type_ == Type::Polygon ? &polygon_ : nullptr;
}

const Shape::PathData *Shape::path() const
{
    return type_ == Type::Path ? &path_ : nullptr;
}

const Shape::TextData *Shape::text() const
{
    return type_ == Type::Text ? &text_ : nullptr;
}

} // namespace cdb
