#pragma once

#include "types.h"

#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief The Shape class is a tagged union of rect, polygon, path, and text geometry.
 *
 * Each shape belongs to a layer (layerId) and may carry string key/value properties.
 *****************************************************************************************/
class Shape {
public:
    enum class Type { Rect, Polygon, Path, Text, Arc };

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
    struct ArcData {
        Point center;
        double radius = 0.0;
        double startAngle = 0.0;
        double endAngle = 0.0;
        std::uint32_t width = 0;
        std::uint32_t layerId = 0;
    };

    explicit Shape(RectData data);
    explicit Shape(PolygonData data);
    explicit Shape(PathData data);
    explicit Shape(TextData data);
    explicit Shape(ArcData data);

    Type                                                type() const { return m_type; }
    const RectData *                                    rect() const;
    const PolygonData *                                 polygon() const;
    const PathData *                                    path() const;
    const TextData *                                    text() const;
    const ArcData *                                     arc() const;

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

private:
    Type                                                m_type;
    RectData                                            m_rect;
    PolygonData                                         m_polygon;
    PathData                                            m_path;
    TextData                                            m_text;
    ArcData                                             m_arc;
    std::vector<Property>                               m_properties;
};

} // namespace room
