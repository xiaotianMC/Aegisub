// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The style panel at the right of the main window (docs/style-panel.md):
// what the text being edited looks like — its style, font, size,
// alignment, fill, outline, background and shadow — read from the style
// and the override tags in effect. With text selected in the editor it
// describes that text; otherwise the selected lines as a whole, showing
// "—" where they differ.
#pragma once

#include <libaegisub/signal.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wx/scrolwin.h>

namespace agi { struct Context; }
struct StyleLook;
class wxSizer;

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

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection theme_changed;
	bool update_pending = false;

	struct Section {
		Header *header;
		wxSizer *body;
	};
	std::map<std::string, Section> sections;
	std::map<std::string, Part*> parts;

	void ScheduleUpdate();
	void Display(std::vector<StyleLook> const& looks, wxString const& scope);
	Section &AddSection(wxSizer *sizer, std::string const& name, wxString const& title);
	template<class T, class... Args> T *Add(std::string const& name, Args&&... args);
};
