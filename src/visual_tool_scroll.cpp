// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "visual_tool_scroll.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "format.h"
#include "gl_text.h"
#include "include/aegisub/context.h"
#include "video_controller.h"
#include "video_display.h"

#include <algorithm>
#include <cmath>

#include <wx/settings.h>

VisualToolScroll::VisualToolScroll(VideoDisplay *parent, agi::Context *context)
: VisualTool<ScrollHandle>(parent, context)
, gl_text(std::make_unique<OpenGLText>())
{
	DoRefresh();
}

VisualToolScroll::~VisualToolScroll() = default;

int VisualToolScroll::Duration() const {
	return active_line ? std::max<int>(active_line->End - active_line->Start, 0) : 0;
}

int VisualToolScroll::Elapsed() const {
	return active_line ? c->videoController->TimeAtFrame(frame_number) - active_line->Start : 0;
}

double VisualToolScroll::EdgeAt(ScrollEffect const& e, double shift) const {
	switch (e.kind) {
		// The text's top comes up from the bottom of the area
		case ScrollEffect::Kind::ScrollUp: return e.Bottom() - shift;
		// The text's bottom comes down from the top of the area
		case ScrollEffect::Kind::ScrollDown: return e.Top() + shift;
		// The text's left comes in from the right, or its right from the left
		case ScrollEffect::Kind::Banner: return e.left_to_right ? shift : script_res.X() - shift;
	}
	return 0;
}

double VisualToolScroll::BannerY() {
	return active_line ? GetLinePosition(active_line).Y() : script_res.Y() / 2;
}

std::vector<AssDialogue*> VisualToolScroll::EditedLines() const {
	auto lines = SelectedLines();
	if (active_line && std::find(lines.begin(), lines.end(), active_line) == lines.end())
		lines.push_back(active_line);
	return lines;
}

template<class Change>
void VisualToolScroll::ApplyToSelected(Change change) {
	if (!effect) return;
	for (auto line : EditedLines()) {
		auto e = ParseScrollEffect(line->Effect.get());
		if (!e || e->IsScroll() != effect->IsScroll()) continue;
		change(*e);
		SetEffect(line, FormatScrollEffect(*e));
	}
	effect = ParseScrollEffect(active_line->Effect.get());
}

void VisualToolScroll::DoRefresh() {
	sel_features.clear();
	active_feature = nullptr;
	features.clear();
	effect.reset();
	other_content = false;
	if (!active_line) return;
	effect = ParseScrollEffect(active_line->Effect.get());
	other_content = !effect && !active_line->Effect.get().empty();
	if (!effect) return;
	if (effect->IsScroll()) {
		features.push_back(*new ScrollHandle(ScrollHandle::Role::Y1));
		features.push_back(*new ScrollHandle(ScrollHandle::Role::Y2));
	}
	features.push_back(*new ScrollHandle(ScrollHandle::Role::End));
	PlaceHandles();
}

void VisualToolScroll::PlaceHandles() {
	if (!effect) return;
	double end = EdgeAt(*effect, ScrollShift(*effect, Duration()));
	for (auto& handle : features) {
		switch (handle.role) {
			// The edges in the middle, the end a little to the right, so
			// they can be told apart where they meet
			case ScrollHandle::Role::Y1: handle.pos = FromScriptCoords(Vector2D(script_res.X() / 2, effect->y1)); break;
			case ScrollHandle::Role::Y2: handle.pos = FromScriptCoords(Vector2D(script_res.X() / 2, effect->y2)); break;
			case ScrollHandle::Role::End:
				handle.pos = effect->IsScroll()
					? FromScriptCoords(Vector2D(script_res.X() * 3 / 4, end))
					: FromScriptCoords(Vector2D(end, BannerY()));
				break;
		}
	}
}

bool VisualToolScroll::InitializeHold() {
	// An area is dragged out for a line with no effect or a scroll; a banner
	// has none, and anything else in the field is not touched
	if (other_content || (effect && !effect->IsScroll())) return false;
	hold_originals.clear();
	for (auto line : EditedLines()) hold_originals[line->Id] = line->Effect.get();
	return true;
}

void VisualToolScroll::UpdateHold() {
	auto clamp_y = [&](Vector2D p) { return static_cast<int>(std::lround(std::clamp<double>(ToScriptCoords(p).Y(), 0, script_res.Y()))); };
	int y1 = clamp_y(drag_start), y2 = clamp_y(mouse_pos);
	bool had_effect = effect.has_value();
	for (auto line : EditedLines()) {
		auto original = hold_originals.find(line->Id);
		if (original == hold_originals.end()) continue;
		// Back where it began: nothing is made
		if (y1 == y2) { SetEffect(line, original->second); continue; }
		auto e = ParseScrollEffect(original->second);
		if (!e && !original->second.empty()) continue;
		if (e && !e->IsScroll()) continue;
		if (!e) {
			e = ScrollEffect{};
			// Through the area by the line's end
			e->delay = DelayForShift(std::max<int>(line->End - line->Start, 0), std::abs(y2 - y1));
		}
		e->y1 = std::min(y1, y2);
		e->y2 = std::max(y1, y2);
		SetEffect(line, FormatScrollEffect(*e));
	}
	effect = ParseScrollEffect(active_line->Effect.get());
	if (effect.has_value() != had_effect || (effect && features.empty())) {
		// Made or unmade: the handles come and go with it
		sel_features.clear();
		active_feature = nullptr;
		features.clear();
		if (effect) {
			features.push_back(*new ScrollHandle(ScrollHandle::Role::Y1));
			features.push_back(*new ScrollHandle(ScrollHandle::Role::Y2));
			features.push_back(*new ScrollHandle(ScrollHandle::Role::End));
		}
	}
	PlaceHandles();
	Commit(_("scroll effect"));
}

bool VisualToolScroll::InitializeDrag(ScrollHandle *) {
	return effect.has_value();
}

void VisualToolScroll::UpdateDrag(ScrollHandle *handle) {
	Vector2D at = ToScriptCoords(handle->pos);
	switch (handle->role) {
		case ScrollHandle::Role::Y1:
		case ScrollHandle::Role::Y2: {
			int y = static_cast<int>(std::lround(std::clamp<double>(at.Y(), 0, script_res.Y())));
			bool first = handle->role == ScrollHandle::Role::Y1;
			ApplyToSelected([&](ScrollEffect &e) { (first ? e.y1 : e.y2) = y; });
			break;
		}
		case ScrollHandle::Role::End: {
			// How far the text goes by the line's end, from where it starts,
			// the way it goes; no less than a pixel
			double start = EdgeAt(*effect, 0);
			double shift = 0;
			switch (effect->kind) {
				case ScrollEffect::Kind::ScrollUp: shift = start - at.Y(); break;
				case ScrollEffect::Kind::ScrollDown: shift = at.Y() - start; break;
				case ScrollEffect::Kind::Banner: shift = effect->left_to_right ? at.X() - start : start - at.X(); break;
			}
			int delay = DelayForShift(Duration(), std::max(shift, 1.));
			ApplyToSelected([&](ScrollEffect &e) { e.delay = delay; });
			break;
		}
	}
	// The handles show what was set: the edge where the rounded delay puts it
	PlaceHandles();
	Commit(_("scroll effect"));
}

void VisualToolScroll::OnDoubleClick() {
	if (!effect) return;
	ApplyToSelected([](ScrollEffect &e) {
		if (e.kind == ScrollEffect::Kind::Banner) e.left_to_right = !e.left_to_right;
		else e.kind = e.kind == ScrollEffect::Kind::ScrollUp ? ScrollEffect::Kind::ScrollDown : ScrollEffect::Kind::ScrollUp;
	});
	PlaceHandles();
	Commit(_("scroll effect"));
}

void VisualToolScroll::Label(std::string const& text, Vector2D at) {
	// The interface's font, white on a dark label, as the cross tool has it
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.SetPixelSize(wxSize(0, 14));
	font.SetWeight(wxFONTWEIGHT_MEDIUM);
	gl_text->SetFont(font);
	gl_text->SetColour(agi::Color(255, 255, 255, 255));
	int tw, th;
	gl_text->GetExtent(text, tw, th);
	const int pad_x = 6, pad_y = 2;
	// Kept on the video
	double x = std::clamp<double>(at.X(), video_pos.X(), std::max<double>(video_pos.X(), video_pos.X() + video_size.X() - tw - 2 * pad_x));
	double y = std::clamp<double>(at.Y(), video_pos.Y(), std::max<double>(video_pos.Y(), video_pos.Y() + video_size.Y() - th - 2 * pad_y));
	gl.SetLineColour(*wxBLACK, 0.f);
	gl.SetFillColour(*wxBLACK, 0.6f);
	gl.DrawRectangle(Vector2D(x, y), Vector2D(x + tw + 2 * pad_x, y + th + 2 * pad_y));
	gl_text->Print(text, x + pad_x, y + pad_y, false);
}

void VisualToolScroll::Draw() {
	if (!active_line) return;
	Vector2D hint_at = video_pos + Vector2D(8, 8);
	if (other_content) {
		Label(from_wx(_("This line's Effect holds something else, which is left as it is")), hint_at);
		return;
	}
	if (!effect) {
		Label(from_wx(_("Drag up or down to make the line scroll in that area")), hint_at);
		return;
	}

	wxColour line_colour = to_wx(line_color_primary_opt->GetColor());
	wxColour now_colour = to_wx(line_color_secondary_opt->GetColor());
	float shaded_alpha = static_cast<float>(shaded_area_alpha_opt->GetDouble());
	Vector2D v_min = video_pos, v_max = video_pos + video_size;
	double now = EdgeAt(*effect, ScrollShift(*effect, Elapsed()));
	double end = EdgeAt(*effect, ScrollShift(*effect, Duration()));

	if (effect->IsScroll()) {
		double top = FromScriptCoords(Vector2D(0, effect->Top())).Y();
		double bottom = FromScriptCoords(Vector2D(0, effect->Bottom())).Y();
		// Outside the area the text is not shown
		gl.SetLineColour(line_colour, 0.f);
		gl.SetFillColour(*wxBLACK, shaded_alpha);
		gl.DrawRectangle(v_min, Vector2D(v_max.X(), top));
		gl.DrawRectangle(Vector2D(v_min.X(), bottom), v_max);
		gl.SetLineColour(line_colour, 1.f, 2);
		gl.DrawLine(Vector2D(v_min.X(), top), Vector2D(v_max.X(), top));
		gl.DrawLine(Vector2D(v_min.X(), bottom), Vector2D(v_max.X(), bottom));

		// Which way it goes: an arrow up the middle of the area
		bool up = effect->kind == ScrollEffect::Kind::ScrollUp;
		double x = v_min.X() + video_size.X() / 4, tip = up ? top + 6 : bottom - 6, tail = up ? bottom - 6 : top + 6;
		double head = up ? 10 : -10;
		gl.DrawLine(Vector2D(x, tail), Vector2D(x, tip));
		gl.DrawLine(Vector2D(x, tip), Vector2D(x - 7, tip + head));
		gl.DrawLine(Vector2D(x, tip), Vector2D(x + 7, tip + head));

		// Where the text's leading edge is now, and will be when the line ends
		double now_y = FromScriptCoords(Vector2D(0, now)).Y(), end_y = FromScriptCoords(Vector2D(0, end)).Y();
		gl.SetLineColour(now_colour, 1.f, 1);
		if (now_y >= v_min.Y() && now_y <= v_max.Y()) gl.DrawLine(Vector2D(v_min.X(), now_y), Vector2D(v_max.X(), now_y));
		gl.SetLineColour(line_colour, 1.f, 1);
		if (end_y >= v_min.Y() && end_y <= v_max.Y()) gl.DrawDashedLine(Vector2D(v_min.X(), end_y), Vector2D(v_max.X(), end_y), 8);
	}
	else {
		double y = FromScriptCoords(Vector2D(0, BannerY())).Y();
		bool rtl = !effect->left_to_right;
		double from = rtl ? v_max.X() - 6 : v_min.X() + 6, to = rtl ? v_min.X() + 6 : v_max.X() - 6;
		double head = rtl ? 10 : -10;
		gl.SetLineColour(line_colour, 1.f, 2);
		gl.DrawLine(Vector2D(from, y), Vector2D(to, y));
		gl.DrawLine(Vector2D(to, y), Vector2D(to + head, y - 7));
		gl.DrawLine(Vector2D(to, y), Vector2D(to + head, y + 7));

		double now_x = FromScriptCoords(Vector2D(now, 0)).X(), end_x = FromScriptCoords(Vector2D(end, 0)).X();
		gl.SetLineColour(now_colour, 1.f, 1);
		if (now_x >= v_min.X() && now_x <= v_max.X()) gl.DrawLine(Vector2D(now_x, v_min.Y()), Vector2D(now_x, v_max.Y()));
		gl.SetLineColour(line_colour, 1.f, 1);
		if (end_x >= v_min.X() && end_x <= v_max.X()) gl.DrawDashedLine(Vector2D(end_x, v_min.Y()), Vector2D(end_x, v_max.Y()), 8);
	}

	DrawAllFeatures();

	// The speed, and how far that takes the text by the line's end
	double speed = 1000. / EffectiveDelay(effect->delay);
	int travel = static_cast<int>(std::lround(ScrollShift(*effect, Duration())));
	wxString speed_text = speed >= 10 ? wxString::Format("%.0f", speed) : wxString::Format("%.1f", speed);
	Label(from_wx(fmt_tl("%s px/s, %d px by the line's end", speed_text, travel)), hint_at);
}
