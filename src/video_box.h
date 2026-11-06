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
#include <memory>

#include <libaegisub/signal.h>

#include <vector>
#include <wx/panel.h>

namespace agi { struct Context; }
class wxSizerItem;
class VideoSlider;
class VideoTimeLabel;

/// @class VideoBox
/// @brief The box containing the video display and associated controls
class VideoBox final : public wxPanel {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
	std::vector<agi::signal::Connection> connections;
	agi::Context *context;     ///< Project context
	VideoSlider *slider;           ///< Seek bar, showing the active line's span
	VideoTimeLabel *VideoPosition; ///< Current frame/time
	VideoTimeLabel *VideoSubsPos;  ///< Time relative to the active subtitle line

	/// Update VideoPosition, VideoSubsPos and the line's span on the slider
	void UpdateTimeBoxes();

	wxSizerItem *toolbar_item = nullptr;

public:
	VideoBox(wxWindow *parent, bool isDetached, agi::Context *context);

	/// Put @p gap pixels between the video and the visual tools to their right
	void SetToolbarGap(int gap);
};
