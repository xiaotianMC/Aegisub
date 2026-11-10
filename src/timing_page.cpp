// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "timing_page.h"

#include "ass_dialogue.h"
#include "async_video_provider.h"
#include "ass_file.h"
#include "format.h"
#include "keyframe_snap.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "style_panel_parts.h"
#include "subtitle_project_access.h"
#include "subtitle_row_edit.h"
#include "timing_adjust.h"
#include "video_controller.h"
#include "command/command.h"
#include "include/aegisub/context.h"

#include <libaegisub/vfr.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <wx/sizer.h>
#include <wx/utils.h>

using Field = StylePanel::Field;
using IconButton = StylePanel::IconButton;
using Caption = StylePanel::Caption;
using TextButton = StylePanel::TextButton;
using CardTitle = StylePanel::CardTitle;
using PageSwitch = StylePanel::PageSwitch;
using style_parts::TimeText;
using style_parts::ReadTime;

/// A line of text, dim, or in the subtitle grid's CPS colours when it
/// warns: how fast the line reads
class TimingPage::Readout final : public StylePanel::Part {
	wxString text;
	int level = 0; ///< 0 plain, 1 over the warning threshold, 2 the error one

	void Draw(wxDC &dc, Palette const& p) override {
		wxColour error = theme::Colour("Colour/Subtitle Grid/CPS Error");
		dc.SetTextForeground(level == 2 ? error : level == 1 ? theme::Mix(p.dim, error, 0.55) : p.dim);
		if (level) {
			wxFont bold = GetFont();
			bold.MakeBold();
			dc.SetFont(bold);
		}
		dc.DrawText(Ellipsize(dc, text, GetClientSize().x), 0, (GetClientSize().y - dc.GetCharHeight()) / 2);
	}

public:
	explicit Readout(wxWindow *parent) : Part(parent, wxSize(40, 20)) { }
	void Set(wxString const& new_text, int new_level) {
		if (new_level != level) { level = new_level; Refresh(false); }
		Part::Set(text, new_text);
	}
	wxString Shown() const override { return text + (level == 2 ? " error" : level == 1 ? " warning" : ""); }
};

namespace {
const int max_time = 10 * 3600 * 1000 - 10;

/// "%d ms after the previous line" and the like: the gap to a neighbour
wxString GapText(bool has, int gap, bool before) {
	if (!has) return before ? _("No line before it") : _("No line after it");
	if (gap == 0) return before ? _("Starts as the line before ends") : _("Ends as the line after starts");
	if (gap > 0) return before ? fmt_tl("%d ms after the line before", gap) : fmt_tl("%d ms before the line after", gap);
	return before ? fmt_tl("Overlaps the line before by %d ms", -gap) : fmt_tl("Overlaps the line after by %d ms", -gap);
}

/// Where a line's start or end stands to the nearest scene change
wxString CutText(bool end, bool within, int frames, int distance) {
	if (!within)
		return end ? fmt_tl("End: none within %d ms", distance) : fmt_tl("Start: none within %d ms", distance);
	if (frames == 0) return end ? _("End on a scene change") : _("Start on a scene change");
	int n = std::abs(frames);
	// Before the cut: early; after it: late
	if (frames > 0)
		return end ? fmt_plural(n, "End %d frame early", "End %d frames early", n)
			: fmt_plural(n, "Start %d frame early", "Start %d frames early", n);
	return end ? fmt_plural(n, "End %d frame late", "End %d frames late", n)
		: fmt_plural(n, "Start %d frame late", "Start %d frames late", n);
}
}

template<class T, class... Args>
T *TimingPage::Add(std::string const& name, Args&&... args) {
	auto part = new T(this, std::forward<Args>(args)...);
	parts[name] = part;
	return part;
}

TimingPage::TimingPage(wxWindow *parent, agi::Context *c)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
, c(c)
{
	theme::UseSurface(this);
	body = new wxBoxSizer(wxVERTICAL);
	SetSizer(body);

	auto schedule = [this](SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); };
	connections.push_back(c->subtitleAccess->Read().AddCommitListener("timing_page.AddCommitListener",
		[this](int, AssDialogue const*, SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); },
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->selectionController->AddSelectionListener("timing_page.AddSelectionListener",
		schedule, std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->selectionController->AddActiveLineListener("timing_page.AddActiveLineListener",
		schedule, std::weak_ptr<void const>(model_observer_lifetime)));
	if (c->project) {
		connections.push_back(c->project->AddKeyframesListener([this](std::vector<int> const&) { ScheduleUpdate(); }));
		connections.push_back(c->project->AddVideoProviderListener([this](AsyncVideoProvider *) { ScheduleUpdate(); }));
		connections.push_back(c->project->AddTimecodesListener([this](agi::vfr::Framerate const&) { ScheduleUpdate(); }));
	}
	// Set here or in Preferences and the timing post-processor alike
	for (const char *option : {"Timing/Scene Snap Distance", "Tool/Timing Post Processor/Lead/IN", "Tool/Timing Post Processor/Lead/OUT",
		"Subtitle/Character Counter/CPS Warning Threshold", "Subtitle/Character Counter/CPS Error Threshold",
		"Subtitle/Character Counter/Ignore Whitespace", "Subtitle/Character Counter/Ignore Punctuation",
		"Tool/Timing Post Processor/Threshold/Adjacent Gap", "Tool/Timing Post Processor/Threshold/Adjacent Overlap",
		"Tool/Timing Post Processor/Adjacent Bias", "Tool/Shift Times/Time", "Tool/Shift Times/Frames", "Tool/Shift Times/ByTime"})
		connections.push_back(OPT_SUB(option, [this](agi::OptionValue const&) { ScheduleUpdate(); }));
	UpdateNow();
}

TimingPage::~TimingPage() = default;

void TimingPage::ScheduleUpdate() {
	if (update_pending) return;
	update_pending = true;
	CallAfter([this] {
		update_pending = false;
		UpdateNow();
	});
}

bool TimingPage::HasVideo() const {
	return c->project && c->project->VideoProvider() && c->videoController;
}

int TimingPage::Snap(int ms, bool at_end) const {
	if (!HasVideo() || wxGetKeyState(WXK_SHIFT)) return ms;
	auto vc = c->videoController.get();
	auto const type = at_end ? agi::vfr::END : agi::vfr::START;
	int const frame = vc->FrameAtTime(ms, type);
	int best = vc->TimeAtFrame(frame, type);
	for (int f : {frame - 1, frame + 1}) {
		if (f < 0) continue;
		int t = vc->TimeAtFrame(f, type);
		if (std::abs(t - ms) < std::abs(best - ms)) best = t;
	}
	return best;
}

void TimingPage::Seek(int ms) {
	if (HasVideo()) c->videoController->JumpToTime(ms);
}

int TimingPage::Write(wxString const& undo_name, Change const& change, int amend) {
	auto scope = c->subtitleAccess->TryWrite();
	if (!scope) return -1;
	SubtitleRowEdit staged(c);
	if (staged.rows.empty()) return amend;
	auto current = std::find_if(staged.rows.begin(), staged.rows.end(), [&](AssDialogue *row) { return row->Id == line_id; });
	std::vector<std::pair<int, int>> before;
	for (auto row : staged.rows) before.emplace_back(row->Start, row->End);
	change(staged.rows, current != staged.rows.end() ? **current : *staged.rows.front());
	bool changed = false;
	for (size_t i = 0; i < staged.rows.size(); ++i)
		changed |= before[i] != std::pair<int, int>(staged.rows[i]->Start, staged.rows[i]->End);
	if (!changed) return amend;
	auto edit = c->subtitleAccess->Edit(scope->Lease());
	edit.UpdateRows(staged.Values());
	return edit.Commit(undo_name, AssFile::COMMIT_DIAG_TIME, amend, staged.rows.size() == 1 ? staged.rows.front()->Id : 0);
}

void TimingPage::WriteNumber(wxString const& undo_name, Change const& change, bool final) {
	drag_commit = Write(undo_name, change, drag_commit);
	if (final) drag_commit = -1;
}

void TimingPage::StepFrame(bool at_end, int frames) {
	if (!HasVideo()) return;
	auto vc = c->videoController.get();
	Write(at_end ? _("move end by a frame") : _("move start by a frame"), [=](std::vector<AssDialogue*> const& rows, AssDialogue &) {
		for (auto row : rows) {
			if (at_end) {
				int f = vc->FrameAtTime(row->End, agi::vfr::END) + frames;
				row->End = std::max<int>(vc->TimeAtFrame(std::max(0, f), agi::vfr::END), row->Start);
			}
			else {
				int f = vc->FrameAtTime(row->Start, agi::vfr::START) + frames;
				row->Start = std::min<int>(vc->TimeAtFrame(std::max(0, f), agi::vfr::START), row->End);
			}
		}
	});
	UpdateNow();
	if (at_end) c->videoController->JumpToTime(end, agi::vfr::END);
	else Seek(start);
}

void TimingPage::SnapEnd(bool at_end) {
	if (c->project->Keyframes().empty() || !c->project->Timecodes().IsLoaded()) return;
	int const d = OPT_GET("Timing/Scene Snap Distance")->GetInt();
	auto provider = c->project->VideoProvider();
	int const frames = provider ? provider->GetFrameCount() : 0;
	KeyframeSnapThresholds limits;
	if (at_end) limits.before_end = limits.after_end = d;
	else limits.before_start = limits.after_start = d;
	Write(_("snap to scene changes"), [&](std::vector<AssDialogue*> const& rows, AssDialogue &) {
		SnapToKeyframes({rows.begin(), rows.end()}, c->project->Keyframes(), frames, c->project->Timecodes(), limits);
	});
}

void TimingPage::Lead(bool in, bool out) {
	int const lead_in = in ? OPT_GET("Tool/Timing Post Processor/Lead/IN")->GetInt() : 0;
	int const lead_out = out ? OPT_GET("Tool/Timing Post Processor/Lead/OUT")->GetInt() : 0;
	if (!lead_in && !lead_out) return;
	auto scope = c->subtitleAccess->TryWrite();
	if (!scope) return;
	// Every line bounds it, as the timing post-processor's would; only the
	// selected ones move
	SubtitleRowEdit staged(c, true);
	std::vector<AssDialogue*> targets, bounds;
	for (auto row : staged.rows) {
		if (row->Comment) continue;
		bounds.push_back(row);
		if (staged.selected.contains(row->Id)) targets.push_back(row);
	}
	auto by_start = [](AssDialogue const* a, AssDialogue const* b) { return a->Start < b->Start; };
	std::stable_sort(targets.begin(), targets.end(), by_start);
	std::stable_sort(bounds.begin(), bounds.end(), by_start);
	std::vector<std::pair<int, int>> before;
	for (auto row : targets) before.emplace_back(row->Start, row->End);
	AddLead(targets, bounds, lead_in, lead_out);
	std::vector<AssDialogueBase> updates;
	for (size_t i = 0; i < targets.size(); ++i)
		if (before[i] != std::pair<int, int>(targets[i]->Start, targets[i]->End)) updates.push_back(*targets[i]);
	if (updates.empty()) return;
	auto edit = c->subtitleAccess->Edit(scope->Lease());
	edit.UpdateRows(updates);
	edit.Commit(in && out ? _("add lead in and out") : in ? _("add lead in") : _("add lead out"), AssFile::COMMIT_DIAG_TIME);
}

void TimingPage::Join() {
	int const gap = OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->GetInt();
	int const overlap = OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Overlap")->GetInt();
	double const bias = OPT_GET("Tool/Timing Post Processor/Adjacent Bias")->GetDouble();
	Write(_("join adjacent lines"), [=](std::vector<AssDialogue*> const& rows, AssDialogue &) {
		// As the timing post-processor has them: comments left out, in
		// the order they show
		std::vector<AssDialogue*> sorted;
		for (auto row : rows)
			if (!row->Comment) sorted.push_back(row);
		std::stable_sort(sorted.begin(), sorted.end(), [](AssDialogue const* a, AssDialogue const* b) { return a->Start < b->Start; });
		JoinAdjacent(sorted, gap, overlap, bias);
	});
}

void TimingPage::Shift(int direction) {
	auto const& fps = c->project->Timecodes();
	bool const by_time = OPT_GET("Tool/Shift Times/ByTime")->GetBool() || !fps.IsLoaded();
	int const amount = by_time ? OPT_GET("Tool/Shift Times/Time")->GetInt() : OPT_GET("Tool/Shift Times/Frames")->GetInt();
	if (!amount) return;
	// Shift Times opens the way it last went
	OPT_SET("Tool/Shift Times/Direction")->SetBool(direction < 0);
	int const shift = direction * amount;
	Write(_("shifting"), [&](std::vector<AssDialogue*> const& rows, AssDialogue &) {
		for (auto row : rows) {
			row->Start = ShiftTime(row->Start, shift, by_time, fps, agi::vfr::START);
			row->End = ShiftTime(row->End, shift, by_time, fps, agi::vfr::END);
		}
	});
}

void TimingPage::AddCardTitle(std::string const& card, wxString const& title) {
	cards.push_back(card);
	body->Add(Add<CardTitle>(card + ".title", title, wxString()), 0, wxEXPAND | wxTOP, FromDIP(4));
	card_body = new wxBoxSizer(wxVERTICAL);
	body->Add(card_body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
}

void TimingPage::AddRow(wxSizer *row, bool first) {
	card_body->Add(row, 0, wxEXPAND | (first ? 0 : wxTOP), FromDIP(3));
}

void TimingPage::BuildSummary() {
	const int pad = FromDIP(12);
	auto scope = Add<Caption>("scope");
	body->Add(scope, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	auto times = Add<Caption>("line");
	body->Add(times, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	auto cps = Add<Readout>("summary.cps");
	cps->SetToolTip(_("Characters per second, counted as the subtitle grid's CPS column does, with its thresholds (Preferences > Interface > Character Counter)"));
	body->Add(cps, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	auto prev = Add<Caption>("summary.prev");
	body->Add(prev, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	auto next = Add<Caption>("summary.next");
	body->Add(next, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
	refreshers.push_back([this, scope, times, cps, prev, next] {
		bool known = line_id != 0;
		scope->SetText(!known ? _("No line selected")
			: line_count > 1 ? fmt_plural(line_count, "%d selected line, shown as the current one is", "%d selected lines, shown as the current one is", line_count)
			: _("The current line"));
		times->SetText(known ? TimeText(start) + wxS(" → ") + TimeText(end) : wxString());
		int rate = known ? CharactersPerSecond(text, end - start, OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool(),
			OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool()) : -1;
		int warn = OPT_GET("Subtitle/Character Counter/CPS Warning Threshold")->GetInt();
		int error = std::max<int>(warn, OPT_GET("Subtitle/Character Counter/CPS Error Threshold")->GetInt());
		if (rate < 0) cps->Set(known ? _("Too short to count characters per second") : wxString(), 0);
		else cps->Set(fmt_tl("%d characters per second", rate), rate >= error && rate > warn ? 2 : rate > warn ? 1 : 0);
		prev->SetText(known ? GapText(has_before, gap_before, true) : wxString());
		next->SetText(known ? GapText(has_after, gap_after, false) : wxString());
	});
}

void TimingPage::AddTimeRow(bool at_end) {
	std::string const name = at_end ? "time.end" : "time.start";
	auto row = new wxBoxSizer(wxHORIZONTAL);
	auto time = Add<Field>(name, at_end ? wxGETTEXT_IN_CONTEXT("time", "End") : wxGETTEXT_IN_CONTEXT("time", "Start"), "", false,
		_("A time of the video, h:mm:ss.cc: drag sideways or click and type. Snapped to the video's frames unless Shift is held. Each selected line moves as much as the current one"));
	time->SetFormat(TimeText, ReadTime);
	time->SetNumberHandler({0, max_time, 10}, [this, at_end](double v, bool final) {
		int const target = Snap(static_cast<int>(std::lround(v)), at_end);
		WriteNumber(at_end ? _("set end") : _("set start"), [=](std::vector<AssDialogue*> const& rows, AssDialogue &current) {
			int const delta = target - (at_end ? static_cast<int>(current.End) : static_cast<int>(current.Start));
			for (auto row : rows) {
				if (at_end) row->End = std::max(static_cast<int>(row->End) + delta, static_cast<int>(row->Start));
				else row->Start = std::min(std::max(0, static_cast<int>(row->Start) + delta), static_cast<int>(row->End));
			}
		}, final);
		if (at_end && HasVideo()) c->videoController->JumpToTime(target, agi::vfr::END);
		else if (!at_end) Seek(target);
	});
	row->Add(time, 1, wxEXPAND);

	auto now = Add<IconButton>(name + "_now", IconButton::Glyph::Now,
		at_end ? _("End at the video's current frame") : _("Start at the video's current frame"));
	now->on_click = [this, at_end] { cmd::call(at_end ? "time/snap/end_video" : "time/snap/start_video", c); };
	row->Add(now, 0, wxLEFT, FromDIP(2));
	now->SetMinSize(FromDIP(wxSize(24, 28)));
	auto back = Add<IconButton>(name + "_prev", IconButton::Glyph::Earlier, _("A frame earlier"));
	back->on_click = [this, at_end] { StepFrame(at_end, -1); };
	back->SetMinSize(FromDIP(wxSize(20, 28)));
	row->Add(back, 0, wxEXPAND);
	auto on = Add<IconButton>(name + "_next", IconButton::Glyph::Later, _("A frame later"));
	on->on_click = [this, at_end] { StepFrame(at_end, 1); };
	on->SetMinSize(FromDIP(wxSize(20, 28)));
	row->Add(on, 0, wxEXPAND);
	AddRow(row, !at_end);

	refreshers.push_back([this, at_end, time, now, back, on] {
		bool known = line_id != 0;
		time->SetNumber(at_end ? end : start, known);
		time->Enable(known);
		bool video = known && HasVideo();
		now->Enable(video);
		back->Enable(video);
		on->Enable(video);
	});
}

void TimingPage::BuildTimes() {
	AddCardTitle("time", _("Times"));
	AddTimeRow(false);
	AddTimeRow(true);
	auto length = Add<Field>("time.duration", _("Duration"), "s", false,
		_("How long the line shows: the end moves. Each selected line gets the same length, ending on a frame unless Shift is held"));
	length->SetFormat([](double ms) { return wxString::FromCDouble(ms / 1000, 2); }, [](wxString const& typed, double &ms) {
		wxString normal = typed;
		normal.Replace(",", ".");
		if (!normal.ToCDouble(&ms)) return false;
		ms *= 1000;
		return true;
	});
	length->SetNumberHandler({0, max_time, 10}, [this](double v, bool final) {
		int const d = static_cast<int>(std::lround(v));
		WriteNumber(_("set duration"), [=](std::vector<AssDialogue*> const& rows, AssDialogue &) {
			for (auto row : rows) row->End = std::max(Snap(static_cast<int>(row->Start) + d, true), static_cast<int>(row->Start));
		}, final);
	});
	auto row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(length, 1, wxEXPAND);
	AddRow(row);
	refreshers.push_back([this, length] {
		length->SetNumber(end - start, line_id != 0);
		length->Enable(line_id != 0);
	});
}

void TimingPage::BuildScene() {
	AddCardTitle("scene", _("Scene changes"));
	bool const have = !c->project->Keyframes().empty() && c->project->Timecodes().IsLoaded();
	bool const own = HasVideo() && !c->project->CanCloseKeyframes();
	if (have) {
		bool first = true;
		for (bool at_end : {false, true}) {
			std::string const name = at_end ? "end" : "start";
			auto row = new wxBoxSizer(wxHORIZONTAL);
			auto where = Add<Caption>("scene." + name);
			row->Add(where, 1, wxALIGN_CENTER_VERTICAL);
			auto snap = Add<TextButton>("scene.snap_" + name, _("Snap"),
				at_end ? _("Move the selected lines' ends to the frame before the nearest scene change, if it is within the distance")
					: _("Move the selected lines' starts onto the nearest scene change, if it is within the distance"));
			snap->on_click = [this, at_end] { SnapEnd(at_end); };
			row->Add(snap, 0, wxEXPAND | wxLEFT, FromDIP(4));
			AddRow(row, first);
			first = false;
			refreshers.push_back([this, at_end, where, snap] {
				SceneCut cut;
				auto provider = c->project->VideoProvider();
				int const distance = OPT_GET("Timing/Scene Snap Distance")->GetInt();
				int const ms = at_end ? end : start;
				if (!line_id || !NearestSceneCut(c->project->Keyframes(), provider ? provider->GetFrameCount() : 0, c->project->Timecodes(), ms, at_end, cut)) {
					where->SetText(wxString());
					snap->Enable(false);
					return;
				}
				bool within = std::abs(cut.time - ms) <= distance;
				where->SetText(CutText(at_end, within, cut.frames, distance));
				snap->Enable(within && cut.frames != 0);
			});
		}

		auto row = new wxBoxSizer(wxHORIZONTAL);
		auto distance = Add<Field>("scene.distance", wxS("±"), "ms", false,
			_("How far a start or end may move to a scene change, here and for Snap to Nearby Scene Changes (Preferences > Timing)"));
		distance->SetNumberHandler({0, 5000, 10}, [](double v, bool) {
			OPT_SET("Timing/Scene Snap Distance")->SetInt(static_cast<int>(std::lround(v)));
		});
		row->Add(distance, 1, wxEXPAND);
		auto both = Add<TextButton>("scene.both", _("Snap both"), cmd::get("time/snap/scene_nearby")->StrHelp());
		both->on_click = [this] { cmd::call("time/snap/scene_nearby", c); };
		row->Add(both, 0, wxEXPAND | wxLEFT, FromDIP(4));
		AddRow(row);
		refreshers.push_back([this, distance, both] {
			distance->SetNumber(OPT_GET("Timing/Scene Snap Distance")->GetInt(), true);
			both->Enable(line_id != 0);
		});
	}
	else {
		auto none = Add<Caption>("scene.none");
		none->SetText(_("No scene changes are known for this video"));
		card_body->Add(none, 0, wxEXPAND);
	}
	if (own) {
		auto note = Add<Caption>("scene.note");
		note->SetText(_("The video's own keyframes are not always scene changes"));
		card_body->Add(note, 0, wxEXPAND | wxTOP, FromDIP(3));
	}
	if (!have || own) {
		auto detect = Add<TextButton>("scene.detect", cmd::get("keyframe/detect")->StrDisplay(c), cmd::get("keyframe/detect")->StrHelp());
		detect->on_click = [this] { cmd::call("keyframe/detect", c); };
		detect->Enable(HasVideo());
		card_body->Add(detect, 0, wxEXPAND | wxTOP, FromDIP(3));
	}
}

void TimingPage::BuildLead() {
	AddCardTitle("lead", _("Lead-in/Lead-out"));
	auto row = new wxBoxSizer(wxHORIZONTAL);
	for (bool out : {false, true}) {
		const char *option = out ? "Tool/Timing Post Processor/Lead/OUT" : "Tool/Timing Post Processor/Lead/IN";
		auto field = Add<Field>(out ? "lead.out" : "lead.in", out ? wxGETTEXT_IN_CONTEXT("lead", "Out") : wxGETTEXT_IN_CONTEXT("lead", "In"), "ms", false,
			out ? _("How much later the selected lines end, as the timing post-processor adds it")
				: _("How much earlier the selected lines start, as the timing post-processor adds it"));
		field->SetNumberHandler({0, 5000, 10}, [option](double v, bool) {
			OPT_SET(option)->SetInt(static_cast<int>(std::lround(v)));
		});
		row->Add(field, 1, wxEXPAND | (out ? wxLEFT : 0), FromDIP(4));
		refreshers.push_back([field, option] { field->SetNumber(OPT_GET(option)->GetInt(), true); });
	}
	AddRow(row, true);

	auto buttons = new wxBoxSizer(wxHORIZONTAL);
	struct { const char *name; wxString label; bool in, out; } const kinds[] = {
		{"lead.add_in", _("+ Lead-in"), true, false},
		{"lead.add_out", _("+ Lead-out"), false, true},
		{"lead.add_both", _("+ Both"), true, true},
	};
	for (auto const& kind : kinds) {
		auto button = Add<TextButton>(kind.name, kind.label,
			_("Not over a line it did not already overlap: it stops at the line's edge, as the timing post-processor does"));
		bool in = kind.in, out = kind.out;
		button->on_click = [this, in, out] { Lead(in, out); };
		buttons->Add(button, 1, wxEXPAND | (buttons->IsEmpty() ? 0 : wxLEFT), FromDIP(4));
		refreshers.push_back([this, button] { button->Enable(line_id != 0); });
	}
	AddRow(buttons);
}

void TimingPage::BuildJoin() {
	AddCardTitle("join", _("Join adjacent lines"));
	auto add_field = [this](wxSizer *row, const char *name, wxString const& label, wxString const& unit, const char *option,
		double max, double step, double scale, wxString const& tip, bool first) {
		auto field = Add<Field>(name, label, unit, false, tip);
		field->SetNumberHandler({0, max, step}, [option, scale](double v, bool) {
			if (scale == 1) OPT_SET(option)->SetInt(static_cast<int>(std::lround(v)));
			else OPT_SET(option)->SetDouble(v / scale);
		});
		row->Add(field, 1, wxEXPAND | (first ? 0 : wxLEFT), FromDIP(4));
		refreshers.push_back([field, option, scale] {
			field->SetNumber(scale == 1 ? OPT_GET(option)->GetInt() : std::round(OPT_GET(option)->GetDouble() * scale), true);
		});
	};
	auto limits = new wxBoxSizer(wxHORIZONTAL);
	add_field(limits, "join.gap", _("Gap"), "ms", "Tool/Timing Post Processor/Threshold/Adjacent Gap", 5000, 10, 1,
		_("Lines this far apart or closer are joined; the timing post-processor uses the same"), true);
	add_field(limits, "join.overlap", _("Overlap"), "ms", "Tool/Timing Post Processor/Threshold/Adjacent Overlap", 5000, 10, 1,
		_("Lines overlapping by this much or less are joined; the timing post-processor uses the same"), false);
	AddRow(limits, true);

	auto row = new wxBoxSizer(wxHORIZONTAL);
	add_field(row, "join.bias", _("Bias"), "%", "Tool/Timing Post Processor/Adjacent Bias", 100, 5, 100,
		_("Where two lines meet: 0% at the first one's end, 100% at the second one's start"), true);
	auto join = Add<TextButton>("join.apply", _("Join"),
		_("Join each selected line to the selected one before it, where they are close enough, in one step"));
	join->on_click = [this] { Join(); };
	row->Add(join, 0, wxEXPAND | wxLEFT, FromDIP(4));
	AddRow(row);

	auto note = Add<Caption>("join.note");
	card_body->Add(note, 0, wxEXPAND | wxTOP, FromDIP(3));
	refreshers.push_back([this, join, note] {
		if (selected_times.size() < 2) {
			note->SetText(_("Select two or more lines to join them"));
			join->Enable(false);
			return;
		}
		int n = CountJoins(selected_times, OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->GetInt(),
			OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Overlap")->GetInt());
		note->SetText(n ? fmt_plural(n, "%d place to join among the selected lines", "%d places to join among the selected lines", n)
			: _("None of the selected lines is close enough to the next to join"));
		join->Enable(n > 0);
	});
}

void TimingPage::BuildShift() {
	AddCardTitle("shift", _("Shift"));
	bool const frames_known = c->project->Timecodes().IsLoaded();
	auto unit = Add<PageSwitch>("shift.unit", std::vector<wxString>{wxGETTEXT_IN_CONTEXT("shift", "Time"), wxGETTEXT_IN_CONTEXT("shift", "Frames")});
	unit->SetToolTip(frames_known ? _("Shift by milliseconds or by frames, as Shift Times does") : _("Shifting by frames needs a video or timecodes"));
	unit->on_choose = [this, frames_known](int index) {
		if (index == 1 && !frames_known) return;
		OPT_SET("Tool/Shift Times/ByTime")->SetBool(index == 0);
	};
	AddRow([&] { auto r = new wxBoxSizer(wxHORIZONTAL); r->Add(unit, 1, wxEXPAND); return r; }(), true);

	auto row = new wxBoxSizer(wxHORIZONTAL);
	auto amount = Add<Field>("shift.amount", wxGETTEXT_IN_CONTEXT("shift", "By"), "ms", false,
		_("How far the selected lines move; Shift Times keeps the same amount"));
	row->Add(amount, 1, wxEXPAND);
	auto earlier = Add<TextButton>("shift.earlier", _("Earlier"), _("Move the selected lines earlier, start and end, in one step"));
	earlier->on_click = [this] { Shift(-1); };
	row->Add(earlier, 0, wxEXPAND | wxLEFT, FromDIP(4));
	auto later = Add<TextButton>("shift.later", _("Later"), _("Move the selected lines later, start and end, in one step"));
	later->on_click = [this] { Shift(1); };
	row->Add(later, 0, wxEXPAND | wxLEFT, FromDIP(4));
	AddRow(row);

	refreshers.push_back([this, frames_known, unit, amount, earlier, later] {
		bool by_time = OPT_GET("Tool/Shift Times/ByTime")->GetBool() || !frames_known;
		unit->Select(by_time ? 0 : 1);
		// The same field for either: its unit, range and option follow
		const char *option = by_time ? "Tool/Shift Times/Time" : "Tool/Shift Times/Frames";
		amount->SetNumberHandler(by_time ? Field::Range{0, max_time, 10} : Field::Range{0, 100000, 1}, [option](double v, bool) {
			OPT_SET(option)->SetInt(static_cast<int>(std::lround(v)));
		});
		amount->SetUnit(by_time ? wxS("ms") : wxGETTEXT_IN_CONTEXT("shift", "frames"));
		amount->SetNumber(OPT_GET(option)->GetInt(), true);
		bool can = line_id != 0 && OPT_GET(option)->GetInt() != 0;
		earlier->Enable(can);
		later->Enable(can);
	});
}

void TimingPage::Build() {
	Freeze();
	refreshers.clear();
	cards.clear();
	body->Clear(true);
	parts.clear();
	BuildSummary();
	if (line_id) {
		BuildTimes();
		BuildScene();
		BuildLead();
		BuildJoin();
		BuildShift();
	}
	Layout();
	Thaw();
	InvalidateBestSize();
	if (on_layout) on_layout();
}

void TimingPage::Display() {
	for (auto const& refresh : refreshers) refresh();
}

void TimingPage::UpdateNow() {
	update_pending = false;
	AssDialogue const* line = c->selectionController->ReadActiveLine();
	auto selected = c->selectionController->ReadSelectedLines();
	if (!line && !selected.empty()) line = selected.front();
	line_count = std::max<size_t>(selected.size(), line ? 1 : 0);
	line_id = line ? line->Id : 0;
	start = line ? static_cast<int>(line->Start) : 0;
	end = line ? std::max(start, static_cast<int>(line->End)) : 0;
	text = line ? line->Text.get() : std::string();

	selected_times.clear();
	for (auto row : selected)
		if (!row->Comment) selected_times.emplace_back(static_cast<int>(row->Start), static_cast<int>(row->End));
	std::stable_sort(selected_times.begin(), selected_times.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

	// The lines either side of it in the file, comments left out
	has_before = has_after = false;
	if (line) {
		AssDialogue const* previous = nullptr;
		bool found = false;
		for (auto const& row : c->subtitleAccess->Read().Events) {
			if (&row == line) { found = true; continue; }
			if (row.Comment) continue;
			if (!found) previous = &row;
			else {
				has_after = true;
				gap_after = static_cast<int>(row.Start) - end;
				break;
			}
		}
		if (previous) {
			has_before = true;
			gap_before = start - static_cast<int>(previous->End);
		}
	}

	std::string key = line ? "L" : "-";
	if (HasVideo()) key += "V";
	if (c->project && !c->project->Keyframes().empty() && c->project->Timecodes().IsLoaded()) key += "K";
	if (c->project && !c->project->CanCloseKeyframes()) key += "O";
	if (key != structure) {
		structure = key;
		Build();
	}
	Display();
}

std::map<std::string, wxString> TimingPage::Shown() const {
	std::map<std::string, wxString> shown;
	for (auto const& [name, part] : parts) {
		auto drawn = dynamic_cast<StylePanel::Part*>(part);
		if (!drawn) continue;
		shown[name] = drawn->Shown() + (part->IsEnabled() ? "" : " disabled");
	}
	wxString list;
	for (auto const& card : cards) list += (list.empty() ? "" : " ") + to_wx(card);
	shown["cards"] = list;
	return shown;
}

void TimingPage::EnterNumber(std::string const& name, double value, bool final) {
	auto it = parts.find(name);
	if (it == parts.end() || !it->second->IsEnabled()) return;
	if (auto field = dynamic_cast<Field*>(it->second)) field->Enter(value, final);
}

bool TimingPage::TypeInto(std::string const& name, wxString const& typed) {
	auto it = parts.find(name);
	auto field = it == parts.end() ? nullptr : dynamic_cast<Field*>(it->second);
	if (!field) return false;
	field->StartEditing();
	if (!field->EditBox()) return false;
	field->EditBox()->ChangeValue(typed);
	field->CommitEditing();
	bool taken = !field->IsEditing();
	field->CancelEditing();
	return taken;
}

void TimingPage::Press(std::string const& name) {
	auto it = parts.find(name);
	if (it == parts.end() || !it->second->IsEnabled()) return;
	if (auto icon = dynamic_cast<IconButton*>(it->second); icon && icon->on_click) icon->on_click();
	else if (auto button = dynamic_cast<TextButton*>(it->second); button && button->on_click) button->on_click();
}

void TimingPage::Choose(std::string const& name, int index) {
	auto it = parts.find(name);
	if (it == parts.end()) return;
	if (auto choice = dynamic_cast<PageSwitch*>(it->second); choice && choice->on_choose) choice->on_choose(index);
}
