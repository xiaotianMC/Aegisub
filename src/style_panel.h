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

#include <wx/scrolwin.h>

namespace agi { struct Context; }
struct StyleLook;
class wxSizer;
class wxTextCtrl;

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
	/// and "scope" for what the panel describes; for tests
	std::map<std::string, wxString> Shown() const;

	/// Act as clicking the button @p name does ("bold", "align_top"…)
	void Press(std::string const& name);
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

	/// Collapse or expand a section: "style", "text" or "appearance"
	void ShowSection(std::string const& name, bool show);
	bool IsSectionShown(std::string const& name) const;

	/// Default, smallest and largest width, in DIP
	static constexpr int default_width = 280;
	static constexpr int min_width = 220;
	static constexpr int max_width = 600;

	class Part;
	class Field;
	class Toggle;
	class ColourRow;
	class Caption;
	class Header;
	class IconButton;

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection theme_changed;
	bool update_pending = false;
	int drag_commit = -1; ///< The undo step a drag amends
	bool box_style = false; ///< The lines' styles draw a box: no outline

	struct Section {
		Header *header;
		wxSizer *body;
	};
	std::map<std::string, Section> sections;
	std::map<std::string, Part*> parts;

	void BindEditing();
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
