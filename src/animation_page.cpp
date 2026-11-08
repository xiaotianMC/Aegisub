// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "animation_page.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "dialogs.h"
#include "format.h"
#include "project.h"
#include "selection_controller.h"
#include "style_look.h"
#include "style_panel_parts.h"
#include "subtitle_project_access.h"
#include "subtitle_row_edit.h"
#include "video_controller.h"
#include "command/command.h"
#include "include/aegisub/context.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/format.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <wx/menu.h>
#include <wx/sizer.h>

using namespace animation;
using Field = StylePanel::Field;
using IconButton = StylePanel::IconButton;
using Caption = StylePanel::Caption;

namespace {
/// The number a target's value holds
double ValueNumber(std::string const& value) {
	return std::strtod(value.c_str(), nullptr);
}

/// Opacity in percent for an alpha written as &HAA&
double OpacityOf(std::string const& value) {
	std::string digits;
	for (char ch : value)
		if (std::isxdigit(static_cast<unsigned char>(ch)) && !(digits.empty() && (ch == 'H' || ch == 'h'))) digits += ch;
	// "&H80&": the H is not a digit, the rest is
	if (digits.size() > 2) digits = digits.substr(digits.size() - 2);
	int alpha = digits.empty() ? 0 : static_cast<int>(std::strtol(digits.c_str(), nullptr, 16));
	return std::round((255 - std::clamp(alpha, 0, 255)) * 100 / 255.);
}
int AlphaOfOpacity(double opacity) {
	return static_cast<int>(std::lround((100 - std::clamp(opacity, 0., 100.)) * 255 / 100));
}
std::string AlphaValue(int alpha) { return agi::format("&H%02X&", std::clamp(alpha, 0, 255)); }

wxString TimeText(double ms) {
	return to_wx(agi::Time(static_cast<int>(std::lround(std::max(0., ms)))).GetAssFormatted());
}
/// A typed time: h:mm:ss.cc, or fewer parts (s, m:ss)
bool ReadTime(wxString const& text, double &ms) {
	if (!text.IsNumber() && text.find_first_of("0123456789") == wxString::npos) return false;
	for (auto ch : text)
		if (!wxIsdigit(ch) && ch != ':' && ch != '.' && ch != ',' && ch != ' ') return false;
	wxString normal = text;
	normal.Replace(",", ".");
	ms = agi::Time(from_wx(normal));
	return true;
}

/// Which tags a card writes, from its name
enum class Kind { Move, Fade, Transforms, Origin };
Kind KindOf(std::string const& card) {
	if (card == "move") return Kind::Move;
	if (card == "fade") return Kind::Fade;
	if (card == "origin") return Kind::Origin;
	return Kind::Transforms;
}
/// The transform a card "t<n>" is for
size_t TransformIndex(std::string const& card) {
	return card.size() > 1 && card[0] == 't' ? std::strtoul(card.c_str() + 1, nullptr, 10) : 0;
}

/// @p text with the tags of @p kind written from @p a
std::string WriteKind(std::string const& text, Animation const& a, Kind kind, int duration) {
	switch (kind) {
		case Kind::Move:
			if (a.move) return WriteMove(text, a.move, duration);
			if (a.position) return WritePosition(text, a.position->first, a.position->second);
			return WriteMove(text, std::nullopt, duration);
		case Kind::Fade: return WriteFade(text, a.fade, duration);
		case Kind::Origin: return WriteOrigin(text, a.origin);
		case Kind::Transforms: return WriteTransforms(text, a.transforms, duration);
	}
	return text;
}
}

/// The bar over the line, 0% to 100% of it: a range's two ends (\move,
/// \t), or the four times of a fade with its opacity drawn over them.
/// Each can be dragged, snapping to the video's frames unless Shift is
/// down; a click elsewhere goes there in the video. The video's time shows
/// as a line across it.
class AnimationPage::Timeline final : public StylePanel::Part {
	int duration = 1;
	std::vector<int> points; ///< Milliseconds from the line's start, in order
	std::vector<int> alphas; ///< For a fade: a1, a2, a3
	std::optional<int> playhead;
	int dragging = -1;
	bool moved = false;

	wxRect Track() const {
		wxSize size = GetClientSize();
		int side = FromDIP(7);
		return wxRect(side, FromDIP(4), std::max(1, size.x - 2 * side), std::max(1, size.y - FromDIP(18)));
	}
	int X(int ms) const {
		wxRect t = Track();
		return t.x + static_cast<int>(std::lround(static_cast<double>(std::clamp(ms, 0, duration)) * t.width / std::max(1, duration)));
	}
	int Ms(int x) const {
		wxRect t = Track();
		return std::clamp(static_cast<int>(std::lround(static_cast<double>(x - t.x) * duration / std::max(1, t.width))), 0, duration);
	}
	int AlphaY(int alpha) const {
		wxRect t = Track();
		return t.y + static_cast<int>(std::lround(alpha * (t.height - 1) / 255.));
	}
	/// The alpha a fade has at point @p i
	int PointAlpha(size_t i) const {
		if (alphas.size() < 3) return 0;
		return i == 0 ? alphas[0] : i == 3 ? alphas[2] : alphas[1];
	}

	void Draw(wxDC &dc, Palette const& p) override {
		wxRect t = Track();
		int thick = std::max(1, FromDIP(1));
		dc.SetBrush(wxBrush(p.field));
		dc.SetPen(wxPen(p.line, thick));
		dc.DrawRoundedRectangle(t, FromDIP(4));

		// Quarters of the line, and their percentages below
		wxFont small = GetFont();
		small.SetFractionalPointSize(small.GetFractionalPointSize() * 0.85);
		dc.SetFont(small);
		dc.SetTextForeground(p.dim);
		for (int q = 0; q <= 4; ++q) {
			int x = t.x + t.width * q / 4;
			dc.SetPen(wxPen(theme::Mix(p.field, p.text, 0.18), thick));
			if (q > 0 && q < 4) dc.DrawLine(x, t.y + 1, x, t.GetBottom());
			if (q % 2) continue;
			wxString label = wxString::Format("%d%%", q * 25);
			int w = dc.GetTextExtent(label).x;
			int lx = q == 0 ? t.x : q == 4 ? t.GetRight() - w : x - w / 2;
			dc.DrawText(label, lx, t.GetBottom() + FromDIP(2));
		}

		wxColour fill = theme::Mix(p.field, p.accent, theme::IsDark() ? 0.35 : 0.22);
		if (alphas.size() == 3 && points.size() == 4) {
			// The opacity: high where the text shows
			std::vector<wxPoint> curve = {{t.x, t.GetBottom()}, {t.x, AlphaY(alphas[0])}};
			for (size_t i = 0; i < 4; ++i) curve.emplace_back(X(points[i]), AlphaY(PointAlpha(i)));
			curve.emplace_back(t.GetRight(), AlphaY(alphas[2]));
			curve.emplace_back(t.GetRight(), t.GetBottom());
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(fill));
			dc.DrawPolygon(static_cast<int>(curve.size()), curve.data());
			dc.SetPen(wxPen(p.accent, std::max(1, FromDIP(3) / 2)));
			for (size_t i = 1; i + 2 < curve.size(); ++i)
				dc.DrawLine(curve[i], curve[i + 1]);
		}
		else if (points.size() == 2) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(fill));
			dc.DrawRectangle(X(points[0]), t.y + 1, std::max(1, X(points[1]) - X(points[0])), t.height - 2);
		}

		// The video's time
		if (playhead && *playhead >= 0 && *playhead <= duration) {
			int x = X(*playhead);
			dc.SetPen(wxPen(p.text, thick));
			dc.DrawLine(x, t.y - FromDIP(2), x, t.GetBottom() + 1);
		}

		// The points that can be dragged
		for (size_t i = 0; i < points.size(); ++i) {
			int x = X(points[i]);
			bool held = static_cast<int>(i) == dragging;
			dc.SetPen(wxPen(theme::IsDark() ? wxColour(20, 20, 20) : *wxWHITE, thick));
			dc.SetBrush(wxBrush(held ? theme::Mix(p.accent, p.text, 0.25) : p.accent));
			if (alphas.size() == 3)
				dc.DrawCircle(x, AlphaY(PointAlpha(i)), FromDIP(held ? 6 : 5));
			else
				dc.DrawRoundedRectangle(x - FromDIP(3), t.y - FromDIP(1), FromDIP(6), t.height + FromDIP(2), FromDIP(3));
		}
	}

	/// The point under @p x: the nearest, the later of two together when
	/// right of them
	int Hit(wxPoint pos) const {
		int best = -1, best_distance = FromDIP(9);
		for (size_t i = 0; i < points.size(); ++i) {
			int d = std::abs(X(points[i]) - pos.x);
			if (d < best_distance || (d == best_distance && pos.x > X(points[i]))) {
				best = static_cast<int>(i);
				best_distance = d;
			}
		}
		return best;
	}

	void MoveTo(int x, bool snap, bool final) {
		int lo = dragging > 0 ? points[dragging - 1] : 0;
		int hi = dragging + 1 < static_cast<int>(points.size()) ? points[dragging + 1] : duration;
		int ms = std::clamp(Ms(x), lo, hi);
		if (on_point) ms = std::clamp(on_point(dragging, ms, snap, final), lo, hi);
		points[dragging] = ms;
		Refresh(false);
	}

public:
	/// A point dragged to @p ms; returns where it goes (snapped to a frame)
	std::function<int(int index, int ms, bool snap, bool final)> on_point;
	/// A click away from the points, at @p ms
	std::function<void(int ms)> on_seek;

	explicit Timeline(wxWindow *parent) : Part(parent, wxSize(120, 46)) {
		SetCursor(wxCursor(wxCURSOR_HAND));
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
			if (!IsEnabled()) return;
			dragging = Hit(e.GetPosition());
			moved = false;
			if (dragging < 0) {
				if (on_seek) on_seek(Ms(e.GetX()));
				return;
			}
			CaptureMouse();
			Refresh(false);
		});
		Bind(wxEVT_MOTION, [this](wxMouseEvent &e) {
			if (dragging < 0 || !HasCapture()) {
				SetCursor(wxCursor(Hit(e.GetPosition()) >= 0 ? wxCURSOR_SIZEWE : wxCURSOR_HAND));
				return;
			}
			moved = true;
			MoveTo(e.GetX(), !e.ShiftDown(), false);
		});
		auto release = [this](bool snap, int x) {
			if (dragging < 0) return;
			if (HasCapture()) ReleaseMouse();
			if (moved) MoveTo(x, snap, true);
			dragging = -1;
			Refresh(false);
		};
		Bind(wxEVT_LEFT_UP, [=](wxMouseEvent &e) { release(!e.ShiftDown(), e.GetX()); });
		Bind(wxEVT_MOUSE_CAPTURE_LOST, [=, this](wxMouseCaptureLostEvent &) {
			if (dragging >= 0) release(true, X(points[dragging]));
		});
	}

	void Set(int new_duration, std::vector<int> new_points, std::vector<int> new_alphas) {
		if (dragging >= 0) return; // the drag shows where the point is
		duration = std::max(1, new_duration);
		points = std::move(new_points);
		alphas = std::move(new_alphas);
		Refresh(false);
	}
	void SetPlayhead(std::optional<int> ms) {
		if (ms == playhead) return;
		playhead = ms;
		Refresh(false);
	}

	/// As dragging point @p index to @p ms and letting go does
	void Drag(int index, int ms, bool final) {
		if (index < 0 || index >= static_cast<int>(points.size())) return;
		// Exactly there, not where a pixel would put it
		int lo = index > 0 ? points[index - 1] : 0;
		int hi = index + 1 < static_cast<int>(points.size()) ? points[index + 1] : duration;
		ms = std::clamp(ms, lo, hi);
		dragging = final ? -1 : index;
		points[index] = on_point ? on_point(index, ms, false, final) : ms;
		Refresh(false);
	}

	wxString Shown() const override {
		wxString shown;
		for (int point : points) shown += wxString::Format(shown.empty() ? "%d" : ",%d", point);
		return shown + wxString::Format("/%d", duration);
	}
};

/// A button with words on it, as the fields are drawn
class AnimationPage::TextButton final : public StylePanel::Part {
	wxString label;
	bool pressed = false;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		dc.SetBrush(wxBrush(hover && IsEnabled() ? theme::Mix(p.field, p.text, pressed ? 0.1 : 0.05) : p.field));
		dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(5));
		wxString text = Ellipsize(dc, label, size.x - FromDIP(8));
		wxSize extent = dc.GetTextExtent(text);
		dc.SetTextForeground(IsEnabled() ? p.text : p.dim);
		dc.DrawText(text, (size.x - extent.x) / 2, (size.y - extent.y) / 2);
	}

public:
	std::function<void()> on_click;

	TextButton(wxWindow *parent, wxString label, wxString const& tip)
	: Part(parent, wxSize(40, 26)), label(std::move(label))
	{
		SetToolTip(tip);
		SetCursor(wxCursor(wxCURSOR_HAND));
		wxClientDC dc(this);
		dc.SetFont(GetFont());
		SetMinSize(wxSize(dc.GetTextExtent(this->label).x + FromDIP(16), FromDIP(26)));
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
			if (!IsEnabled()) return;
			pressed = true;
			Refresh(false);
			if (on_click) CallAfter([this] { pressed = false; Refresh(false); if (on_click) on_click(); });
		});
	}

	wxString Shown() const override { return label; }
};

/// A card's title over a line across the page: bold, with the tag it
/// writes after it, dim
class AnimationPage::CardTitle final : public StylePanel::Part {
	wxString title, tag;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		dc.SetBrush(wxBrush(p.line));
		dc.DrawRectangle(0, 0, size.x, std::max(1, FromDIP(1)));
		wxFont font = GetFont();
		font.MakeBold();
		dc.SetFont(font);
		dc.SetTextForeground(p.text);
		int y = (size.y - dc.GetCharHeight()) / 2 + FromDIP(2);
		int left = FromDIP(12);
		dc.DrawText(title, left, y);
		int x = left + dc.GetTextExtent(title).x + FromDIP(8);
		dc.SetFont(GetFont());
		dc.SetTextForeground(p.dim);
		dc.DrawText(Ellipsize(dc, tag, std::max(0, size.x - x)), x, y + (dc.GetCharHeight() < size.y ? 0 : 0));
	}

public:
	CardTitle(wxWindow *parent, wxString title, wxString tag)
	: Part(parent, wxSize(40, 34)), title(std::move(title)), tag(std::move(tag)) { }
	void SetTag(wxString const& new_tag) { Set(tag, new_tag); }
	wxString Shown() const override { return title + " " + tag; }
};

/// A colour a \t goes to: its name, and the colour, which opens the picker
class AnimationPage::ColourTarget final : public StylePanel::Part {
	wxString label;
	agi::Color colour;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		dc.SetBrush(wxBrush(hover ? theme::Mix(p.field, p.text, 0.05) : p.field));
		dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(5));
		int pad = FromDIP(8);
		dc.SetTextForeground(p.dim);
		dc.DrawText(label, pad, (size.y - dc.GetCharHeight()) / 2);
		int swatch = size.y - FromDIP(10);
		wxRect box(size.x - pad - swatch * 2, FromDIP(5), swatch * 2, swatch);
		dc.SetBrush(wxBrush(wxColour(colour.r, colour.g, colour.b)));
		dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(box, FromDIP(3));
	}

public:
	std::function<void()> on_pick;

	ColourTarget(wxWindow *parent, wxString label, wxString const& tip)
	: Part(parent, wxSize(48, 28)), label(std::move(label))
	{
		SetToolTip(tip);
		SetCursor(wxCursor(wxCURSOR_HAND));
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { if (on_pick) CallAfter([this] { if (on_pick) on_pick(); }); });
	}
	void SetColour(agi::Color new_colour) {
		if (new_colour == colour) return;
		colour = new_colour;
		Refresh(false);
	}
	wxString Shown() const override { return to_wx(colour.GetAssOverrideFormatted()); }
};

std::vector<AnimationPage::TargetKind> const& AnimationPage::TargetKinds() {
	using V = TargetKind::Value;
	static std::vector<TargetKind> kinds = {
		{"\\fs", _("Size"), "", V::Number, 1, 1000, 1},
		{"\\fscx", _("Scale X"), "%", V::Number, 0, 1000, 1},
		{"\\fscy", _("Scale Y"), "%", V::Number, 0, 1000, 1},
		{"\\frz", _("Rotation"), wxS("°"), V::Number, -3600, 3600, 1},
		{"\\frx", _("Rotation X"), wxS("°"), V::Number, -3600, 3600, 1},
		{"\\fry", _("Rotation Y"), wxS("°"), V::Number, -3600, 3600, 1},
		{"\\fsp", _("Spacing"), "px", V::Number, -100, 100, 0.1},
		{"\\bord", _("Outline"), "", V::Number, 0, 100, 0.1},
		{"\\shad", _("Shadow"), "", V::Number, 0, 100, 0.1},
		{"\\blur", _("Blur"), "", V::Number, 0, 100, 0.1},
		{"\\fax", _("Shear X"), "", V::Number, -10, 10, 0.01},
		{"\\fay", _("Shear Y"), "", V::Number, -10, 10, 0.01},
		{"\\alpha", _("Opacity"), "%", V::Opacity, 0, 100, 1},
		{"\\1a", _("Fill opacity"), "%", V::Opacity, 0, 100, 1},
		{"\\1c", _("Fill colour"), "", V::Colour, 0, 0, 0},
		{"\\3c", _("Outline colour"), "", V::Colour, 0, 0, 0},
		{"\\4c", _("Shadow colour"), "", V::Colour, 0, 0, 0},
	};
	return kinds;
}

namespace {
AnimationPage::TargetKind const* KindFor(std::string name) {
	if (name == "\\c") name = "\\1c";
	for (auto const& kind : AnimationPage::TargetKinds())
		if (name == kind.name) return &kind;
	return nullptr;
}
}

template<class T, class... Args>
T *AnimationPage::Add(std::string const& name, Args&&... args) {
	auto part = new T(this, std::forward<Args>(args)...);
	parts[name] = part;
	return part;
}

AnimationPage::AnimationPage(wxWindow *parent, agi::Context *c)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
, c(c)
{
	theme::UseSurface(this);
	body = new wxBoxSizer(wxVERTICAL);
	SetSizer(body);

	auto schedule = [this](SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); };
	connections.push_back(c->subtitleAccess->Read().AddCommitListener("animation_page.AddCommitListener",
		[this](int, AssDialogue const*, SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); },
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->selectionController->AddSelectionListener("animation_page.AddSelectionListener",
		schedule, std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->selectionController->AddActiveLineListener("animation_page.AddActiveLineListener",
		schedule, std::weak_ptr<void const>(model_observer_lifetime)));
	if (c->videoController)
		seek_connection = c->videoController->AddSeekListener([this](int) {
			auto now = VideoTime();
			for (auto const& [name, part] : parts)
				if (auto timeline = dynamic_cast<Timeline*>(part))
					timeline->SetPlayhead(now ? std::optional<int>(*now - start) : std::nullopt);
		});
	UpdateNow();
}

AnimationPage::~AnimationPage() = default;

void AnimationPage::ScheduleUpdate() {
	if (update_pending) return;
	update_pending = true;
	CallAfter([this] {
		update_pending = false;
		UpdateNow();
	});
}

std::optional<int> AnimationPage::VideoTime() const {
	if (!c->project || !c->project->VideoProvider() || !c->videoController) return std::nullopt;
	return c->videoController->TimeAtFrame(c->videoController->GetFrameN());
}

int AnimationPage::Snap(int ms) const {
	if (!c->project || !c->project->VideoProvider() || !c->videoController) return ms;
	auto vc = c->videoController.get();
	int frame = vc->FrameAtTime(ms);
	int before = vc->TimeAtFrame(frame), after = vc->TimeAtFrame(frame + 1);
	return ms - before <= after - ms ? before : after;
}

void AnimationPage::Seek(int ms) {
	if (c->project && c->project->VideoProvider() && c->videoController)
		c->videoController->JumpToTime(ms);
}

int AnimationPage::Write(wxString const& undo_name, Change const& change, int amend) {
	auto scope = c->subtitleAccess->TryWrite();
	if (!scope) return -1;
	SubtitleRowEdit staged(c);
	bool changed = false;
	for (auto row : staged.rows) {
		int length = std::max(0, static_cast<int>(row->End) - static_cast<int>(row->Start));
		std::string text = change(*row, Read(row->Text, length), length);
		if (text == row->Text.get()) continue;
		row->Text = text;
		changed = true;
	}
	if (!changed) return amend;
	auto edit = c->subtitleAccess->Edit(scope->Lease());
	edit.UpdateRows(staged.Values());
	return edit.Commit(undo_name, AssFile::COMMIT_DIAG_TEXT, amend, staged.rows.size() == 1 ? staged.rows.front()->Id : 0);
}

void AnimationPage::WriteNumber(wxString const& undo_name, Change const& change, bool final) {
	drag_commit = Write(undo_name, change, drag_commit);
	if (final) drag_commit = -1;
}

namespace {
/// A change to the tags of @p card: @p edit changes a line's animation,
/// false where the line has nothing for it to change
using Edit = std::function<bool(Animation&, AssDialogue const&, int duration)>;
std::function<std::string(AssDialogue const&, Animation const&, int)> Mutate(std::string const& card, Edit edit) {
	return [card, edit](AssDialogue const& line, Animation const& before, int duration) {
		Animation a = before;
		if (!edit(a, line, duration)) return line.Text.get();
		return WriteKind(line.Text.get(), a, KindOf(card), duration);
	};
}

/// The range of @p card in @p a: \move's or a \t's times
std::optional<std::pair<int, int>> RangeOf(Animation const& a, std::string const& card) {
	if (card == "move") {
		if (!a.move) return std::nullopt;
		return std::make_pair(a.move->t1, a.move->t2);
	}
	size_t i = TransformIndex(card);
	if (i >= a.transforms.size()) return std::nullopt;
	return std::make_pair(a.transforms[i].t1, a.transforms[i].t2);
}
bool SetRange(Animation &a, std::string const& card, int t1, int t2) {
	if (card == "move") {
		if (!a.move) return false;
		a.move->t1 = t1;
		a.move->t2 = t2;
		return true;
	}
	size_t i = TransformIndex(card);
	if (i >= a.transforms.size()) return false;
	a.transforms[i].t1 = t1;
	a.transforms[i].t2 = t2;
	return true;
}

/// The fade's times in order
std::vector<int> FadeTimes(Fade const& f) { return {f.t1, f.t2, f.t3, f.t4}; }
void SetFadeTime(Fade &f, int index, int ms) {
	int *t[] = {&f.t1, &f.t2, &f.t3, &f.t4};
	*t[index] = ms;
	// The others make room, in order
	for (int i = index + 1; i < 4; ++i) *t[i] = std::max(*t[i], *t[i - 1]);
	for (int i = index - 1; i >= 0; --i) *t[i] = std::min(*t[i], *t[i + 1]);
}
}

void AnimationPage::AddCardTitle(std::string const& card, wxString const& title, wxString const& tag) {
	cards.push_back(card);
	auto row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<CardTitle>(card + ".title", title, tag), 1, wxEXPAND);
	auto remove = Add<IconButton>(card + ".remove", IconButton::Glyph::Close, _("Remove this effect from the selected lines"));
	remove->on_click = [this, card] { RemoveCard(card); };
	row->Add(remove, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP, FromDIP(6));
	body->Add(row, 0, wxEXPAND | wxTOP, FromDIP(4));
	card_body = new wxBoxSizer(wxVERTICAL);
	body->Add(card_body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
}

void AnimationPage::AddRow(wxSizer *row, bool first) {
	card_body->Add(row, 0, wxEXPAND | (first ? 0 : wxTOP), FromDIP(3));
}

wxWindow *AnimationPage::AddSubtitle(std::string const& name, wxString const& text) {
	auto caption = Add<Caption>(name);
	caption->SetText(text);
	card_body->Add(caption, 0, wxEXPAND | wxTOP, FromDIP(6));
	return caption;
}

void AnimationPage::AddTimeRow(std::string const& name, wxString const& label, wxString const& undo_name,
	std::function<int(Animation const&, int)> get, std::function<bool(Animation&, int, int)> set)
{
	std::string card = name.substr(0, name.find('.'));
	auto row = new wxBoxSizer(wxHORIZONTAL);
	auto time = Add<Field>(name, label, "", false,
		_("A time of the video, h:mm:ss.cc: drag sideways or click and type. Each selected line gets the same time from its start"));
	time->SetFormat(TimeText, ReadTime);
	time->SetNumberHandler({0, 1, 10}, [this, card, set, undo_name](double v, bool final) {
		int ms = static_cast<int>(std::lround(v)) - start;
		WriteNumber(undo_name, Mutate(card, [=](Animation &a, AssDialogue const&, int d) {
			return set(a, d, std::clamp(ms, 0, d));
		}), final);
	});
	row->Add(time, 1, wxEXPAND);

	auto now = Add<IconButton>(name + "_now", IconButton::Glyph::Now, _("Use the video's current time"));
	now->on_click = [this, card, set, undo_name] {
		auto t = VideoTime();
		if (!t) return;
		int at = *t;
		Write(undo_name, Mutate(card, [=](Animation &a, AssDialogue const& line, int d) {
			return set(a, d, std::clamp(at - static_cast<int>(line.Start), 0, d));
		}));
	};
	row->Add(now, 0, wxLEFT, FromDIP(2));

	auto percent = Add<Field>(name + "%", "", "%", false, _("The same time as a share of the line: 50% is halfway through each selected line"));
	percent->SetMinSize(wxSize(FromDIP(62), -1));
	percent->SetNumberHandler({0, 100, 1}, [this, card, set, undo_name](double v, bool final) {
		WriteNumber(undo_name, Mutate(card, [=](Animation &a, AssDialogue const&, int d) {
			return set(a, d, static_cast<int>(std::lround(v * d / 100)));
		}), final);
	});
	row->Add(percent, 0, wxEXPAND | wxLEFT, FromDIP(2));
	AddRow(row);

	refreshers.push_back([this, get, time, percent, now] {
		bool known = line != nullptr;
		int ms = known ? get(current, duration) : 0;
		time->SetRange(start, start + duration);
		time->SetNumber(start + ms, known);
		percent->SetNumber(duration > 0 ? ms * 100.0 / duration : 0, known);
		now->Enable(VideoTime().has_value());
	});
}

void AnimationPage::AddRange(std::string const& card, wxString const& undo_name) {
	auto timeline = Add<Timeline>(card + ".bar");
	timeline->SetToolTip(_("The line from start to end: drag the ends of the range, snapped to the video's frames unless Shift is held; click elsewhere to go there in the video"));
	timeline->on_point = [this, card, undo_name](int index, int ms, bool snap, bool final) {
		if (snap) ms = std::clamp(Snap(start + ms) - start, 0, duration);
		WriteNumber(undo_name, Mutate(card, [=](Animation &a, AssDialogue const&, int d) {
			auto range = RangeOf(a, card);
			if (!range) return false;
			int t = std::clamp(ms, 0, d);
			return index == 0 ? SetRange(a, card, std::min(t, range->second), range->second)
				: SetRange(a, card, range->first, std::max(t, range->first));
		}), final);
		Seek(start + ms);
		return ms;
	};
	timeline->on_seek = [this](int ms) { Seek(start + ms); };
	card_body->Add(timeline, 0, wxEXPAND);
	refreshers.push_back([this, card, timeline] {
		auto range = RangeOf(current, card);
		timeline->Set(duration, range ? std::vector<int>{range->first, range->second} : std::vector<int>{}, {});
	});

	AddTimeRow(card + ".in", _("In"), undo_name,
		[card](Animation const& a, int) { auto r = RangeOf(a, card); return r ? r->first : 0; },
		[card](Animation &a, int, int ms) {
			auto r = RangeOf(a, card);
			return r && SetRange(a, card, std::min(ms, r->second), r->second);
		});
	AddTimeRow(card + ".out", _("Out"), undo_name,
		[card](Animation const& a, int) { auto r = RangeOf(a, card); return r ? r->second : 0; },
		[card](Animation &a, int, int ms) {
			auto r = RangeOf(a, card);
			return r && SetRange(a, card, r->first, std::max(ms, r->first));
		});

	// The usual shares of the line
	auto presets = new wxBoxSizer(wxHORIZONTAL);
	struct Preset { const char *name; wxString label, tip; double from, to; };
	Preset list[] = {
		{"whole", _("Whole line"), _("From the line's start to its end"), 0, 1},
		{"first_half", _("First half"), _("The first half of each selected line"), 0, 0.5},
		{"second_half", _("Second half"), _("The second half of each selected line"), 0.5, 1},
	};
	for (auto const& preset : list) {
		auto button = Add<TextButton>(card + "." + preset.name, preset.label, preset.tip);
		double from = preset.from, to = preset.to;
		button->on_click = [this, card, undo_name, from, to] {
			Write(undo_name, Mutate(card, [=](Animation &a, AssDialogue const&, int d) {
				return SetRange(a, card, static_cast<int>(std::lround(from * d)), static_cast<int>(std::lround(to * d)));
			}));
		};
		presets->Add(button, 1, wxEXPAND | (presets->IsEmpty() ? 0 : wxLEFT), FromDIP(3));
	}
	card_body->Add(presets, 0, wxEXPAND | wxTOP, FromDIP(6));
}

namespace {
/// The coordinates a card holds: \move's two points, \org's one
double *Coordinate(Animation &a, std::string const& name) {
	if (name == "move.x1") return a.move ? &a.move->x1 : nullptr;
	if (name == "move.y1") return a.move ? &a.move->y1 : nullptr;
	if (name == "move.x2") return a.move ? &a.move->x2 : nullptr;
	if (name == "move.y2") return a.move ? &a.move->y2 : nullptr;
	if (name == "origin.x") return a.origin ? &a.origin->x : nullptr;
	if (name == "origin.y") return a.origin ? &a.origin->y : nullptr;
	return nullptr;
}
}

void AnimationPage::AddPoint(std::string const& card, std::string const& x, std::string const& y, wxString const& label,
	wxString const& undo_name, const char *tool, wxString const& pick_tip, bool first)
{
	auto row = new wxBoxSizer(wxHORIZONTAL);
	auto caption = Add<Caption>(x + ".label");
	caption->SetText(label);
	{
		// The same width for each point's name, so that the fields line up
		wxClientDC dc(caption);
		dc.SetFont(caption->GetFont());
		int widest = 0;
		for (auto const& name : {_("Start"), _("End"), _("Centre")}) widest = std::max(widest, dc.GetTextExtent(name).x);
		caption->SetMinSize(wxSize(widest + FromDIP(8), -1));
	}
	row->Add(caption, 0, wxEXPAND);
	for (auto const& name : {x, y}) {
		auto field = Add<Field>(name, name == x ? "X" : "Y", "", false,
			name == x ? _("Across, in the script's pixels") : _("Down, in the script's pixels"));
		field->SetNumberHandler({-100000, 100000, 1}, [this, card, name, undo_name](double v, bool final) {
			WriteNumber(undo_name, Mutate(card, [=](Animation &a, AssDialogue const&, int) {
				double *coordinate = Coordinate(a, name);
				if (!coordinate) return false;
				*coordinate = v;
				return true;
			}), final);
		});
		row->Add(field, 1, wxEXPAND | (name == x ? 0 : wxLEFT), FromDIP(4));
		refreshers.push_back([this, name, field] {
			Animation a = current;
			double *coordinate = Coordinate(a, name);
			field->SetNumber(coordinate ? *coordinate : 0, coordinate != nullptr);
		});
	}
	auto pick = Add<IconButton>(x + ".pick", IconButton::Glyph::Pick, pick_tip);
	std::string command = tool;
	pick->on_click = [this, command] {
		if (auto cmd = cmd::get(command); cmd && cmd->Validate(c)) cmd::call(command, c);
	};
	row->Add(pick, 0, wxLEFT, FromDIP(2));
	refreshers.push_back([this, pick] { pick->Enable(VideoTime().has_value()); });
	AddRow(row, first);
}

void AnimationPage::BuildMove() {
	const wxString undo = _("set move");
	AddCardTitle("move", _("Move"), "\\move");
	const wxString pick_tip = _("Drag the start and the end on the video, with the drag tool");
	AddPoint("move", "move.x1", "move.y1", _("Start"), undo, "video/tool/drag", pick_tip, true);
	AddPoint("move", "move.x2", "move.y2", _("End"), undo, "video/tool/drag", pick_tip, false);
	card_body->AddSpacer(FromDIP(6));
	AddRange("move", undo);
}

void AnimationPage::BuildFade() {
	const wxString undo = _("set fade");
	AddCardTitle("fade", _("Fade"), "\\fad");
	auto title = static_cast<CardTitle*>(parts["fade.title"]);
	refreshers.push_back([this, title] {
		title->SetTag(current.fade && !current.fade->Simple(duration) ? "\\fade" : "\\fad");
	});

	auto timeline = Add<Timeline>("fade.bar");
	timeline->SetToolTip(_("How much of the text shows over the line: drag the points where it starts and ends showing, snapped to the video's frames unless Shift is held; click elsewhere to go there in the video"));
	timeline->on_point = [this, undo](int index, int ms, bool snap, bool final) {
		if (snap) ms = std::clamp(Snap(start + ms) - start, 0, duration);
		WriteNumber(undo, Mutate("fade", [=](Animation &a, AssDialogue const&, int d) {
			if (!a.fade) return false;
			SetFadeTime(*a.fade, index, std::clamp(ms, 0, d));
			return true;
		}), final);
		Seek(start + ms);
		return ms;
	};
	timeline->on_seek = [this](int ms) { Seek(start + ms); };
	card_body->Add(timeline, 0, wxEXPAND);
	refreshers.push_back([this, timeline] {
		if (!current.fade) return timeline->Set(duration, {}, {});
		auto const& f = *current.fade;
		timeline->Set(duration, FadeTimes(f), {f.a1, f.a2, f.a3});
	});

	auto time_row = [this, undo](std::string const& name, wxString const& label, int index) {
		AddTimeRow(name, label, undo,
			[index](Animation const& a, int) { return a.fade ? FadeTimes(*a.fade)[index] : 0; },
			[index](Animation &a, int, int ms) {
				if (!a.fade) return false;
				SetFadeTime(*a.fade, index, ms);
				return true;
			});
	};
	AddSubtitle("fade.in_title", _("Fade in"));
	time_row("fade.in_start", _("Start"), 0);
	time_row("fade.in_end", _("Shown"), 1);
	AddSubtitle("fade.out_title", _("Fade out"));
	time_row("fade.out_start", _("Start"), 2);
	time_row("fade.out_end", _("Gone"), 3);

	// How much shows before, between and after: all of it between, as \fad has it
	AddSubtitle("fade.opacity_title", _("Opacity"));
	auto row = new wxBoxSizer(wxHORIZONTAL);
	struct Level { const char *name; wxString label, tip; int Fade::*alpha; };
	Level levels[] = {
		{"fade.before", _("Before"), _("How much of the text shows before it fades in"), &Fade::a1},
		{"fade.during", _("Mid"), _("How much of the text shows between fading in and out"), &Fade::a2},
		{"fade.after", _("After"), _("How much of the text shows after it fades out"), &Fade::a3},
	};
	for (auto const& level : levels) {
		auto field = Add<Field>(level.name, level.label, "%", false, level.tip);
		auto alpha = level.alpha;
		field->SetNumberHandler({0, 100, 1}, [this, undo, alpha](double v, bool final) {
			WriteNumber(undo, Mutate("fade", [=](Animation &a, AssDialogue const&, int) {
				if (!a.fade) return false;
				(*a.fade).*alpha = AlphaOfOpacity(v);
				return true;
			}), final);
		});
		row->Add(field, 1, wxEXPAND | (row->IsEmpty() ? 0 : wxLEFT), FromDIP(4));
		refreshers.push_back([this, field, alpha] {
			field->SetNumber(current.fade ? std::round((255 - (*current.fade).*alpha) * 100 / 255.) : 0, current.fade.has_value());
		});
	}
	AddRow(row);
}

void AnimationPage::BuildTransform(size_t index) {
	const wxString undo = _("set transform");
	std::string card = "t" + std::to_string(index);
	AddCardTitle(card, _("Transform"), "\\t");
	AddRange(card, undo);

	auto accel = Add<Field>(card + ".accel", _("Acceleration"), "", false,
		_("How the change speeds up: 1 is steady, above 1 starts slowly and ends fast, below 1 the other way"));
	accel->SetNumberHandler({0.01, 20, 0.05}, [this, card, undo, index](double v, bool final) {
		WriteNumber(undo, Mutate(card, [=](Animation &a, AssDialogue const&, int) {
			if (index >= a.transforms.size()) return false;
			a.transforms[index].accel = v;
			return true;
		}), final);
	});
	card_body->Add(accel, 0, wxEXPAND | wxTOP, FromDIP(6));
	refreshers.push_back([this, accel, index] {
		bool known = index < current.transforms.size();
		accel->SetNumber(known ? current.transforms[index].accel : 1, known);
	});

	AddSubtitle(card + ".targets_title", _("Changes to"));
	auto const& targets = current.transforms[index].targets;
	for (size_t j = 0; j < targets.size(); ++j) {
		std::string tag = targets[j].name;
		std::string name = card + ".target" + std::to_string(j);
		auto kind = KindFor(tag);
		auto row = new wxBoxSizer(wxHORIZONTAL);
		// Each line's target of the same tag
		auto set_value = [this, card, index, tag, undo](std::string value, bool final, bool drag) {
			auto change = Mutate(card, [=](Animation &a, AssDialogue const&, int) {
				if (index >= a.transforms.size()) return false;
				for (auto &target : a.transforms[index].targets)
					if (target.name == tag) { target.value = value; return true; }
				return false;
			});
			if (drag) WriteNumber(undo, change, final);
			else Write(undo, change);
		};
		auto value_of = [this, index, tag]() -> std::optional<std::string> {
			if (index >= current.transforms.size()) return std::nullopt;
			for (auto const& target : current.transforms[index].targets)
				if (target.name == tag) return target.value;
			return std::nullopt;
		};
		if (kind && kind->value == TargetKind::Value::Colour) {
			auto colour = Add<ColourTarget>(name, kind->label, _("The colour it changes to: click to choose"));
			colour->on_pick = [this, index, tag] { PickColour(index, tag); };
			row->Add(colour, 1, wxEXPAND);
			refreshers.push_back([colour, value_of] {
				if (auto v = value_of()) colour->SetColour(agi::Color(*v));
			});
		}
		else if (kind) {
			bool opacity = kind->value == TargetKind::Value::Opacity;
			auto field = Add<Field>(name, kind->label, kind->unit, false, wxString::Format(_("The value it changes to (%s)"), to_wx(tag)));
			field->SetNumberHandler({kind->min, kind->max, kind->step}, [set_value, opacity](double v, bool final) {
				set_value(opacity ? AlphaValue(AlphaOfOpacity(v)) : Number(v), final, true);
			});
			row->Add(field, 1, wxEXPAND);
			refreshers.push_back([field, value_of, opacity] {
				auto v = value_of();
				field->SetNumber(v ? (opacity ? OpacityOf(*v) : ValueNumber(*v)) : 0, v.has_value());
			});
		}
		else {
			// A tag the page doesn't know: kept as written
			auto caption = Add<Caption>(name);
			caption->SetText(to_wx(tag + targets[j].value));
			caption->SetToolTip(_("A tag set by hand: it stays as it is"));
			row->Add(caption, 1, wxEXPAND | wxLEFT, FromDIP(8));
		}
		auto remove = Add<IconButton>(name + ".remove", IconButton::Glyph::Close, _("Change this no more"));
		remove->on_click = [this, card, index, tag, undo] {
			Write(undo, Mutate(card, [=](Animation &a, AssDialogue const&, int) {
				if (index >= a.transforms.size()) return false;
				auto &list = a.transforms[index].targets;
				auto it = std::find_if(list.begin(), list.end(), [&](Target const& t) { return t.name == tag; });
				if (it == list.end()) return false;
				list.erase(it);
				// A \t changing nothing goes
				if (list.empty()) a.transforms.erase(a.transforms.begin() + index);
				return true;
			}));
		};
		row->Add(remove, 0, wxLEFT, FromDIP(2));
		AddRow(row, j == 0);
	}
	auto add = Add<TextButton>(card + ".add", _("+ Property"), _("Choose another property for it to change"));
	add->on_click = [this, index] { ShowTargetMenu(index); };
	card_body->Add(add, 0, wxTOP, FromDIP(6));
}

void AnimationPage::BuildOrigin() {
	AddCardTitle("origin", _("Rotation origin"), "\\org");
	AddPoint("origin", "origin.x", "origin.y", _("Centre"), _("set origin"), "video/tool/rotate/z",
		_("Drag the origin on the video, with the rotate tool"), true);
}

void AnimationPage::Build() {
	Freeze();
	refreshers.clear();
	cards.clear();
	body->Clear(true);
	parts.clear();
	const int pad = FromDIP(12);

	auto scope = Add<Caption>("scope");
	body->Add(scope, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	auto times = Add<Caption>("line");
	body->Add(times, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
	refreshers.push_back([this, scope, times] {
		scope->SetText(!line ? _("No line selected")
			: line_count > 1 ? fmt_plural(line_count, "%d selected line, shown as the current one is", "%d selected lines, shown as the current one is", line_count)
			: _("The current line"));
		times->SetText(line ? TimeText(start) + wxS(" → ") + TimeText(start + duration) : wxString());
	});

	if (line) {
		if (current.move) BuildMove();
		if (current.fade) BuildFade();
		for (size_t i = 0; i < current.transforms.size(); ++i) BuildTransform(i);
		if (current.origin) BuildOrigin();
	}
	if (cards.empty() && line) {
		auto hint = Add<Caption>("hint");
		hint->SetText(_("Nothing moves yet: add an effect"));
		body->Add(hint, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	}

	auto add = Add<TextButton>("add", _("+ Add effect"), _("Make the selected lines move, fade in and out, change over time or turn about a point"));
	add->on_click = [this] { ShowAddMenu(); };
	add->Enable(line != nullptr);
	body->Add(add, 0, wxEXPAND | wxALL, pad);

	Layout();
	Thaw();
	InvalidateBestSize();
	if (on_layout) on_layout();
}

void AnimationPage::Display() {
	for (auto const& refresh : refreshers) refresh();
	auto now = VideoTime();
	for (auto const& [name, part] : parts)
		if (auto timeline = dynamic_cast<Timeline*>(part))
			timeline->SetPlayhead(now && line ? std::optional<int>(*now - start) : std::nullopt);
}

void AnimationPage::UpdateNow() {
	update_pending = false;
	line = c->selectionController->ReadActiveLine();
	auto selected = c->selectionController->ReadSelectedLines();
	if (!line && !selected.empty()) line = selected.front();
	line_count = std::max<size_t>(selected.size(), line ? 1 : 0);
	start = line ? static_cast<int>(line->Start) : 0;
	duration = line ? std::max(0, static_cast<int>(line->End) - start) : 0;
	current = line ? Read(line->Text, duration) : Animation{};

	std::string key = line ? "L" : "-";
	if (current.move) key += "M";
	if (current.fade) key += "F";
	for (auto const& t : current.transforms) {
		key += "T";
		for (auto const& target : t.targets) key += target.name + ",";
	}
	if (current.origin) key += "O";
	if (key != structure) {
		structure = key;
		Build();
	}
	Display();
}

namespace {
/// What @p look has for the tag @p name, as a \t's value
std::string CurrentValue(StyleLook const& look, std::string const& name) {
	if (name == "\\fs") return Number(look.size);
	if (name == "\\fscx") return Number(look.scale_x);
	if (name == "\\fscy") return Number(look.scale_y);
	if (name == "\\frz") return Number(look.angle);
	if (name == "\\fsp") return Number(look.spacing);
	if (name == "\\bord") return Number(look.border);
	if (name == "\\shad") return Number(look.shadow_x);
	if (name == "\\blur") return Number(look.blur);
	if (name == "\\alpha" || name == "\\1a") return AlphaValue(look.primary.a);
	if (name == "\\1c") return look.primary.GetAssOverrideFormatted();
	if (name == "\\3c") return look.outline.GetAssOverrideFormatted();
	if (name == "\\4c") return look.shadow.GetAssOverrideFormatted();
	return "0";
}
}

StyleLook AnimationPage::LookOfLine(AssDialogue const& row) const {
	auto const& file = c->subtitleAccess->Read();
	StyleLookup find = [&](std::string const& name) { return file.GetStyle(name); };
	int width = 0, height = 0;
	file.GetResolution(width, height);
	StyleLook look = LookAt(row, 0, find);
	DefaultPosition(look, row, find(row.Style.get()), width, height);
	return look;
}

void AnimationPage::AddEffect(Effect effect, std::string const& target) {
	switch (effect) {
		case Effect::Move:
			// Standing still where the line is, until the end is moved
			Write(_("add move"), Mutate("move", [this](Animation &a, AssDialogue const& row, int d) {
				if (a.move) return false;
				StyleLook look = LookOfLine(row);
				a.move = animation::Move{look.pos_x, look.pos_y, look.pos_x, look.pos_y, 0, d};
				return true;
			}));
			break;
		case Effect::Fade:
			Write(_("add fade"), Mutate("fade", [](Animation &a, AssDialogue const&, int d) {
				if (a.fade) return false;
				int length = std::min(300, d / 4);
				a.fade = Fade{255, 0, 255, 0, length, d - length, d};
				return true;
			}));
			break;
		case Effect::Transform:
			// To what the line has now: dragging the value makes the change
			Write(_("add transform"), Mutate("t0", [this, target](Animation &a, AssDialogue const& row, int d) {
				a.transforms.push_back(Transform{0, d, 1, {Target{target, CurrentValue(LookOfLine(row), target)}}});
				return true;
			}));
			break;
		case Effect::Origin:
			Write(_("add origin"), Mutate("origin", [this](Animation &a, AssDialogue const& row, int) {
				if (a.origin) return false;
				StyleLook look = LookOfLine(row);
				a.origin = Origin{look.pos_x, look.pos_y};
				return true;
			}));
			break;
	}
}

void AnimationPage::AddTarget(int index, std::string const& name) {
	std::string card = "t" + std::to_string(index);
	Write(_("set transform"), Mutate(card, [this, index, name](Animation &a, AssDialogue const& row, int) {
		if (index < 0 || static_cast<size_t>(index) >= a.transforms.size()) return false;
		auto &targets = a.transforms[index].targets;
		if (std::any_of(targets.begin(), targets.end(), [&](Target const& t) { return t.name == name; })) return false;
		targets.push_back({name, CurrentValue(LookOfLine(row), name)});
		return true;
	}));
}

void AnimationPage::RemoveCard(std::string const& card) {
	Write(_("remove effect"), Mutate(card, [card](Animation &a, AssDialogue const&, int) {
		switch (KindOf(card)) {
			case Kind::Move:
				if (!a.move) return false;
				// It stays where it started
				a.position = std::make_pair(a.move->x1, a.move->y1);
				a.move.reset();
				return true;
			case Kind::Fade:
				if (!a.fade) return false;
				a.fade.reset();
				return true;
			case Kind::Origin:
				if (!a.origin) return false;
				a.origin.reset();
				return true;
			case Kind::Transforms: {
				size_t i = TransformIndex(card);
				if (i >= a.transforms.size()) return false;
				a.transforms.erase(a.transforms.begin() + i);
				return true;
			}
		}
		return false;
	}));
}

void AnimationPage::ShowAddMenu() {
	wxMenu menu;
	enum { MoveId = 1, FadeId, OriginId, TargetBase = 100 };
	menu.Append(MoveId, _("Move (\\move)"))->Enable(!current.move);
	menu.Append(FadeId, _("Fade in and out (\\fad)"))->Enable(!current.fade);
	auto transform = new wxMenu;
	auto const& kinds = TargetKinds();
	for (size_t i = 0; i < kinds.size(); ++i)
		transform->Append(TargetBase + static_cast<int>(i), kinds[i].label + " (" + to_wx(kinds[i].name) + ")");
	menu.AppendSubMenu(transform, _("Change over time (\\t)"));
	menu.Append(OriginId, _("Rotation origin (\\org)"))->Enable(!current.origin);
	int chosen = GetPopupMenuSelectionFromUser(menu, parts["add"]->GetPosition() + wxPoint(0, parts["add"]->GetSize().y));
	if (chosen == MoveId) AddEffect(Effect::Move);
	else if (chosen == FadeId) AddEffect(Effect::Fade);
	else if (chosen == OriginId) AddEffect(Effect::Origin);
	else if (chosen >= TargetBase && chosen < TargetBase + static_cast<int>(kinds.size()))
		AddEffect(Effect::Transform, kinds[chosen - TargetBase].name);
}

void AnimationPage::ShowTargetMenu(int index) {
	if (index < 0 || static_cast<size_t>(index) >= current.transforms.size()) return;
	wxMenu menu;
	auto const& kinds = TargetKinds();
	auto const& targets = current.transforms[index].targets;
	for (size_t i = 0; i < kinds.size(); ++i) {
		bool used = std::any_of(targets.begin(), targets.end(), [&](Target const& t) {
			return t.name == kinds[i].name || (t.name == "\\c" && std::string(kinds[i].name) == "\\1c");
		});
		menu.Append(1 + static_cast<int>(i), kinds[i].label + " (" + to_wx(kinds[i].name) + ")")->Enable(!used);
	}
	auto button = parts["t" + std::to_string(index) + ".add"];
	int chosen = GetPopupMenuSelectionFromUser(menu, button->GetPosition() + wxPoint(0, button->GetSize().y));
	if (chosen >= 1 && chosen <= static_cast<int>(kinds.size())) AddTarget(index, kinds[chosen - 1].name);
}

void AnimationPage::PickColour(size_t index, std::string const& tag) {
	if (index >= current.transforms.size()) return;
	agi::Color initial;
	for (auto const& target : current.transforms[index].targets)
		if (target.name == tag) initial = agi::Color(target.value);
	agi::Color chosen = initial;
	if (!GetColorFromUser(c->parent, initial, false, [&](agi::Color colour) { chosen = colour; })) return;
	std::string value = chosen.GetAssOverrideFormatted();
	std::string card = "t" + std::to_string(index);
	Write(_("set transform"), Mutate(card, [=](Animation &a, AssDialogue const&, int) {
		if (index >= a.transforms.size()) return false;
		for (auto &target : a.transforms[index].targets)
			if (target.name == tag) { target.value = value; return true; }
		return false;
	}));
}

std::map<std::string, wxString> AnimationPage::Shown() const {
	std::map<std::string, wxString> shown;
	for (auto const& [name, part] : parts) {
		auto drawn = dynamic_cast<StylePanel::Part*>(part);
		if (!drawn) continue;
		shown[name] = drawn->Shown() + (part->IsEnabled() ? "" : " disabled");
	}
	wxString list;
	for (auto const& card : cards) list += (list.empty() ? "" : " ") + to_wx(card);
	shown["cards"] = list;
	return shown;
}

void AnimationPage::EnterNumber(std::string const& name, double value, bool final) {
	auto it = parts.find(name);
	if (it == parts.end()) return;
	if (auto field = dynamic_cast<Field*>(it->second)) field->Enter(value, final);
}

bool AnimationPage::TypeInto(std::string const& name, wxString const& text) {
	auto it = parts.find(name);
	auto field = it == parts.end() ? nullptr : dynamic_cast<Field*>(it->second);
	if (!field) return false;
	field->StartEditing();
	if (!field->EditBox()) return false;
	field->EditBox()->ChangeValue(text);
	field->CommitEditing();
	bool taken = !field->IsEditing();
	field->CancelEditing();
	return taken;
}

void AnimationPage::Press(std::string const& name) {
	auto it = parts.find(name);
	if (it == parts.end() || !it->second->IsEnabled()) return;
	if (auto icon = dynamic_cast<IconButton*>(it->second); icon && icon->on_click) icon->on_click();
	else if (auto text = dynamic_cast<TextButton*>(it->second); text && text->on_click) text->on_click();
}

void AnimationPage::DragPoint(std::string const& card, int index, int ms, bool final) {
	auto it = parts.find(card + ".bar");
	if (it == parts.end()) return;
	static_cast<Timeline*>(it->second)->Drag(index, ms, final);
}
