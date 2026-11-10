// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include "scroll_effect.h"
#include "visual_feature.h"
#include "visual_tool.h"

#include <map>
#include <memory>

class OpenGLText;

/// A handle of the scroll effect tool
struct ScrollHandle final : public VisualDraggableFeature {
	/// The edge of the area set by y1, by y2, or where the text is when the
	/// line ends, which sets the speed
	enum class Role { Y1, Y2, End } role = Role::End;
	explicit ScrollHandle(Role role) : role(role) { type = role == Role::End ? DRAG_BIG_TRIANGLE : DRAG_SMALL_SQUARE; }
};

/// Shows and edits a line's Banner / Scroll up / Scroll down effect on the
/// video (docs/scroll-effect.md): the area a scroll stays in, its direction,
/// and how far the text has gone now and will have gone when the line ends.
class VisualToolScroll final : public VisualTool<ScrollHandle> {
	std::unique_ptr<OpenGLText> gl_text;
	/// The active line's effect, if it has one of the three
	std::optional<ScrollEffect> effect;
	/// The active line's Effect holds something else, which is left alone
	bool other_content = false;
	/// The Effect field of each selected line when an area is dragged out,
	/// to go back to if the drag ends where it began
	std::map<int, std::string> hold_originals;

	int Duration() const;
	int Elapsed() const;
	/// Where the text's leading edge is @p shift script pixels in: a y for a
	/// scroll, an x for a banner
	double EdgeAt(ScrollEffect const& e, double shift) const;
	/// The banner's height on the video, in script pixels
	double BannerY();
	/// The selected lines and the active line, which is edited even when it
	/// is not among them
	std::vector<AssDialogue*> EditedLines() const;

	/// Apply @p change to the effect of each edited line with a banner, or
	/// a scroll, as the active line has
	template<class Change> void ApplyToSelected(Change change);
	void PlaceHandles();
	void Label(std::string const& text, Vector2D at);

	void DoRefresh() override;
	bool InitializeHold() override;
	void UpdateHold() override;
	bool InitializeDrag(ScrollHandle *handle) override;
	void UpdateDrag(ScrollHandle *handle) override;
	void OnDoubleClick() override;

public:
	VisualToolScroll(VideoDisplay *parent, agi::Context *context);
	~VisualToolScroll() override;
	void Draw() override;
};
