// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The vertical zoom and volume controls right of the waveform
// (docs/audio-waveform.md): two slim tracks drawn on the waveform's dark
// background, their values below them, and the link and reset buttons.
#pragma once

#include <functional>

#include <wx/window.h>

class AudioLevels final : public wxWindow {
public:
	/// Positions run from 1 to 100, 50 being neutral: the value is
	/// (position / 50)³, as with the sliders this replaces.
	static constexpr int min_position = 1;
	static constexpr int max_position = 100;
	static constexpr int neutral_position = 50;

	enum class Part { None, Zoom, Volume, Link, Reset };

	struct Callbacks {
		std::function<void(int)> zoom;    ///< The user moved the zoom
		std::function<void(int)> volume;  ///< The user moved the volume
		std::function<void()> link;       ///< The link button was clicked
		std::function<void()> reset;      ///< The reset button was clicked
	};

	AudioLevels(wxWindow *parent, Callbacks callbacks);

	void SetZoom(int position);
	void SetVolume(int position);
	/// Linked: one track sets both, drawn as one
	void SetLinked(bool linked);
	int GetZoom() const { return zoom; }
	int GetVolume() const { return volume; }
	bool IsLinked() const { return linked; }

	/// What is at @p point, for the mouse and tests
	Part HitTest(wxPoint point) const;
	/// The handle's centre on a track for @p position
	wxPoint HandleAt(Part track, int position) const;

	/// Texts shown below the tracks
	static wxString ZoomText(int position);
	static wxString VolumeText(int position);

private:
	Callbacks callbacks;
	int zoom = neutral_position;
	int volume = neutral_position;
	bool linked = false;

	Part hover = Part::None;
	Part dragging = Part::None;
	int drag_last_y = 0;
	/// Unrounded position while dragging, so slow (Shift) drags add up
	double drag_value = 0;

	wxRect TrackColumn(Part track) const;
	wxRect TrackRect(Part track) const;
	wxRect ButtonRect(Part button) const;
	int LabelHeight() const;
	int PositionAt(Part track, int y) const;
	void Move(Part track, int position);

	void OnPaint(wxPaintEvent &);
	void OnMouse(wxMouseEvent &event);
	void OnWheel(wxMouseEvent &event);
	void UpdateToolTip();
};
