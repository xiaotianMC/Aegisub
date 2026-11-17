// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include "magia_timeline.h"

#include <functional>
#include <optional>
#include <vector>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/window.h>

namespace agi { struct Context; }

/// A frame of the video with a rectangle drawn on it, dragged into place:
/// a new one from where the mouse is pressed outside it, moved from inside
/// it, resized from its sides and corners. MagiaTimeline gives a preset's
/// rectangles as shares of the content rectangle (the frame without its
/// black bars), and that one as shares of the frame: the rectangle is set
/// and given in the terms of its @p within.
class MagiaRectPicker final : public wxWindow {
	wxImage image;
	wxBitmap scaled;
	wxSize scaled_for;
	/// In shares of the frame
	magia_timeline::Rect within, edited;
	std::vector<magia_timeline::Rect> others;
	bool editing = false;

	enum Grip { none = 0, left = 1, right = 2, top = 4, bottom = 8, move = 16, fresh = 32 };
	int grip = none;
	double press_x = 0, press_y = 0;
	magia_timeline::Rect pressed;

	void OnPaint(wxPaintEvent &);
	void OnMouse(wxMouseEvent &event);
	/// @p p in shares of the frame
	void At(wxPoint p, double &x, double &y) const;
	int GripAt(wxPoint p) const;

public:
	explicit MagiaRectPicker(wxWindow *parent);

	/// Called with the rectangle, in the terms of its within, when a drag ends
	std::function<void(magia_timeline::Rect)> on_change;

	void SetImage(wxImage const& frame);
	/// The rectangle to drag, @p rect given in shares of @p within (both
	/// nothing: none to drag), and @p shown, in shares of the frame, drawn
	/// dim around it
	void SetRects(std::optional<magia_timeline::Rect> within, std::optional<magia_timeline::Rect> rect,
	              std::vector<magia_timeline::Rect> shown);
	/// The rectangle in the terms of its within
	magia_timeline::Rect GetRect() const;
	bool IsEditing() const { return editing; }

	/// Where the frame is drawn
	wxRect ImageArea() const;
	/// The mouse pressed, moved and let go at @p p, as it is by the user
	void Press(wxPoint p);
	void Drag(wxPoint p);
	void Release(wxPoint p);

	wxSize DoGetBestClientSize() const override;
	bool AcceptsFocus() const override { return false; }
};

/// What to run MagiaTimeline with, chosen over what its config.yml says
/// (docs/magiatimeline.md): the strategy and its preset, how frames are
/// sampled and whether the text is read. Starts from @p start; offers only
/// what @p installed has. Nothing when it is cancelled.
std::optional<magia_timeline::Values> ShowMagiaTimelineDialog(agi::Context *c,
	magia_timeline::Installed const& installed, magia_timeline::Values const& start,
	agi::fs::path const& config_file);
