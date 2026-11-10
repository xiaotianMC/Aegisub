// Copyright (c) 2005, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#pragma once

#include <libaegisub/signal.h>

#include <vector>
#include <wx/window.h>

namespace agi { struct Context; }

class VideoController;
class AsyncVideoProvider;

/// @class VideoSlider
/// @brief Slider for displaying and adjusting the video position
///
/// A flat track (docs/main-window.md): played part in the accent colour, a
/// round handle, keyframes as ticks above, and the active line's time as a
/// tinted span behind the track.
class VideoSlider: public wxWindow {
	agi::Context *c; ///< Associated project context
	std::vector<int> keyframes; ///< Currently loaded keyframes
	std::vector<agi::signal::Connection> connections;

	int val = 0; ///< Current frame number
	int max = 1; ///< Last frame number
	int line_start = -1; ///< First frame of the active line, -1 for none
	int line_end = -1;   ///< Last frame of the active line
	bool hover = false;  ///< The mouse is over the slider
	int hover_frame = -1; ///< Frame under the mouse, for the tool tip

	/// Space at each end of the track, so the handle fits at both ends
	int Margin() const;
	/// Get the frame number for the given x coordinate
	int GetValueAtX(int x) const;
	/// Get the x-coordinate for a frame number
	int GetXAtValue(int value) const;
	/// Set the position of the slider
	void SetValue(int value);
	/// Tool tip naming the time and frame under the mouse
	void UpdateToolTip(int x);

	/// Video open event handler
	void VideoOpened(AsyncVideoProvider *new_provider);
	/// Keyframe open even handler
	void KeyframesChanged(std::vector<int> const& newKeyframes);

	void OnMouse(wxMouseEvent &event);
	void OnKeyDown(wxKeyEvent &event);
	void OnCharHook(wxKeyEvent &event);
	void OnPaint(wxPaintEvent &);
	void OnFocus(wxFocusEvent &);

public:
	VideoSlider(wxWindow* parent, agi::Context *c);

	/// Mark the active line's frames, @p start to @p end inclusive; -1 for no line
	void SetLineRange(int start, int end);
	int GetLineStart() const { return line_start; }
	int GetLineEnd() const { return line_end; }
	/// x-coordinate of @p frame on the track, for tests
	int XAtFrame(int frame) const { return GetXAtValue(frame); }

	DECLARE_EVENT_TABLE()
};
