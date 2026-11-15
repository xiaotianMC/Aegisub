// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Drawing what tag_summary says of a line's override blocks: each {…} as a
// chip of icons (docs/subtitle-grid-tags.md). Shared by the subtitle grid's
// text column and the comparison page's panes.
#pragma once

#include "tag_summary.h"

#include <map>
#include <string>
#include <tuple>

#include <wx/bitmap.h>
#include <wx/colour.h>

class wxDC;

namespace tag_chips {

/// Sizes of a chip for the font the DC has
struct Metrics {
	int height; ///< Of a chip: the row's text and a pixel either side
	int icon;   ///< Icons are square
	int pad, gap, bar;
	/// Apart from the text either side, as a word is
	int spacing;
	explicit Metrics(wxDC &dc);
};

/// Icons in a chip; more are "+N"
constexpr size_t max_marks = 3;

/// Draws chips, keeping the icons it made for the appearance they were
/// made in
class Painter {
	std::map<std::tuple<std::string, int, bool>, wxBitmap> icons;
	int icons_made_for = -1;

	wxBitmap Icon(wxDC &dc, const char *name, int size, bool dimmed);

public:
	/// How wide a block's chip is (a karaoke block's bar)
	int Width(wxDC &dc, Metrics const& m, tag_summary::Piece const& block) const;
	/// Draw @p block's chip from @p left to @p right, its top a pixel
	/// under @p y, in the DC's text colour over @p back
	void Draw(wxDC &dc, tag_summary::Piece const& block, int left, int right, int y, Metrics const& m, wxColour const& back);
};

}
