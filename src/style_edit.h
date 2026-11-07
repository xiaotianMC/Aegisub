// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Changes made in the style panel (docs/style-panel.md): one property of the
// selected text, or of the selected lines as a whole, written as override
// tags.
#pragma once

#include "ass_dialogue.h"

#include <functional>
#include <string>
#include <vector>

#include <wx/string.h>

namespace agi { struct Context; }
class AssStyle;
struct StyleLook;

namespace style_edit {
/// What the panel can change. Each is set by its own override tags.
enum class Property {
	Font, Size, Weight, Italic, Underline, Strikeout, Spacing, Alignment,
	FillColour, FillAlpha, OutlineColour, OutlineAlpha, Border,
	ShadowColour, ShadowAlpha, Shadow, Blur,
	ScaleX, ScaleY, Rotation,
	Position,  ///< \pos; lines placed by \move are left alone
	Opacity,   ///< \alpha: fill, outline and shadow together
};

/// The tags that set @p property, e.g. \bord, \xbord and \ybord
std::vector<std::string> const& TagNames(Property property);
/// Whether @p property is one for the whole line (alignment)
bool LineWide(Property property);
/// The tags that give @p look's value of @p property, e.g. "\fs48"
std::string TagFor(Property property, StyleLook const& look);

/// Remove the tags @p names everywhere in @p text, outside \t, then put
/// @p tag at the start of the line unless it is empty
std::string SetForLine(std::string const& text, std::vector<std::string> const& names, std::string const& tag);

struct RangeResult {
	std::string text;
	int start, end; ///< The same text selected in the result
};
/// Set @p tag for the text between the byte offsets @p start and @p end,
/// removing @p names inside it; @p restore goes after it, to keep the rest
/// of the line as it was (empty: nothing follows, or nothing to restore)
RangeResult SetForRange(std::string const& text, int start, int end,
	std::vector<std::string> const& names, std::string const& tag, std::string const& restore);

/// Change @p property of the selection in @p c: the selected text when
/// there is some in one line, otherwise every selected line from its start.
/// @p change is given the look before and makes the new one; a line whose
/// result is its style's value keeps no tag for it. @p amend_id is the
/// commit to amend (-1: a new undo step).
/// @return The commit's id, or -1 when nothing was written
int Apply(agi::Context *c, Property property, std::function<void(StyleLook&)> const& change, int amend_id = -1);

/// As Apply, with @p change also given how the line's style looks
int ApplyWithStyle(agi::Context *c, Property property,
	std::function<void(StyleLook&, StyleLook const&)> const& change, int amend_id = -1);

/// The rows Apply would write, without writing them: for a preview
/// written through an interaction (the colour picker)
struct Staged {
	std::vector<AssDialogueBase> rows; ///< The selected rows, changed
	std::vector<int> ids;              ///< Their ids
	bool changed = false;              ///< Whether any row changed
	wxString name;                     ///< For the undo step
	int single_line_id = 0;            ///< For the commit
	bool select = false;               ///< Whether the editor's selection moves
	int start = 0, end = 0;            ///< To there
};
Staged Stage(agi::Context const* c, Property property, std::function<void(StyleLook&, StyleLook const&)> const& change);
/// Move the editor's selection as @p staged says, after writing it
void Select(agi::Context *c, Staged const& staged);

/// The properties a style holds
std::vector<Property> const& StyleProperties();
/// Whether a style can hold @p look's value of @p property: not a weight
/// other than normal and bold, nor different horizontal and vertical shadows
bool Storable(Property property, StyleLook const& look);
/// Give @p style @p look's value of @p property
void Store(Property property, StyleLook const& look, AssStyle &style);

/// Give the selected lines the style @p name. @return As Apply
int SetLineStyle(agi::Context *c, std::string const& name);
/// Make the current line's style look as the line does at its start, and
/// take out of the selected lines of that style the tags it now makes
/// unneeded. @return As Apply
int UpdateStyleFromLine(agi::Context *c);
/// Take out of the selected lines every tag the panel sets, so that they
/// look as their style does. @return As Apply
int ResetToStyle(agi::Context *c);
/// A copy of the current line's style named @p name that looks as the line
/// does at its start, given to the selected lines. @return As Apply; -1
/// also when a style of that name exists
int NewStyleFromLine(agi::Context *c, std::string const& name);

/// Give the selected lines a box behind the text (@p on), or take it
/// away. Only a style has one (border style 3), so each line gets a copy
/// of its style that differs only in that — "Name Box", or back to "Name"
/// — reusing one that exists; the style and its other lines stay as they
/// are. @return As Apply
int SetBackground(agi::Context *c, bool on);
}
