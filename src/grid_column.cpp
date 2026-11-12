// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "grid_column.h"
#include "theme.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "subtitle_binding.h"
#include "timing_adjust.h"
#include "video_controller.h"
#include "format.h"
#include "libresrc/libresrc.h"
#include "tag_summary.h"

#include <libaegisub/character_count.h>

#include <wx/dc.h>
#include <wx/image.h>

#include <map>
#include <tuple>
#include <unordered_map>

void WidthHelper::Age() {
	for (auto it = begin(widths), e = end(widths); it != e; ) {
		if (it->second.age == age)
			++it;
		else
			it = widths.erase(it);
	}
	++age;
}

int WidthHelper::operator()(boost::flyweight<std::string> const& str) {
	if (str.get().empty()) return 0;
	auto it = widths.find(str);
	if (it != end(widths)) {
		it->second.age = age;
		return it->second.width;
	}

#ifdef _WIN32
	wxMBConvUTF8 conv;
	size_t len = conv.ToWChar(nullptr, 0, str.get().c_str(), str.get().size());
	scratch.resize(len);
	conv.ToWChar(const_cast<wchar_t *>(scratch.wx_str()), len, str.get().c_str(), str.get().size());
	int width = dc->GetTextExtent(scratch).GetWidth();
#else
	int width = dc->GetTextExtent(to_wx(str)).GetWidth();
#endif

	widths[str] = {width, age};
	return width;
}

int WidthHelper::operator()(std::string const& str) {
	return dc->GetTextExtent(to_wx(str)).GetWidth();
}

int WidthHelper::operator()(wxString const& str) {
	return dc->GetTextExtent(str).GetWidth();
}

int WidthHelper::operator()(const char *str) {
	return dc->GetTextExtent(wxString::FromUTF8(str)).GetWidth();
}

int WidthHelper::operator()(const wchar_t *str) {
	return dc->GetTextExtent(str).GetWidth();
}

void GridColumn::UpdateWidth(const agi::Context *c, WidthHelper &helper) {
	if (!visible) {
		width = 0;
		return;
	}

	width = Width(c, helper);
	if (width)
		width = 2 * cell_padding + std::max(width, helper(Header()));
}

void GridColumn::Paint(wxDC &dc, int x, int y, const AssDialogue *d, const agi::Context *c) const {
	wxString str = Value(d, c);
	dc.DrawText(str, TextLeft(x, dc.GetTextExtent(str).GetWidth()), y + 2);
}

namespace {
#define COLUMN_HEADER(value) \
	private: const wxString header = value; \
	public: wxString const& Header() const override { return header; }
#define COLUMN_DESCRIPTION(value) \
	private: const wxString description = value; \
	public: wxString const& Description() const override { return description; }

/// A rounded tag behind the text of the cell at @p x, in the current brush,
/// inset from the cell so neighbouring tags stay apart.
void DrawPill(wxDC &dc, int x, int y, int width) {
	int inset_x = std::max(2, GridColumn::cell_padding / 2);
	int height = dc.GetCharHeight() + 2;
	int radius = std::max(2, height / 3);
	dc.DrawRoundedRectangle(x + inset_x, y + 1, width - 2 * inset_x, height, radius);
}

struct GridColumnLineNumber final : GridColumn {
	COLUMN_HEADER(_("#"))
	COLUMN_DESCRIPTION(_("Line Number"))
	bool Centered() const override { return true; }

	wxString Value(const AssDialogue *d, const agi::Context * = nullptr) const override {
		return std::to_wstring(d->Row + 1);
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		int number_width = c->subtitleAccess->Read().Events.empty() ? helper("1") : helper(Value(&c->subtitleAccess->Read().Events.back()));
		return std::max(number_width, helper("00"));
	}

	void Paint(wxDC &dc, int x, int y, const AssDialogue *d, const agi::Context *c) const override {
		if (!d->Comment) {
			GridColumn::Paint(dc, x, y, d, c);
			return;
		}
		// Draw a slashed eye at the grid's font size, without a font-glyph dependency.
		int eye_width = std::min(width - cell_padding, std::max(10, dc.GetCharHeight()));
		int eye_height = std::max(6, eye_width / 2);
		int cx = x + width / 2;
		int cy = y + 2 + dc.GetCharHeight() / 2;
		wxDCPenChanger pen(dc, wxPen(dc.GetTextForeground(), std::max(1, eye_width / 10)));
		wxDCBrushChanger brush(dc, *wxTRANSPARENT_BRUSH);
		dc.DrawEllipse(cx - eye_width / 2, cy - eye_height / 2, eye_width, eye_height);
		dc.SetBrush(wxBrush(dc.GetTextForeground()));
		dc.DrawCircle(cx, cy, std::max(1, eye_height / 4));
		dc.DrawLine(cx - eye_width / 2, cy - eye_height / 2 - 2,
			cx + eye_width / 2, cy + eye_height / 2 + 2);
	}
};

template<typename T>
T max_value(T AssDialogueBase::*field, EntryList<AssDialogue> const& lines) {
	T value = 0;
	for (AssDialogue const& line : lines) {
		if (line.*field > value)
			value = line.*field;
	}
	return value;
}

struct GridColumnLayer final : GridColumn {
	COLUMN_HEADER(_("L"))
	COLUMN_DESCRIPTION(_("Layer"))
	bool Centered() const override { return true; }

	wxString Value(const AssDialogue *d, const agi::Context *c) const override {
		return d->Layer || !GetSubtitleBinding(c->subtitleAccess->Read(), *d).empty() ? wxString(std::to_wstring(d->Layer)) : wxString();
	}

	void Paint(wxDC &dc, int x, int y, const AssDialogue *d, const agi::Context *c) const override {
		auto group = GetSubtitleBinding(c->subtitleAccess->Read(), *d);
		if (group.empty()) { GridColumn::Paint(dc, x, y, d, c); return; }
		static const wxColour colors[] = {
			{191, 219, 254}, {254, 215, 170}, {187, 247, 208}, {233, 213, 255},
			{254, 202, 202}, {165, 243, 252}, {254, 240, 138}, {251, 207, 232}};
		wxDCPenChanger pen(dc, *wxTRANSPARENT_PEN);
		wxDCBrushChanger brush(dc, wxBrush(colors[SubtitleBindingColorIndex(group)]));
		DrawPill(dc, x, y, width);
		auto foreground = dc.GetTextForeground();
		dc.SetTextForeground(*wxBLACK);
		GridColumn::Paint(dc, x, y, d, c);
		dc.SetTextForeground(foreground);
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		int max_layer = max_value(&AssDialogue::Layer, c->subtitleAccess->Read().Events);
		bool bound = std::any_of(c->subtitleAccess->Read().Events.begin(), c->subtitleAccess->Read().Events.end(), [&](auto const& line) {
			return !GetSubtitleBinding(c->subtitleAccess->Read(), line).empty();
		});
		return max_layer == 0 && !bound ? 0 : helper(std::to_wstring(max_layer));
	}
};

struct GridColumnTime : GridColumn {
	bool by_frame = false;

	bool Centered() const override { return true; }
	void SetByFrame(bool by_frame) override { this->by_frame = by_frame; }
};

struct GridColumnStartTime final : GridColumnTime {
	COLUMN_HEADER(_("Start"))
	COLUMN_DESCRIPTION(_("Start Time"))

	wxString Value(const AssDialogue *d, const agi::Context *c) const override {
		if (by_frame)
			return std::to_wstring(c->videoController->FrameAtTime(d->Start, agi::vfr::START));
		return to_wx(d->Start.GetAssFormatted());
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		if (!by_frame)
			return helper(wxS("0:00:00.00"));
		int frame = c->videoController->FrameAtTime(max_value(&AssDialogue::Start, c->subtitleAccess->Read().Events), agi::vfr::START);
		return helper(std::to_wstring(frame));
	}
};

struct GridColumnEndTime final : GridColumnTime {
	COLUMN_HEADER(_("End"))
	COLUMN_DESCRIPTION(_("End Time"))

	wxString Value(const AssDialogue *d, const agi::Context *c) const override {
		if (by_frame)
			return std::to_wstring(c->videoController->FrameAtTime(d->End, agi::vfr::END));
		return to_wx(d->End.GetAssFormatted());
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		if (!by_frame)
			return helper(wxS("0:00:00.00"));
		int frame = c->videoController->FrameAtTime(max_value(&AssDialogue::End, c->subtitleAccess->Read().Events), agi::vfr::END);
		return helper(std::to_wstring(frame));
	}
};

template<typename T>
int max_width(T AssDialogueBase::*field, EntryList<AssDialogue> const& lines, WidthHelper &helper) {
	int w = 0;
	for (AssDialogue const& line : lines) {
		auto const& v = line.*field;
		if (v.get().empty()) continue;
		int width = helper(v);
		if (width > w)
			w = width;
	}
	return w;
}

struct GridColumnStyle final : GridColumn {
	COLUMN_HEADER(_("Style"))
	COLUMN_DESCRIPTION(_("Style"))
	bool Centered() const override { return false; }

	wxString Value(const AssDialogue *d, const agi::Context *) const override {
		return to_wx(d->Style);
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		return max_width(&AssDialogue::Style, c->subtitleAccess->Read().Events, helper);
	}
};

struct GridColumnEffect final : GridColumn {
	COLUMN_HEADER(_("Effect"))
	COLUMN_DESCRIPTION(_("Effect"))
	bool Centered() const override { return false; }

	wxString Value(const AssDialogue *d, const agi::Context *) const override {
		return to_wx(d->Effect);
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		return max_width(&AssDialogue::Effect, c->subtitleAccess->Read().Events, helper);
	}
};

struct GridColumnActor final : GridColumn {
	COLUMN_HEADER(_("Actor"))
	COLUMN_DESCRIPTION(_("Actor"))
	bool Centered() const override { return false; }

	wxString Value(const AssDialogue *d, const agi::Context *) const override {
		return to_wx(d->Actor);
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		return max_width(&AssDialogue::Actor, c->subtitleAccess->Read().Events, helper);
	}
};

struct GridColumnMargin : GridColumn {
	int index;
	GridColumnMargin(int index) : index(index) { }

	bool Centered() const override { return true; }

	wxString Value(const AssDialogue *d, const agi::Context *) const override {
		return d->Margin[index] ? wxString(std::to_wstring(d->Margin[index])) : wxString();
	}

	int Width(const agi::Context *c, WidthHelper &helper) const override {
		int max = 0;
		for (AssDialogue const& line : c->subtitleAccess->Read().Events) {
			if (line.Margin[index] > max)
				max = line.Margin[index];
		}
		return max == 0 ? 0 : helper(std::to_wstring(max));
	}
};

struct GridColumnMarginLeft final : GridColumnMargin {
	GridColumnMarginLeft() : GridColumnMargin(0) { }
	COLUMN_HEADER(_("Left"))
	COLUMN_DESCRIPTION(_("Left Margin"))
};

struct GridColumnMarginRight final : GridColumnMargin {
	GridColumnMarginRight() : GridColumnMargin(1) { }
	COLUMN_HEADER(_("Right"))
	COLUMN_DESCRIPTION(_("Right Margin"))
};

struct GridColumnMarginVert final : GridColumnMargin {
	GridColumnMarginVert() : GridColumnMargin(2) { }
	COLUMN_HEADER(_("Vert"))
	COLUMN_DESCRIPTION(_("Vertical Margin"))
};

wxColor blend(wxColor fg, wxColor bg, double alpha) {
	return wxColor(
		wxColor::AlphaBlend(fg.Red(), bg.Red(), alpha),
		wxColor::AlphaBlend(fg.Green(), bg.Green(), alpha),
		wxColor::AlphaBlend(fg.Blue(), bg.Blue(), alpha));
}

class GridColumnCPS final : public GridColumn {
	const agi::OptionValue *ignore_whitespace = OPT_GET("Subtitle/Character Counter/Ignore Whitespace");
	const agi::OptionValue *ignore_punctuation = OPT_GET("Subtitle/Character Counter/Ignore Punctuation");
	const agi::OptionValue *cps_warn = OPT_GET("Subtitle/Character Counter/CPS Warning Threshold");
	const agi::OptionValue *cps_error = OPT_GET("Subtitle/Character Counter/CPS Error Threshold");

public:
	COLUMN_HEADER(_("CPS"))
	COLUMN_DESCRIPTION(_("Characters Per Second"))
	bool Centered() const override { return true; }
	bool RefreshOnTextChange() const override { return true; }

	wxString Value(const AssDialogue *, const agi::Context *) const override {
		return wxS("");
	}

	int CPS(const AssDialogue *d) const {
		return CharactersPerSecond(d->Text.get(), d->End - d->Start, ignore_whitespace->GetBool(), ignore_punctuation->GetBool());
	}

	int Width(const agi::Context *, WidthHelper &helper) const override {
		return helper(wxS("999"));
	}

	void Paint(wxDC &dc, int x, int y, const AssDialogue *d, const agi::Context *) const override {
		int cps = CPS(d);
		if (cps < 0 || cps > 100) return;

		wxString str = std::to_wstring(cps);
		wxSize ext = dc.GetTextExtent(str);
		auto tc = dc.GetTextForeground();

		int cps_min = cps_warn->GetInt();
		int cps_max = std::max<int>(cps_min, cps_error->GetInt());
		if (cps > cps_min) {
			double alpha = std::min((double)(cps - cps_min + 1) / (cps_max - cps_min + 1), 1.0);
			dc.SetBrush(wxBrush(blend(theme::Colour("Colour/Subtitle Grid/CPS Error"), dc.GetBrush().GetColour(), alpha)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			DrawPill(dc, x, y, width);
			dc.SetTextForeground(blend(*wxWHITE, tc, alpha));
		}

		dc.DrawText(str, TextLeft(x, ext.GetWidth()), y + 2);
		dc.SetTextForeground(tc);
	}
};

/// The text column. Simplifying tags (the default) draws each {…} as a chip
/// of icons saying what its tags do, with a tooltip spelling it out
/// (docs/subtitle-grid-tags.md); the other modes show the tags in full or
/// leave them out.
class GridColumnText final : public GridColumn {
	const agi::OptionValue *override_mode;

	/// The pieces of the lines' texts drawn lately, by text
	mutable std::unordered_map<std::string, std::vector<tag_summary::Piece>> summaries;
	/// The icons drawn, by name, size and whether dimmed, for the appearance
	/// they were made in
	mutable std::map<std::tuple<std::string, int, bool>, wxBitmap> icons;
	mutable int icons_made_for = -1;

	std::vector<tag_summary::Piece> const& Summary(const AssDialogue *d) const {
		auto const& text = d->Text.get();
		auto it = summaries.find(text);
		if (it != summaries.end()) return it->second;
		// Only those lately drawn: a long script's lines aren't all kept
		if (summaries.size() > 4000) summaries.clear();
		return summaries.emplace(text, tag_summary::Summarize(text)).first->second;
	}

	/// What a piece takes up across the cell: a run's text, a chip
	struct Placed {
		int left, right;
		tag_summary::Piece const *piece;
		wxString text; ///< For a run
	};

	struct Metrics {
		int height; ///< Of a chip: the row's text and a pixel either side
		int icon;   ///< Icons are square
		int pad, gap, bar;
		explicit Metrics(wxDC &dc) {
			height = dc.GetCharHeight() + 2;
			icon = std::max(8, (dc.GetCharHeight() * 4 + 2) / 5);
			pad = std::max(2, icon * 3 / 10);
			gap = std::max(2, icon / 5);
			bar = std::max(5, icon / 2);
		}
	};
	static constexpr size_t max_marks = 3; ///< Icons in a chip; more are "+N"

	int ChipWidth(wxDC &dc, Metrics const& m, tag_summary::Piece const& piece) const {
		if (piece.karaoke) return m.bar;
		size_t n = std::min(piece.marks.size(), max_marks);
		int width = 2 * m.pad + static_cast<int>(n) * m.icon + static_cast<int>(n ? n - 1 : 0) * m.gap;
		if (piece.marks.size() > max_marks)
			width += m.gap + dc.GetTextExtent(fmt_wx("+%d", piece.marks.size() - max_marks)).x;
		return width;
	}

	std::vector<Placed> Lay(wxDC &dc, int x, const AssDialogue *d) const {
		Metrics m(dc);
		std::vector<Placed> placed;
		// Apart from the text either side, as a word is
		const int spacing = std::max(4, m.icon * 2 / 5);
		size_t chars = 0;
		for (auto const& piece : Summary(d)) {
			if (piece.block) {
				int width = ChipWidth(dc, m, piece);
				placed.push_back({x + spacing / 2, x + spacing / 2 + width, &piece, {}});
				x += width + spacing;
				continue;
			}
			wxString text = tag_summary::PlainText(piece);
			// As before: the first 512 characters
			if (chars + text.size() > 512) text = text.Left(512 - std::min<size_t>(chars, 512)) + "...";
			chars += text.size();
			int width = dc.GetTextExtent(text).x;
			placed.push_back({x, x + width, &piece, text});
			x += width;
			if (chars >= 512) break;
		}
		return placed;
	}

	wxBitmap Icon(wxDC &dc, const char *name, int size, bool dimmed) const {
		int appearance = theme::IsDark() ? 1 : 0;
		if (icons_made_for != appearance) {
			icons.clear();
			icons_made_for = appearance;
		}
		auto key = std::make_tuple(std::string(name), size, dimmed);
		auto it = icons.find(key);
		if (it != icons.end()) return it->second;
		wxBitmap bitmap;
		if (auto body = libresrc_modern_icon(name)) {
			double scale = dc.GetContentScaleFactor();
			int pixels = static_cast<int>(size * scale + 0.5);
			bitmap = libresrc_modern_bundle(body, size).GetBitmap(wxSize(pixels, pixels));
			if (bitmap.IsOk() && dimmed) {
				// Switched off: faint, as a style left out is
				wxImage image = bitmap.ConvertToImage();
				if (!image.HasAlpha()) image.InitAlpha();
				unsigned char *alpha = image.GetAlpha();
				for (int i = 0, n = image.GetWidth() * image.GetHeight(); i < n; ++i)
					alpha[i] = static_cast<unsigned char>(alpha[i] * 2 / 5);
				bitmap = wxBitmap(image);
			}
			if (bitmap.IsOk()) bitmap.SetScaleFactor(scale);
		}
		return icons[key] = bitmap;
	}

	/// A colour tag's colour, shaped by which it is: filled for the primary,
	/// crossed for the secondary, a ring for the border, a square offset
	/// for the shadow; a faint edge in the text colour round each, so a
	/// colour close to the row's still shows
	static void DrawColour(wxDC &dc, tag_summary::Mark const& mark, wxRect r, wxColour ink, wxColour back) {
		wxColour colour(mark.colour.r, mark.colour.g, mark.colour.b);
		wxColour edge = theme::Mix(back, ink, 0.4);
		int side = std::max(6, r.width * 4 / 5);
		wxRect sq(r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side);
		int radius = std::max(1, side / 5);
		switch (mark.index) {
			case 3: {
				int ring = std::max(2, side / 5);
				dc.SetPen(wxPen(edge));
				dc.SetBrush(wxBrush(colour));
				dc.DrawRoundedRectangle(sq, radius);
				dc.SetBrush(wxBrush(back));
				dc.DrawRoundedRectangle(sq.Deflate(ring), std::max(1, radius - ring / 2));
				break;
			}
			case 4: {
				int offset = std::max(2, side / 5);
				wxRect shadow(sq.x + offset, sq.y + offset, side - offset, side - offset);
				dc.SetPen(wxPen(edge));
				dc.SetBrush(wxBrush(colour));
				dc.DrawRoundedRectangle(shadow, radius);
				dc.SetPen(wxPen(ink));
				dc.SetBrush(wxBrush(back));
				dc.DrawRoundedRectangle(wxRect(sq.x, sq.y, side - offset, side - offset), radius);
				break;
			}
			default:
				dc.SetPen(wxPen(edge));
				dc.SetBrush(wxBrush(colour));
				dc.DrawRoundedRectangle(sq, radius);
				if (mark.index == 2) {
					dc.SetPen(wxPen(theme::Mix(colour, ink, 0.7), std::max(1, side / 9)));
					dc.DrawLine(sq.GetRight() - 1, sq.y + 1, sq.x + 1, sq.GetBottom() - 1);
				}
		}
	}

	/// \an as a 3×3 grid of dots, the one it names a solid square
	static void DrawAlign(wxDC &dc, int an, wxRect r, wxColour ink, wxColour back) {
		int column = (an - 1) % 3, row = 2 - (an - 1) / 3;
		int step = std::max(3, r.width * 5 / 16);
		int x0 = r.x + r.width / 2 - step, y0 = r.y + r.height / 2 - step;
		int dot = std::max(2, r.width / 8), mark = std::max(4, r.width / 3);
		dc.SetPen(*wxTRANSPARENT_PEN);
		for (int j = 0; j < 3; ++j)
			for (int i = 0; i < 3; ++i) {
				int cx = x0 + i * step, cy = y0 + j * step;
				if (i == column && j == row) {
					dc.SetBrush(wxBrush(ink));
					dc.DrawRoundedRectangle(cx - mark / 2, cy - mark / 2, mark, mark, std::max(1, mark / 4));
				}
				else {
					dc.SetBrush(wxBrush(theme::Mix(back, ink, 0.45)));
					dc.DrawRectangle(cx - dot / 2, cy - dot / 2, dot, dot);
				}
			}
	}

	void DrawChip(wxDC &dc, Placed const& p, int y, Metrics const& m) const {
		wxColour ink = dc.GetTextForeground();
		wxColour back = dc.GetBrush().IsOk() ? dc.GetBrush().GetColour() : theme::Surface();
		auto const& piece = *p.piece;
		if (piece.karaoke) {
			// Between syllables: a thin bar
			int width = std::max(2, m.icon / 7);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(theme::Mix(back, ink, 0.45)));
			dc.DrawRoundedRectangle(p.left + (p.right - p.left - width) / 2, y + 1 + m.height / 5, width, m.height * 3 / 5, width / 2);
			return;
		}
		wxColour chip = theme::Mix(back, ink, 0.08);
		dc.SetPen(wxPen(theme::Mix(back, ink, 0.18)));
		dc.SetBrush(wxBrush(chip));
		dc.DrawRoundedRectangle(p.left, y + 1, p.right - p.left, m.height, std::max(2, m.height / 3));
		int x = p.left + m.pad, top = y + 1 + (m.height - m.icon) / 2;
		for (size_t i = 0; i < std::min(piece.marks.size(), max_marks); ++i) {
			auto const& mark = piece.marks[i];
			wxRect r(x, top, m.icon, m.icon);
			if (mark.kind == tag_summary::Kind::Colour) DrawColour(dc, mark, r, ink, chip);
			else if (mark.kind == tag_summary::Kind::Align) DrawAlign(dc, mark.index, r, ink, chip);
			else if (auto name = tag_summary::IconName(mark.kind)) {
				wxBitmap bitmap = Icon(dc, name, m.icon, mark.off);
				if (bitmap.IsOk()) dc.DrawBitmap(bitmap, x, top, true);
			}
			x += m.icon + m.gap;
		}
		if (piece.marks.size() > max_marks) {
			wxColour old = dc.GetTextForeground();
			dc.SetTextForeground(theme::Mix(chip, ink, 0.6));
			dc.DrawText(fmt_wx("+%d", piece.marks.size() - max_marks), x, y + 1 + (m.height - dc.GetCharHeight()) / 2);
			dc.SetTextForeground(old);
		}
		dc.SetBrush(wxBrush(back));
	}

public:
	GridColumnText()
	: override_mode(OPT_GET("Subtitle/Grid/Hide Overrides"))
	{
	}

	COLUMN_HEADER(_("Text"))
	COLUMN_DESCRIPTION(_("Text"))
	bool Centered() const override { return false; }
	bool CanHide() const override { return false; }
	bool RefreshOnTextChange() const override { return true; }

	wxString Value(const AssDialogue *d, const agi::Context *) const override {
		wxString str;
		int mode = override_mode->GetInt();

		// Show overrides
		if (mode == 0)
			str = to_wx(d->Text);
		// Simplified: what the chips say, in words
		else if (mode == 1) {
			for (auto const& piece : Summary(d)) str += tag_summary::PlainText(piece);
		}
		// Hidden overrides
		else {
			auto const& text = d->Text.get();
			str.reserve(text.size());
			size_t start = 0, pos;
			while ((pos = text.find('{', start)) != std::string::npos) {
				str += to_wx(text.substr(start, pos - start));
				start = text.find('}', pos);
				if (start != std::string::npos) ++start;
			}
			if (start != std::string::npos)
				str += to_wx(text.substr(start));
		}

		// Cap length and set text
		if (str.size() > 512)
			str = str.Left(512) + "...";
		return str;
	}

	void Paint(wxDC &dc, int x, int y, const AssDialogue *d, const agi::Context *c) const override {
		if (override_mode->GetInt() != 1) {
			GridColumn::Paint(dc, x, y, d, c);
			return;
		}
		Metrics m(dc);
		wxColour ink = dc.GetTextForeground();
		wxColour back = dc.GetBrush().IsOk() ? dc.GetBrush().GetColour() : theme::Surface();
		for (auto const& p : Lay(dc, TextLeft(x, 0), d)) {
			if (p.piece->block) DrawChip(dc, p, y, m);
			else if (p.piece->drawing_points >= 0) {
				// A drawing's commands: how many points, quietly
				dc.SetTextForeground(theme::Mix(back, ink, 0.6));
				dc.DrawText(p.text, p.left, y + 2);
				dc.SetTextForeground(ink);
			}
			else dc.DrawText(p.text, p.left, y + 2);
		}
	}

	wxString Tooltip(wxDC &dc, int x, const AssDialogue *d, const agi::Context *) const override {
		if (override_mode->GetInt() != 1) return {};
		for (auto const& p : Lay(dc, cell_padding, d))
			if (p.piece->block && x >= p.left - 1 && x < p.right + 1)
				return tag_summary::Describe(*p.piece);
		return {};
	}

	int Width(const agi::Context *, WidthHelper &) const override {
		return 5000;
	}
};

template<typename T>
std::unique_ptr<GridColumn> make() {
	return std::unique_ptr<GridColumn>(new T);
}

}

std::vector<std::unique_ptr<GridColumn>> GetGridColumns() {
	std::vector<std::unique_ptr<GridColumn>> ret;
	ret.push_back(make<GridColumnLineNumber>());
	ret.push_back(make<GridColumnLayer>());
	ret.push_back(make<GridColumnStartTime>());
	ret.push_back(make<GridColumnEndTime>());
	ret.push_back(make<GridColumnCPS>());
	ret.push_back(make<GridColumnStyle>());
	ret.push_back(make<GridColumnActor>());
	ret.push_back(make<GridColumnEffect>());
	ret.push_back(make<GridColumnMarginLeft>());
	ret.push_back(make<GridColumnMarginRight>());
	ret.push_back(make<GridColumnMarginVert>());
	ret.push_back(make<GridColumnText>());
	return ret;
}
