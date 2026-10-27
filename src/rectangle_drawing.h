// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include <cmath>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

// Recognize the single, axis-aligned rectangle produced by our drawing tool.
// Other drawings must remain untouched by the rectangle editor.
inline std::optional<std::pair<double, double>> ParseRectangleDrawing(std::string const& text) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    char move, line;
    double x0, y0, x1, y1, x2, y2, x3, y3;
    if (!(input >> move >> x0 >> y0 >> line >> x1 >> y1 >> x2 >> y2 >> x3 >> y3))
        return std::nullopt;
    input >> std::ws;
    if (!input.eof() || move != 'm' || line != 'l' || x0 != 0 || y0 != 0
        || y1 != 0 || x3 != 0 || x1 != x2 || y2 != y3
        || !std::isfinite(x1) || !std::isfinite(y2)
        || x1 <= 0 || y2 <= 0 || x1 > 1000000 || y2 > 1000000)
        return std::nullopt;
    return std::pair{x1, y2};
}
