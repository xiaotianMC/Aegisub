// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Shapes drawn with \p (a rectangle drawn by the visual tool, say): their
// size, edited in the style panel's Shape section (docs/style-panel.md).
#pragma once

#include <optional>
#include <utility>

namespace agi { struct Context; }

/// Whether the active line is a shape the panel can resize: one drawing,
/// no text, and no \move, \t or \r
bool CanEditDrawing(const agi::Context *c);
/// Show the style panel's Shape section for the active line
void EditDrawing(agi::Context *c);

/// The width and height of the active line's shape, as it is shown
std::optional<std::pair<double, double>> ShapeSize(const agi::Context *c);
/// Resize the active line's shape to @p width by @p height (either left
/// out: as it is). The drawing's points are scaled, so the line needs no
/// \fscx or \fscy for it. @p amend_id is the commit to amend (-1: a
/// new undo step). @return The commit's id, @p amend_id when nothing changed
int ResizeShape(agi::Context *c, std::optional<double> width, std::optional<double> height, int amend_id = -1);
