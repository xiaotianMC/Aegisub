// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

enum class DrawingShape { Rectangle, Ellipse, Triangle, Polygon };

struct DrawingPoint { double x, y; };

// Work in script coordinates so Shift remains correct at any preview aspect ratio.
inline DrawingPoint ConstrainDrawingPoint(DrawingPoint start, DrawingPoint end,
    DrawingPoint bounds, DrawingShape shape) {
    double dx = end.x - start.x, dy = end.y - start.y;
    if (shape == DrawingShape::Polygon) {
        constexpr double step = 3.14159265358979323846 / 4;
        double angle = std::round(std::atan2(dy, dx) / step) * step;
        double length = std::hypot(dx, dy);
        dx = std::cos(angle) * length;
        dy = std::sin(angle) * length;
        double scale = 1;
        if (std::abs(dx) > 1e-8)
            scale = std::min(scale, (dx < 0 ? start.x : bounds.x - start.x) / std::abs(dx));
        if (std::abs(dy) > 1e-8)
            scale = std::min(scale, (dy < 0 ? start.y : bounds.y - start.y) / std::abs(dy));
        return {start.x + dx * scale, start.y + dy * scale};
    }
    double ratio = shape == DrawingShape::Triangle ? std::sqrt(3.0) / 2 : 1;
    double side = std::max(std::abs(dx), std::abs(dy) / ratio);
    side = std::min({side, dx < 0 ? start.x : bounds.x - start.x,
        (dy < 0 ? start.y : bounds.y - start.y) / ratio});
    return {start.x + (dx < 0 ? -side : side),
        start.y + (dy < 0 ? -side : side) * ratio};
}

inline std::string ShapeDrawingPath(DrawingShape shape, double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1)
        return {};
    std::ostringstream path;
    path.imbue(std::locale::classic());
    if (shape == DrawingShape::Rectangle)
        path << "m 0 0 l " << width << " 0 " << width << ' ' << height << " 0 " << height;
    else if (shape == DrawingShape::Triangle)
        path << "m " << width / 2 << " 0 l " << width << ' ' << height << " 0 " << height;
    else if (shape == DrawingShape::Ellipse) {
        // Four cubic Bezier arcs, using the standard circle approximation.
        double rx = width / 2, ry = height / 2;
        double cx = rx * 0.5522847498307936, cy = ry * 0.5522847498307936;
        path << "m " << rx << " 0 b "
            << rx + cx << " 0 " << width << ' ' << ry - cy << ' ' << width << ' ' << ry << ' '
            << width << ' ' << ry + cy << ' ' << rx + cx << ' ' << height << ' ' << rx << ' ' << height << ' '
            << rx - cx << ' ' << height << " 0 " << ry + cy << " 0 " << ry << ' '
            << "0 " << ry - cy << ' ' << rx - cx << " 0 " << rx << " 0";
    }
    return path.str();
}

inline std::string PolygonDrawingPath(std::vector<DrawingPoint> const& points) {
    if (points.size() < 3) return {};
    for (auto p : points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return {};
    // Reject collinear clicks; self-intersecting paths are valid ASS drawings.
    auto a = points.front();
    auto b = std::find_if(points.begin(), points.end(), [=](DrawingPoint p) {
        return p.x != a.x || p.y != a.y;
    });
    if (b == points.end() || std::none_of(points.begin(), points.end(), [=](DrawingPoint p) {
        return std::abs((b->x - a.x) * (p.y - a.y) - (b->y - a.y) * (p.x - a.x)) >= 1;
    })) return {};
    std::ostringstream path;
    path.imbue(std::locale::classic());
    path << "m " << a.x << ' ' << a.y << " l";
    for (size_t i = 1; i < points.size(); ++i)
        path << ' ' << points[i].x << ' ' << points[i].y;
    return path.str();
}
