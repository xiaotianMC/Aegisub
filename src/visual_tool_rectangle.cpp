// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "visual_tool_rectangle.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "video_display.h"

#include <libaegisub/format.h>

#include <wx/colour.h>
#include <wx/dcmemory.h>
#include <wx/event.h>

wxString DrawingShapeLabel(DrawingShape shape) {
	switch (shape) {
		case DrawingShape::Rectangle: return _("Draw Rectangle");
		case DrawingShape::Ellipse: return _("Draw Ellipse / Circle");
		case DrawingShape::Triangle: return _("Draw Triangle");
		case DrawingShape::Polygon: return _("Draw Polygon");
	}
	return {};
}

wxBitmapBundle DrawingShapeIcon(DrawingShape shape, int size) {
	wxBitmap bitmap(size, size, 32);
	wxMemoryDC dc(bitmap);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();
	dc.SetPen(wxPen(wxColour(30, 110, 190)));
	dc.SetBrush(wxBrush(wxColour(45, 135, 220)));
	int lo = size / 5, hi = size * 4 / 5, mid = size / 2;
	if (shape == DrawingShape::Rectangle) dc.DrawRectangle(lo, lo, hi - lo, hi - lo);
	else if (shape == DrawingShape::Ellipse) dc.DrawEllipse(lo, lo, hi - lo, hi - lo);
	else if (shape == DrawingShape::Triangle) {
		wxPoint points[] = {{mid, lo}, {hi, hi}, {lo, hi}};
		dc.DrawPolygon(3, points);
	}
	else {
		wxPoint points[] = {{lo, lo}, {hi, lo}, {mid, mid}, {hi, hi}, {lo, hi}};
		dc.DrawPolygon(5, points);
	}
	// A small corner marker advertises the shape flyout.
	dc.SetPen(*wxBLACK_PEN);
	dc.SetBrush(*wxBLACK_BRUSH);
	wxPoint arrow[] = {{size - 5, size - 2}, {size - 2, size - 5}, {size - 2, size - 2}};
	dc.DrawPolygon(3, arrow);
	dc.SelectObject(wxNullBitmap);
	bitmap.SetMask(new wxMask(bitmap, *wxWHITE));
	return wxBitmapBundle::FromBitmap(bitmap);
}

VisualToolRectangle::VisualToolRectangle(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context) {
	parent->Bind(wxEVT_CHAR_HOOK, &VisualToolRectangle::OnKeyDown, this);
	parent->Bind(wxEVT_KEY_UP, &VisualToolRectangle::OnKeyUp, this);
	parent->Bind(wxEVT_RIGHT_DOWN, &VisualToolRectangle::OnMouseEvent, this);
}

VisualToolRectangle::~VisualToolRectangle() {
	Cancel();
	parent->Unbind(wxEVT_CHAR_HOOK, &VisualToolRectangle::OnKeyDown, this);
	parent->Unbind(wxEVT_KEY_UP, &VisualToolRectangle::OnKeyUp, this);
	parent->Unbind(wxEVT_RIGHT_DOWN, &VisualToolRectangle::OnMouseEvent, this);
}

void VisualToolRectangle::SetSubTool(int subtool) {
	if (subtool < 0 || subtool > static_cast<int>(DrawingShape::Polygon)) return;
	Cancel();
	shape = static_cast<DrawingShape>(subtool);
	parent->Render();
}

Vector2D VisualToolRectangle::ClampToVideo(Vector2D point) const {
	return video_pos.Max((video_pos + video_size).Min(point));
}

void VisualToolRectangle::Cancel() {
	holding = false;
	vertices.clear();
	if (parent->HasCapture()) parent->ReleaseMouse();
}

Vector2D VisualToolRectangle::DrawingEnd() const {
	if (!shift_down) return mouse_pos;
	Vector2D start = ToScriptCoords(vertices.empty() ? drag_start : vertices.back());
	Vector2D end = ToScriptCoords(mouse_pos);
	auto point = ConstrainDrawingPoint({start.X(), start.Y()}, {end.X(), end.Y()},
		{script_res.X(), script_res.Y()}, shape);
	return FromScriptCoords(Vector2D(point.x, point.y));
}

void VisualToolRectangle::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && holding) {
		Cancel();
		parent->Render();
	}
	else if ((event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER)
		&& holding && shape == DrawingShape::Polygon) {
		Finish();
		Cancel();
		parent->Render();
	}
	else if (event.GetKeyCode() == WXK_BACK && holding && shape == DrawingShape::Polygon) {
		if (!vertices.empty()) vertices.pop_back();
		if (vertices.empty()) Cancel();
		parent->Render();
	}
	else if (event.GetKeyCode() == WXK_SHIFT) {
		shift_down = true;
		parent->Render();
		event.Skip();
	}
	else event.Skip();
}

void VisualToolRectangle::OnKeyUp(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_SHIFT) {
		shift_down = false;
		parent->Render();
	}
	event.Skip();
}

void VisualToolRectangle::OnMouseEvent(wxMouseEvent& event) {
	mouse_pos = ClampToVideo(event.GetPosition());
	shift_down = event.ShiftDown();
	if (shape == DrawingShape::Polygon) {
		if (!active_line) return;
		if (event.LeftDown() || event.LeftDClick()) {
			if (event.GetPosition() != wxPoint(mouse_pos.X(), mouse_pos.Y())) return;
			parent->SetFocus();
			if (vertices.size() >= 3 && (mouse_pos - vertices.front()).Len() <= 6) {
				Finish();
				Cancel();
			}
			else {
				auto point = vertices.empty() ? mouse_pos : DrawingEnd();
				if (vertices.empty() || (point - vertices.back()).Len() >= 1)
					vertices.push_back(point);
				holding = true;
				if (event.LeftDClick() && vertices.size() >= 3) {
					Finish();
					Cancel();
				}
			}
		}
		else if (event.RightDown()) Cancel();
		parent->Render();
		return;
	}
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
		Finish();
		Cancel();
	}
	else if (holding && !event.LeftIsDown()) {
		Cancel();
	}
	parent->Render();
}

void VisualToolRectangle::Finish() {
	if (!active_line) return;
	Vector2D end = DrawingEnd();
	Vector2D first;
	std::string path;
	if (shape == DrawingShape::Polygon) {
		if (vertices.size() < 3) return;
		first = ToScriptCoords(vertices.front()).Round(1);
		for (auto vertex : vertices) first = first.Min(ToScriptCoords(vertex).Round(1));
		std::vector<DrawingPoint> points;
		for (auto vertex : vertices) {
			auto point = ToScriptCoords(vertex).Round(1) - first;
			points.push_back({point.X(), point.Y()});
		}
		path = PolygonDrawingPath(points);
	}
	else {
		first = ToScriptCoords(drag_start.Min(end)).Round(1);
		Vector2D last = ToScriptCoords(drag_start.Max(end)).Round(1);
		path = ShapeDrawingPath(shape, last.X() - first.X(), last.Y() - first.Y());
	}
	if (path.empty()) return;

	bool added_style = false;
	if (!c->ass->GetStyle("图形")) {
		auto style = std::make_unique<AssStyle>();
		style->name = "图形";
		style->alignment = 7;
		style->outline_w = 0;
		style->shadow_w = 0;
		style->primary = agi::Color(0, 0, 0, 64);
		style->Margin = {0, 0, 0};
		style->UpdateData();
		c->ass->Styles.push_back(*style.release());
		added_style = true;
	}
	auto drawing = std::make_unique<AssDialogue>();
	drawing->Start = active_line->Start;
	drawing->End = active_line->End;
	drawing->Style = "图形";
	drawing->Layer = active_line->Layer;
	drawing->Text = agi::format(
		"{\\an7\\pos(%s)\\fscx100\\fscy100\\frz0\\bord0\\shad0"
		"\\1c&H000000&\\1a&H40&\\3a&HFF&\\4a&HFF&\\p1}"
		"%s{\\p0}", first.DStr(), path);

	// Same layer and earlier read order put the shape behind the source subtitle.
	auto added = drawing.get();
	c->ass->Events.insert(c->ass->iterator_to(*active_line), *added);
	drawing.release();
	c->ass->Commit(_("draw shape"), AssFile::COMMIT_DIAG_ADDREM | (added_style ? AssFile::COMMIT_STYLES : 0));
	c->selectionController->SetSelectionAndActive({added}, added);
}

void VisualToolRectangle::Draw() {
	if (!holding) return;
	gl.SetLineColour(to_wx(line_color_primary_opt->GetColor()), 1.0f, 2);
	gl.SetFillColour(*wxBLACK, 0.75f);
	Vector2D end = DrawingEnd();
	Vector2D first = drag_start.Min(end), last = drag_start.Max(end);
	if (shape == DrawingShape::Rectangle) gl.DrawRectangle(first, last);
	else if (shape == DrawingShape::Ellipse) gl.DrawEllipse((first + last) / 2, (last - first) / 2);
	else if (shape == DrawingShape::Triangle)
		gl.DrawTriangle(Vector2D((first.X() + last.X()) / 2, first.Y()), last, Vector2D(first.X(), last.Y()));
	else if (!vertices.empty()) {
		std::vector<float> points;
		for (auto vertex : vertices) {
			points.push_back(vertex.X());
			points.push_back(vertex.Y());
		}
		points.push_back(end.X());
		points.push_back(end.Y());
		std::vector<int> starts{0}, counts{static_cast<int>(vertices.size() + 1)};
		if (vertices.size() >= 2)
			gl.DrawMultiPolygon(points, starts, counts, video_pos, video_size, false);
		else gl.DrawLine(vertices.front(), end);
		for (auto vertex : vertices) gl.DrawCircle(vertex, 3);
		gl.DrawCircle(vertices.front(), 6);
	}
}
