// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include "visual_tool.h"
#include "shape_drawing.h"

#include <wx/bmpbndl.h>

class wxKeyEvent;

wxString DrawingShapeLabel(DrawingShape shape);
wxBitmapBundle DrawingShapeIcon(DrawingShape shape, int size);

/// Create a separate, visible ASS drawing without replacing the active subtitle.
class VisualToolRectangle final : public VisualToolBase {
	DrawingShape shape = DrawingShape::Rectangle;
	std::vector<Vector2D> vertices;
	Vector2D ClampToVideo(Vector2D point) const;
	Vector2D DrawingEnd() const;
	void Cancel();
	void Finish();
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);
	void DoRefresh() override { Cancel(); }
	void OnFrameChanged() override { Cancel(); }

public:
	VisualToolRectangle(VideoDisplay *parent, agi::Context *context);
	~VisualToolRectangle() override;
	void OnMouseEvent(wxMouseEvent& event) override;
	void Draw() override;
	void SetSubTool(int subtool) override;
	int GetSubTool() override { return static_cast<int>(shape); }
};
