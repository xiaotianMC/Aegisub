// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Time or frames for the edit box's time fields and the grid's times
// (docs/main-window.md): a drawn two-part switch, like the zoom control
// below the video.
#pragma once

#include <libaegisub/signal.h>

#include <functional>

#include <wx/window.h>

class TimeUnitToggle final : public wxWindow {
	bool by_frame = false;
	bool frames_allowed = false;
	int hover = -1; ///< Part under the mouse: 0 time, 1 frames
	agi::signal::Connection theme_changed;

	wxRect PartRect(int part) const;
	void OnPaint(wxPaintEvent &);
	void OnMouse(wxMouseEvent &event);

public:
	explicit TimeUnitToggle(wxWindow *parent);

	/// Called after the user switches, with whether it is frames now
	std::function<void(bool)> on_change;

	bool ByFrame() const { return by_frame; }
	/// Switch without calling on_change
	void SetByFrame(bool on);
	/// Frames need a video's timecodes: without them only time is offered
	void AllowFrames(bool on);
	bool FramesAllowed() const { return frames_allowed; }
	/// As a click on a part does: 0 time, 1 frames
	void Choose(int part);

	/// Clicks leave the keyboard where it was
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
};
