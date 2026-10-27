// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include "visual_tool.h"

class wxKeyEvent;

/// Create a separate, visible ASS drawing without replacing the active subtitle.
class VisualToolRectangle final : public VisualToolBase {
	Vector2D ClampToVideo(Vector2D point) const;
	void Cancel();
	void Finish();
	void OnKeyDown(wxKeyEvent& event);
	void DoRefresh() override { Cancel(); }
	void OnFrameChanged() override { Cancel(); }

public:
	VisualToolRectangle(VideoDisplay *parent, agi::Context *context);
	~VisualToolRectangle() override;
	void OnMouseEvent(wxMouseEvent& event) override;
	void Draw() override;
};
