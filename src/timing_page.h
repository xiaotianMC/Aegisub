// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The style panel's timing page (docs/style-panel.md#时间页): when the
// current line shows and goes, how fast it reads, how far its ends are
// from the scene changes, its lead-in and lead-out, joining the lines next
// to each other and shifting them. Changes go to
// every selected line; the commands and dialogs doing the same are kept.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wx/window.h>

namespace agi { struct Context; }
class AssDialogue;
class wxSizer;

class TimingPage final : public wxWindow {
protected:
	std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);

public:
	TimingPage(wxWindow *parent, agi::Context *c);
	~TimingPage();

	/// Read the current line again now, rather than when idle
	void UpdateNow();

	/// What each part shows, by name ("time.start", "scene.start",
	/// "summary.cps"…), " disabled" after one that can't be used; "cards"
	/// the cards in order; for tests
	std::map<std::string, wxString> Shown() const;
	/// Act as dragging (@p final false) or typing a number into a field
	/// does; a time field takes milliseconds of the video
	void EnterNumber(std::string const& name, double value, bool final);
	/// Act as typing @p text into a field and pressing Enter does
	bool TypeInto(std::string const& name, wxString const& text);
	/// Act as clicking a button does: "time.start_now", "time.start_next",
	/// "scene.snap_start", "scene.both", "lead.add_in", "join.apply",
	/// "shift.later"…
	void Press(std::string const& name);
	/// Act as choosing @p index of a switch does: "shift.unit" (time, frames)
	void Choose(std::string const& name, int index);

	class Readout;

	/// Called when the cards change, for the panel to lay out again
	std::function<void()> on_layout;

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	bool update_pending = false;
	int drag_commit = -1;

	wxSizer *body;
	wxSizer *card_body = nullptr; ///< The card being built
	std::map<std::string, wxWindow*> parts;
	std::vector<std::string> cards;
	/// Show the line's values in the parts, one for each
	std::vector<std::function<void()>> refreshers;
	std::string structure; ///< What the cards are built for

	/// The line read: its id, times and text; how many lines a change goes to
	int line_id = 0;
	int start = 0, end = 0;
	std::string text;
	size_t line_count = 0;
	/// Gaps to the lines before and after it in the file, comments left
	/// out: negative for an overlap; whether there is one
	int gap_before = 0, gap_after = 0;
	bool has_before = false, has_after = false;
	/// The selected lines' times, comments left out, sorted by start
	std::vector<std::pair<int, int>> selected_times;

	/// Change the selected lines' times: @p change is given them and the
	/// line read among them, and says whether it changed anything.
	/// Amends @p amend (-1: a new undo step). @return The commit, or
	/// @p amend if nothing changed.
	using Change = std::function<void(std::vector<AssDialogue*> const& rows, AssDialogue &current)>;
	int Write(wxString const& undo_name, Change const& change, int amend = -1);
	/// As Write, for a drag or a typed value: one undo step for a drag
	void WriteNumber(wxString const& undo_name, Change const& change, bool final);

	void Build();
	void Display();
	void ScheduleUpdate();
	bool HasVideo() const;
	/// @p ms on the start (or @p end) of the nearest frame, unless Shift
	/// is down or there is no video
	int Snap(int ms, bool end) const;
	void Seek(int ms);
	/// Move the selected lines' starts (@p end false) or ends a frame on
	/// (@p frames 1) or back (-1)
	void StepFrame(bool end, int frames);
	/// Move the selected lines' start or end onto the nearest scene change
	/// within the distance, as Snap to Nearby Scene Changes does both
	void SnapEnd(bool end);
	/// Add lead-in, lead-out or both to the selected lines
	void Lead(bool in, bool out);
	/// Join the selected lines next to each other, as the timing
	/// post-processor does
	void Join();
	/// Move the selected lines later (@p direction 1) or earlier (-1) by
	/// the amount Shift Times keeps
	void Shift(int direction);

	template<class T, class... Args> T *Add(std::string const& name, Args&&... args);
	void AddCardTitle(std::string const& card, wxString const& title);
	void AddRow(wxSizer *row, bool first = false);
	void BuildSummary();
	void BuildTimes();
	void BuildScene();
	void BuildLead();
	void BuildJoin();
	void BuildShift();
	/// A time of the line, with the video's time and a frame back and on
	void AddTimeRow(bool end);
};
