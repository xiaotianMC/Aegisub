// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The style panel's animation page (docs/style-panel.md): what makes the
// current line move and change over time — \move, \fad and \fade, \t and
// \org — as a card each, with its times on a bar over the line and as
// times of the video. Changes go to every selected line.
#pragma once

#include "animation_tags.h"

#include "style_look.h"

#include <libaegisub/signal.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/window.h>

namespace agi { struct Context; }
class AssDialogue;
class wxSizer;

class AnimationPage final : public wxWindow {
protected:
	std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);

public:
	/// What can be added: a card each, Transform as many times as wanted
	enum class Effect { Move, Fade, Transform, Origin };

	AnimationPage(wxWindow *parent, agi::Context *c);
	~AnimationPage();

	/// Read the current line again now, rather than when idle
	void UpdateNow();

	/// What each part shows, by name: "line" for the line it reads, "cards"
	/// the cards in order ("move fade t0 origin"), and each field, as
	/// "move.x1", "move.in", "move.in%", "fade.in_end", "t0.accel",
	/// "t0.target0"; " disabled" after one that can't be used; for tests
	std::map<std::string, wxString> Shown() const;

	/// Add an effect to the selected lines, as the add menu does; a
	/// transform changes @p target, to what the line has now
	void AddEffect(Effect effect, std::string const& target = "\\fs");
	/// Take a card's tags away, as its ✕ does: "move", "fade", "t0"…
	void RemoveCard(std::string const& card);
	/// Act as dragging (@p final false) or typing a number into a field
	/// does; a time field takes milliseconds of the video
	void EnterNumber(std::string const& name, double value, bool final);
	/// Act as typing @p text into a field and pressing Enter does
	bool TypeInto(std::string const& name, wxString const& text);
	/// Act as clicking a button does: "move.in_now", "move.whole",
	/// "move.first_half", "move.second_half", "move.pick", "t0.add"…
	void Press(std::string const& name);
	/// Act as dragging point @p index of a card's bar to @p ms after the
	/// line's start does
	void DragPoint(std::string const& card, int index, int ms, bool final);
	/// Add @p name ("\fs", "\1c"…) to what transform @p index changes, as
	/// its + menu does
	void AddTarget(int index, std::string const& name);

	/// The tags a \t can change from the + menu: name, label and unit
	struct TargetKind {
		const char *name;
		wxString label, unit;
		enum class Value { Number, Opacity, Colour } value;
		double min, max, step;
	};
	static std::vector<TargetKind> const& TargetKinds();

	class Timeline;
	class TextButton;
	class CardTitle;
	class ColourTarget;

	/// Called when the cards change, for the panel to lay out again
	std::function<void()> on_layout;

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection seek_connection;
	bool update_pending = false;
	int drag_commit = -1;

	wxSizer *body;
	wxSizer *card_body = nullptr; ///< The card being built
	std::map<std::string, wxWindow*> parts;
	std::vector<std::string> cards;
	/// Show the line's values in the parts, one for each
	std::vector<std::function<void()>> refreshers;
	std::string structure; ///< What the cards are built for

	/// The line read, its time and what it does
	AssDialogue const* line = nullptr;
	int start = 0, duration = 0;
	animation::Animation current;
	/// How many lines a change goes to
	size_t line_count = 0;

	using Change = std::function<std::string(AssDialogue const&, animation::Animation const&, int duration)>;
	/// Rewrite the selected lines' text with @p change, amending @p amend
	/// (-1: a new undo step). @return The commit, or -1 if nothing changed.
	int Write(wxString const& undo_name, Change const& change, int amend = -1);
	/// As Write, for a drag or a typed value: one undo step for a drag
	void WriteNumber(wxString const& undo_name, Change const& change, bool final);

	void Build();
	void Display();
	void ScheduleUpdate();
	void ShowAddMenu();
	void ShowTargetMenu(int index);
	void PickColour(size_t index, std::string const& tag);
	/// How @p row looks at its start, with where it stands
	StyleLook LookOfLine(AssDialogue const& row) const;

	/// The video's time snapped to its frame, when one is open
	int Snap(int ms) const;
	/// The video's current time, if there is a video
	std::optional<int> VideoTime() const;
	void Seek(int ms);

	template<class T, class... Args> T *Add(std::string const& name, Args&&... args);
	void BuildMove();
	void BuildFade();
	void BuildTransform(size_t index);
	void BuildOrigin();
	/// A card's title with its ✕, and its body for what follows
	void AddCardTitle(std::string const& card, wxString const& title, wxString const& tag);
	void AddRow(wxSizer *row, bool first = false);
	/// A small title in the card
	wxWindow *AddSubtitle(std::string const& name, wxString const& text);
	/// Two coordinates, with a button for the video tool dragging them
	void AddPoint(std::string const& card, std::string const& x, std::string const& y, wxString const& label,
		wxString const& undo_name, const char *tool, wxString const& pick_tip, bool first);
	/// The bar over the line and the times of a range (\move, \t), with
	/// buttons for the whole line and its halves
	void AddRange(std::string const& card, wxString const& undo_name);
	/// A time as one of the video's, the button taking the video's, and
	/// the same time as a share of the line
	void AddTimeRow(std::string const& name, wxString const& label, wxString const& undo_name,
		std::function<int(animation::Animation const&, int)> get,
		std::function<bool(animation::Animation&, int, int)> set);
};
