// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file visual_tool_cross.cpp
/// @brief Crosshair double-click-to-position visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_cross.h"

#include "gl_text.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "video_display.h"

#include <libaegisub/color.h>
#include <libaegisub/format.h>

#include <wx/settings.h>

VisualToolCross::VisualToolCross(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualDraggableFeature>(parent, context)
, gl_text(std::make_unique<OpenGLText>())
{
	parent->SetCursor(wxCursor(wxCURSOR_BLANK));
}

VisualToolCross::~VisualToolCross() {
	parent->SetCursor(wxNullCursor);
}

void VisualToolCross::OnDoubleClick() {
	Vector2D d = ToScriptCoords(mouse_pos) - GetLinePosition(active_line);

	for (auto line : SelectedLines()) {
		Vector2D p1, p2;
		int t1, t2;
		if (GetLineMove(line, p1, p2, t1, t2)) {
			if (t1 > 0 || t2 > 0)
				SetOverride(line, "\\move", agi::format("(%s,%s,%d,%d)", Text(p1 + d), Text(p2 + d), t1, t2));
			else
				SetOverride(line, "\\move", agi::format("(%s,%s)", Text(p1 + d), Text(p2 + d)));
		}
		else
			SetOverride(line, "\\pos", "(" + Text(GetLinePosition(line) + d) + ")");

		if (Vector2D org = GetLineOrigin(line))
			SetOverride(line, "\\org", "(" + Text(org + d) + ")");
	}

	Commit(_("positioning"));
}

void VisualToolCross::Draw() {
	if (!mouse_pos) return;

	// Draw cross
	gl.SetInvert();
	gl.SetLineColour(*wxWHITE, 1.0, 1);
	float lines[] = {
		0.f, mouse_pos.Y(),
		canvas_size.X(), mouse_pos.Y(),
		mouse_pos.X(), 0.f,
		mouse_pos.X(), canvas_size.Y(),
	};
	gl.DrawLines(2, lines, 4);
	gl.ClearInvert();

	std::string mouse_text = Text(ToScriptCoords(shift_down ? 2 * video_pos + video_size - mouse_pos : mouse_pos));

	// The interface's own font, white on a dark label, as the readouts below
	// the video read
	int tw, th;
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.SetPixelSize(wxSize(0, 14));
	font.SetWeight(wxFONTWEIGHT_MEDIUM);
	gl_text->SetFont(font);
	gl_text->SetColour(agi::Color(255, 255, 255, 255));
	gl_text->GetExtent(mouse_text, tw, th);
	const int pad_x = 6, pad_y = 2, gap = 6;
	int w = tw + 2 * pad_x, h = th + 2 * pad_y;

	// Place the label in the corner of the cross closest to the center of the video
	int dx = mouse_pos.X();
	int dy = mouse_pos.Y();
	if (dx > canvas_size.X() / 2)
		dx -= w + gap;
	else
		dx += gap;

	if (dy < canvas_size.Y() / 2)
		dy += gap;
	else
		dy -= h + gap;

	gl.SetLineColour(*wxBLACK, 0.f);
	gl.SetFillColour(*wxBLACK, 0.6f);
	gl.DrawRectangle(Vector2D(dx, dy), Vector2D(dx + w, dy + h));
	gl_text->Print(mouse_text, dx + pad_x, dy + pad_y, false);
}

std::string VisualToolCross::Text(Vector2D v) {
	return video_size.X() > script_res.X() ? v.Str(',', 3) : v.DStr();
}
