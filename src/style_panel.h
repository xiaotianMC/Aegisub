// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The style panel at the right of the main window (docs/style-panel.md):
// what the text being edited looks like — its style, font, size,
// alignment, fill, outline, background and shadow — read from the style
// and the override tags in effect. With text selected in the editor it
// describes that text; otherwise the selected lines as a whole, showing
// "—" where they differ.
#pragma once

#include "style_edit.h"

#include <libaegisub/color.h>
#include <libaegisub/signal.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wx/longlong.h>
#include <wx/scrolwin.h>

namespace agi { struct Context; }
struct StyleLook;
class wxSizer;
class wxTextCtrl;
class AnimationPage;
class ScriptPage;
class TimingPage;

class StylePanel final : public wxScrolledWindow {
protected:
	// Declared first: expires only after all owned bindings are destroyed.
	std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);

public:
	StylePanel(wxWindow *parent, agi::Context *c);
	~StylePanel();

	/// Read the selection again now, rather than when idle
	void UpdateNow();

	/// What each part shows, by name ("style", "font", "size", "fill"…),
	/// and "scope" for what the panel describes; "<name> disabled" for a
	/// part that can't be used, and " (style)" after a margin left to the
	/// style; for tests
	std::map<std::string, wxString> Shown() const;

	/// Act as clicking the button @p name does ("bold", "align_top"…)
	void Press(std::string const& name);
	/// Act as turning the wheel @p steps notches (up positive) over the
	/// list field @p name ("style", "font", "weight") does
	void Wheel(std::string const& name, int steps);
	/// Act as dragging (@p final false) or typing a number into @p name does
	void EnterNumber(std::string const& name, double value, bool final);
	/// Open the box for typing into the number field @p name, as a click does
	void StartEditing(std::string const& name);
	wxTextCtrl *EditBox(std::string const& name);
	/// Use @p font, as choosing it from the font list does
	void ChooseFont(wxString const& font);
	/// Use @p weight and slant, as the weight menu does
	void ChooseWeight(int weight, bool italic);
	/// Tick or untick "fill", "outline", "background" or "shadow"
	void Check(std::string const& row, bool on);
	/// Use @p colour (with its alpha) for a colour row, as the picker does
	void ChooseColour(std::string const& row, agi::Color colour);
	/// Open the colour picker for a colour row, as clicking its colour does
	void PickColour(std::string const& row);
	/// Give the selected lines a style, as the style list does
	void ChooseStyle(std::string const& name);
	/// Make a style from the current line, as + does once it has a name
	bool NewStyle(wxString const& name);

	/// Which page the panel shows: the style's sections, animation
	/// (\move, \fad, \t and \org of the current line) or timing (its
	/// times, scene changes and lead). The switch at the top chooses; the
	/// panel opens on the one last shown (App/Style Panel/Page).
	/// Script, the video's script typed in, isn't on the switch: the button
	/// beside it opens it over the whole panel, and its ‹ goes back
	enum class Page { Style, Animation, Timing, Script };
	void ShowPage(Page page);
	Page ShownPage() const { return page; }
	AnimationPage *Animation() const { return animation; }
	TimingPage *Timing() const { return timing; }
	ScriptPage *Script() const { return script; }
	/// Show the panel, if it is hidden or folded away, on the script page
	/// with the keyboard in its text; from there, back to the page before
	void ToggleScript();
	/// Show the current line's Banner or Scroll on the animation page,
	/// adding a scroll where its Effect field is empty, if the panel is on
	/// the screen. @return Whether it is
	bool ShowScrollEffect();

	class Part;
	/// The parts Tab goes through, in order: those shown and enabled that
	/// take the keyboard
	std::vector<Part*> KeyboardParts() const;
	/// Give the keyboard to the part after @p from (a part, or the script's
	/// text), or before it; round from the last to the first
	void MoveFocus(wxWindow *from, bool forward);
	/// Whether the keyboard is in the panel
	bool HasKeyboard() const;
	/// Show the panel if it is hidden or folded away and give the keyboard
	/// to its first part, the switch between the pages. @return Whether
	/// it has it
	bool TakeKeyboard();

	/// Collapse or expand a section: "style", "text" or "appearance"
	void ShowSection(std::string const& name, bool show);
	bool IsSectionShown(std::string const& name) const;

	/// Called by the button folding the panel away
	std::function<void()> on_collapse;
	/// Called to show the panel when it is folded away
	std::function<void()> request_open;

	/// Show the panel and its Shape section, for the active line's shape
	void EditShape();
	/// Whether a section is there at all: Shape only for a shape line
	bool IsSectionVisible(std::string const& name) const;

	/// Default, smallest and largest width, in DIP
	static constexpr int default_width = 280;
	static constexpr int min_width = 220;
	static constexpr int max_width = 600;

	class Field;
	class Toggle;
	class ColourRow;
	class Caption;
	class Header;
	class IconButton;
	class PageSwitch;
	class TextButton;
	class CardTitle;

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection theme_changed;
	bool update_pending = false;
	int drag_commit = -1; ///< The undo step a drag amends
	int wheel_commit = -1; ///< The undo step wheel turns close together amend
	wxLongLong last_wheel = 0;
	bool box_style = false; ///< The lines' styles draw a box: no outline
	/// The shape's width over its height, kept while its proportions are
	/// locked; taken again when the shape changes other than by its size
	double shape_aspect = 0;
	bool resizing_shape = false;

	struct Section {
		Header *header;
		wxSizer *body;
		bool visible = true;
	};
	std::map<std::string, Section> sections;
	Page page = Page::Style;
	wxSizer *animation_body = nullptr;
	AnimationPage *animation = nullptr;
	wxSizer *timing_body = nullptr;
	TimingPage *timing = nullptr;
	wxSizer *top_line = nullptr; ///< The switch, the script button and »
	ScriptPage *script = nullptr;
	Page before_script = Page::Style; ///< Where the script page's ‹ goes
	/// Show or hide a section's title and body for its state and the page
	void ApplySection(Section const& section);
	std::map<std::string, Part*> parts;

	void BindEditing();
	void SetSectionVisible(std::string const& name, bool visible);
	void ShowWeightMenu();
	void ShowStyleMenu();
	void AskNewStyle();
	void OpenStyleEditor();
	/// Writes one property's change: through Apply, or the picker's interaction
	using Writer = std::function<void(style_edit::Property, std::function<void(StyleLook&)>)>;
	void SetColour(std::string const& row, agi::Color colour, bool rgb, bool alpha, Writer const& write);
	void ScheduleUpdate();
	void Display(std::vector<StyleLook> const& looks, wxString const& scope);
	Section &AddSection(wxSizer *sizer, std::string const& name, wxString const& title);
	template<class T, class... Args> T *Add(std::string const& name, Args&&... args);
};

/// The strip left of the style panel (docs/style-panel.md): the gap
/// between it and the editing area, which sets its width when dragged.
/// It also shows and hides the panel: as "App/Style Panel/Show" says, and
/// folded away while the window is narrower than narrow_width, leaving a
/// wider strip with an arrow and the pages' names down it: a name opens
/// the panel on its page, anywhere else on the page last shown.
class StylePanelEdge final : public wxWindow {
	StylePanel *panel;
	agi::signal::Connection option_changed, theme_changed;
	bool opened = false; ///< Opened by hand while the window is narrow
	bool hover = false;
	int hover_page = -1; ///< The page whose name is under the pointer
	int start_x = 0, start_width = 0;

	void SetWidth(int width);
	/// The pages' names down the folded strip, and where each is
	std::vector<std::pair<wxString, wxRect>> PageNames() const;

public:
	/// Below this window width, in DIP, the panel folds away
	static constexpr int narrow_width = 1100;

	StylePanelEdge(wxWindow *parent, StylePanel *panel);

	/// Show or hide the panel for the parent's width and the option
	void Update();
	/// Whether the panel is hidden and the strip opens it
	bool Folded() const;
	/// Act as clicking the folded strip does
	void Open();
	/// Act as clicking a page's name on the folded strip does: open the
	/// panel on that page
	void Open(StylePanel::Page page);
	/// The page whose name is at @p point of the folded strip, -1 for none
	int PageAt(wxPoint point) const;
	/// Fold the panel away, as its » button does
	void Collapse();
};
