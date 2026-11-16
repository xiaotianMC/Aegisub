// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "tag_chips.h"

#include "format.h"
#include "libresrc/libresrc.h"
#include "theme.h"

#include <algorithm>

#include <wx/dc.h>
#include <wx/image.h>

namespace tag_chips {

Metrics::Metrics(wxDC &dc) {
	height = dc.GetCharHeight() + 2;
	icon = std::max(8, (dc.GetCharHeight() * 4 + 2) / 5);
	pad = std::max(2, icon * 3 / 10);
	gap = std::max(2, icon / 5);
	bar = std::max(5, icon / 2);
	spacing = std::max(4, icon * 2 / 5);
}

namespace {

/// A colour tag's colour, shaped by which it is: filled for the primary,
/// crossed for the secondary, a ring for the border, a square offset
/// for the shadow; a faint edge in the text colour round each, so a
/// colour close to the row's still shows
void DrawColour(wxDC &dc, tag_summary::Mark const& mark, wxRect r, wxColour ink, wxColour back) {
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
void DrawAlign(wxDC &dc, int an, wxRect r, wxColour ink, wxColour back) {
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

}

wxBitmap Painter::Icon(wxDC &dc, const char *name, int size, bool dimmed) {
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
		// Pixels to a unit of the DC: where units are pixels, as on
		// Windows, the DC still names the system's scale, which the size
		// (from the DC's font) already has in it
#ifdef wxHAS_DPI_INDEPENDENT_PIXELS
		double scale = dc.GetContentScaleFactor();
#else
		double scale = 1.0;
#endif
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

int Painter::Width(wxDC &dc, Metrics const& m, tag_summary::Piece const& block) const {
	if (block.karaoke) return m.bar;
	size_t n = std::min(block.marks.size(), max_marks);
	int width = 2 * m.pad + static_cast<int>(n) * m.icon + static_cast<int>(n ? n - 1 : 0) * m.gap;
	if (block.marks.size() > max_marks)
		width += m.gap + dc.GetTextExtent(fmt_wx("+%d", block.marks.size() - max_marks)).x;
	return width;
}

void Painter::Draw(wxDC &dc, tag_summary::Piece const& block, int left, int right, int y, Metrics const& m, wxColour const& back) {
	wxColour ink = dc.GetTextForeground();
	if (block.karaoke) {
		// Between syllables: a thin bar
		int width = std::max(2, m.icon / 7);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(theme::Mix(back, ink, 0.45)));
		dc.DrawRoundedRectangle(left + (right - left - width) / 2, y + 1 + m.height / 5, width, m.height * 3 / 5, width / 2);
		return;
	}
	wxColour chip = theme::Mix(back, ink, 0.08);
	dc.SetPen(wxPen(theme::Mix(back, ink, 0.18)));
	dc.SetBrush(wxBrush(chip));
	dc.DrawRoundedRectangle(left, y + 1, right - left, m.height, std::max(2, m.height / 3));
	int x = left + m.pad, top = y + 1 + (m.height - m.icon) / 2;
	for (size_t i = 0; i < std::min(block.marks.size(), max_marks); ++i) {
		auto const& mark = block.marks[i];
		wxRect r(x, top, m.icon, m.icon);
		if (mark.kind == tag_summary::Kind::Colour) DrawColour(dc, mark, r, ink, chip);
		else if (mark.kind == tag_summary::Kind::Align) DrawAlign(dc, mark.index, r, ink, chip);
		else if (auto name = tag_summary::IconName(mark.kind)) {
			wxBitmap bitmap = Icon(dc, name, m.icon, mark.off);
			if (bitmap.IsOk()) dc.DrawBitmap(bitmap, x, top, true);
		}
		x += m.icon + m.gap;
	}
	if (block.marks.size() > max_marks) {
		wxColour old = dc.GetTextForeground();
		dc.SetTextForeground(theme::Mix(chip, ink, 0.6));
		dc.DrawText(fmt_wx("+%d", block.marks.size() - max_marks), x, y + 1 + (m.height - dc.GetCharHeight()) / 2);
		dc.SetTextForeground(old);
	}
	dc.SetBrush(wxBrush(back));
}

}
