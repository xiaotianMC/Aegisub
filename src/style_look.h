// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// What text looks like at a point of a line (docs/style-panel.md): the
// line's style with the override tags before that point applied.
#pragma once

#include <libaegisub/color.h>

#include <functional>
#include <string>

class AssDialogue;
class AssStyle;

struct StyleLook {
	std::string style;        ///< The line's style name
	std::string font;
	double size = 0;
	int weight = 400;         ///< 400 normal, 700 bold; \b also takes other weights
	bool italic = false;
	bool underline = false;
	bool strikeout = false;
	double spacing = 0;       ///< \fsp, in pixels
	int alignment = 2;        ///< \an, 1–9 as on a numeric keypad
	/// Colours with their alpha: 0 opaque, 255 invisible
	agi::Color primary, outline, shadow;
	double border = 0;        ///< \bord: the larger of \xbord and \ybord
	double shadow_x = 0, shadow_y = 0;
	double blur = 0;          ///< \blur
	int border_style = 1;     ///< The style's: 3 draws an opaque box
	double scale_x = 100, scale_y = 100;
	double angle = 0;         ///< \frz
	/// Where the line is: its first \pos, or where \move starts, or
	/// (has_position false) where its alignment and margins put it
	bool has_position = false;
	bool moving = false;      ///< Placed by \move rather than \pos
	double pos_x = 0, pos_y = 0;

	bool Bold() const { return weight >= 700; }
};

/// Finds a style by name, for \r and the line's own style; may return null
using StyleLookup = std::function<AssStyle const*(std::string const&)>;

/// How text in @p style looks without override tags
StyleLook LookOf(AssStyle const& style);

/// How the line's text looks at its plain character @p plain_pos (tags
/// not counted): the tags before that character apply, including any
/// directly in front of it. 0 is the start of the text, after any tags
/// that open the line. Alignment counts from wherever it is in the line,
/// as only its first appearance has any effect.
StyleLook LookAt(AssDialogue const& line, int plain_pos, StyleLookup const& find_style);

/// Where @p look's line goes without \pos, for a script of @p width by
/// @p height: from its alignment and margins (the line's, or else its
/// style's), as the renderers place it. Does nothing when it has a position.
void DefaultPosition(StyleLook &look, AssDialogue const& line, AssStyle const* style, int width, int height);

/// The plain character a byte offset of the line's text falls on: the
/// number of text bytes before it that are not part of a tag block.
int PlainPosition(std::string const& text, int raw_pos);
