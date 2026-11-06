// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The video zoom below the video (docs/main-window.md): − and + buttons
// either side of the zoom in whole percent; clicking the value opens the
// presets.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <vector>

#include <wx/window.h>

class VideoZoomControl final : public wxWindow {
public:
	enum class Part { None, Out, Value, In };

	explicit VideoZoomControl(wxWindow *parent);

	/// Show @p zoom (1 = 100%)
	void SetZoom(double zoom);
	double GetZoom() const { return zoom; }
	/// Text shown for the zoom: a whole percentage
	wxString ZoomText() const;
	/// Called with the zoom asked for by −, + or a preset
	void SetHandler(std::function<void(double)> handler) { on_zoom = std::move(handler); }

	/// Zoom presets, in whole percent
	static std::vector<int> Presets();
	/// The next step up or down from @p zoom: multiples of 25%
	static double StepIn(double zoom);
	static double StepOut(double zoom);

	/// Area of each part, for tests
	wxRect PartRect(Part part) const;
	/// Act as a click on @p part would, except the preset menu
	void Press(Part part);

private:
	double zoom = 1;
	Part hover = Part::None;
	Part pressed = Part::None;
	std::function<void(double)> on_zoom;
	agi::signal::Connection theme_changed;

	int ButtonWidth() const;
	Part HitTest(wxPoint point) const;
	void Request(double new_zoom);
	void ShowPresets();
	void UpdateToolTip();

	void OnPaint(wxPaintEvent &);
	void OnMouse(wxMouseEvent &event);
};
