// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "visual_tool_rectangle.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "video_display.h"

#include <libaegisub/format.h>

#include <wx/colour.h>
#include <wx/event.h>

VisualToolRectangle::VisualToolRectangle(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context) {
	parent->Bind(wxEVT_KEY_DOWN, &VisualToolRectangle::OnKeyDown, this);
}

VisualToolRectangle::~VisualToolRectangle() {
	Cancel();
	parent->Unbind(wxEVT_KEY_DOWN, &VisualToolRectangle::OnKeyDown, this);
}

Vector2D VisualToolRectangle::ClampToVideo(Vector2D point) const {
	return video_pos.Max((video_pos + video_size).Min(point));
}

void VisualToolRectangle::Cancel() {
	holding = false;
	if (parent->HasCapture()) parent->ReleaseMouse();
}

void VisualToolRectangle::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && holding) {
		Cancel();
		parent->Render();
	}
	else event.Skip();
}

void VisualToolRectangle::OnMouseEvent(wxMouseEvent& event) {
	mouse_pos = ClampToVideo(event.GetPosition());
	if (event.LeftDown()) {
		if (!active_line || event.GetPosition() != wxPoint(mouse_pos.X(), mouse_pos.Y())) return;
		parent->SetFocus();
		drag_start = mouse_pos;
		holding = true;
		parent->CaptureMouse();
	}
	else if (holding && event.RightDown()) {
		Cancel();
	}
	else if (holding && event.LeftUp()) {
		Cancel();
		Finish();
	}
	else if (holding && !event.LeftIsDown()) {
		Cancel();
	}
	parent->Render();
}

void VisualToolRectangle::Finish() {
	if (!active_line) return;
	Vector2D first = ToScriptCoords(drag_start.Min(mouse_pos)).Round(1);
	Vector2D last = ToScriptCoords(drag_start.Max(mouse_pos)).Round(1);
	int width = last.X() - first.X();
	int height = last.Y() - first.Y();
	if (width < 1 || height < 1) return;

	auto rectangle = std::make_unique<AssDialogue>();
	rectangle->Start = active_line->Start;
	rectangle->End = active_line->End;
	rectangle->Style = active_line->Style;
	rectangle->Layer = active_line->Layer;
	rectangle->Text = agi::format(
		"{\\an7\\pos(%s)\\fscx100\\fscy100\\frz0\\bord0\\shad0"
		"\\1c&H000000&\\1a&H40&\\3a&HFF&\\4a&HFF&\\p1}"
		"m 0 0 l %d 0 %d %d 0 %d{\\p0}", first.DStr(), width, width, height, height);

	// Same layer and earlier read order put the box behind the source subtitle.
	auto added = rectangle.get();
	c->ass->Events.insert(c->ass->iterator_to(*active_line), *added);
	rectangle.release();
	c->ass->Commit(_("draw rectangle"), AssFile::COMMIT_DIAG_ADDREM);
	c->selectionController->SetSelectionAndActive({added}, added);
}

void VisualToolRectangle::Draw() {
	if (!holding) return;
	gl.SetLineColour(to_wx(line_color_primary_opt->GetColor()), 1.0f, 2);
	gl.SetFillColour(*wxBLACK, 0.75f);
	gl.DrawRectangle(drag_start.Min(mouse_pos), drag_start.Max(mouse_pos));
}
