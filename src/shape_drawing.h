// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include <algorithm>
#include <cmath>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

enum class DrawingShape { Rectangle, Ellipse, Triangle, Polygon };

struct DrawingPoint { double x, y; };

struct DrawingBounds {
    double left, top, right, bottom;
    double Width() const { return right - left; }
    double Height() const { return bottom - top; }
};

// ASS layout uses the control-point box, including Bezier and B-spline controls.
// Keep the original commands intact: resizing is applied with fscx/fscy tags.
inline std::optional<DrawingBounds> ParseDrawingBounds(std::string const& text) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    std::optional<DrawingBounds> bounds;
    bool moved = false, drawn = false, spline = false;
    while (input >> std::ws && !input.eof()) {
        char command;
        if (!(input >> command) || std::string("mnlbspc").find(command) == std::string::npos)
            return std::nullopt;
        if (!moved && command != 'm') return std::nullopt;
        if (command == 'c') {
            if (!spline) return std::nullopt;
            spline = false;
            continue;
        }
        if (command == 'p' && !spline) return std::nullopt;
        size_t count = 0;
        while (input >> std::ws && !input.eof()) {
            if (std::string("mnlbspc").find(static_cast<char>(input.peek())) != std::string::npos)
                break;
            double x, y;
            if (!(input >> x >> y) || !std::isfinite(x) || !std::isfinite(y)
                || std::abs(x) > 1000000 || std::abs(y) > 1000000)
                return std::nullopt;
            if (!bounds) bounds = DrawingBounds{x, y, x, y};
            else {
                bounds->left = std::min(bounds->left, x);
                bounds->top = std::min(bounds->top, y);
                bounds->right = std::max(bounds->right, x);
                bounds->bottom = std::max(bounds->bottom, y);
            }
            ++count;
        }
        if (!count || (command == 'b' && count % 3) || (command == 's' && count < 3))
            return std::nullopt;
        moved = true;
        if (command != 'm' && command != 'n') drawn = true;
        if (command == 's') spline = true;
        else if (command != 'p') spline = false;
    }
    if (!drawn || !bounds || bounds->Width() <= 0 || bounds->Height() <= 0)
        return std::nullopt;
    return bounds;
}

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
