#pragma once

#include "types.h"

#include <vector>

namespace core {

class Shape {
public:
    enum class Type { Rect, Polygon, Path, Text };

    struct RectData {
        Box box;
        std::uint32_t layerId = 0;
    };
    struct PolygonData {
        std::vector<Point> points;
        std::uint32_t layerId = 0;
    };
    struct PathData {
        std::vector<Point> points;
        std::uint32_t width = 0;
        std::uint32_t layerId = 0;
    };
    struct TextData {
        Point position;
        std::string text;
        std::uint32_t layerId = 0;
        std::uint32_t height = 0;
    };

    explicit Shape(RectData data);
    explicit Shape(PolygonData data);
    explicit Shape(PathData data);
    explicit Shape(TextData data);

    Type type() const { return type_; }
    const RectData *rect() const;
    const PolygonData *polygon() const;
    const PathData *path() const;
    const TextData *text() const;

    std::vector<Property> &properties() { return properties_; }
    const std::vector<Property> &properties() const { return properties_; }

private:
    Type type_;
    RectData rect_;
    PolygonData polygon_;
    PathData path_;
    TextData text_;
    std::vector<Property> properties_;
};

} // namespace core
