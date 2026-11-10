// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "timing_adjust.h"
#include "ass_dialogue.h"
#include "utils.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_row_edit.h"
#include "compat.h"
#include "dialog_manager.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "help_button.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "theme.h"
#include "subs_controller.h"
#include "timeedit_ctrl.h"

#include <libaegisub/ass/time.h>
#include <memory>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>
#include <libaegisub/signal.h>
#include <libaegisub/vfr.h>

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/writer.h>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
class DialogShiftTimes final : public wxDialog {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
	agi::Context *context;

	agi::fs::path history_filename;
	json::Array history;
	agi::vfr::Framerate fps;
	agi::signal::Connection timecodes_loaded_slot;
	agi::signal::Connection selected_set_changed_slot;

	TimeEdit *shift_time;
	FlatFrame *time_frame;
	flat::NumberField *shift_frames;
	FlatSwitch *shift_by;      ///< Time or frames
	FlatSwitch *direction;     ///< Forward or backward
	FlatSwitch *selection_mode;
	FlatSwitch *time_fields;
	FlatList *history_box;

	bool ByTime() const { return shift_by->GetSelection() == 0; }
	/// Show the field for time or for frames
	void ShowAmount(bool by_time);

	void SaveHistory(json::Array shifted_blocks);
	void LoadHistory();
	void ShowHistory();
	void Process(wxCommandEvent&);
	int Shift(int initial_time, int shift, bool by_time, agi::vfr::Time type);

	void OnClear(wxCommandEvent&);
	void OnHistoryClick(wxCommandEvent&);

	void OnSelectedSetChanged();
	void OnTimecodesLoaded(agi::vfr::Framerate const& new_fps);

public:
	DialogShiftTimes(agi::Context *context);
	~DialogShiftTimes();
};

static wxString get_history_string(json::Object &obj) {
	auto filename = to_wx(obj["filename"]);
	if (filename.empty())
		filename = _("unsaved");

	auto shift_amount = to_wx(obj["amount"]);
	if (!obj["is by time"])
		shift_amount = fmt_plural(wxAtoi(shift_amount), "1 frame", "%s frames", shift_amount);

	wxString shift_direction = obj["is backward"] ? _("backward") : _("forward");

	int64_t time_field = obj["fields"];
	wxString fields =
		time_field == 0 ? _("s+e") :
		time_field == 1 ? _("s")   :
		                  _("e")   ;

	json::Array& sel = obj["selection"];
	wxString lines;

	int64_t sel_mode = obj["mode"];
	if (sel_mode == 0)
		lines = _("all");
	else if (sel_mode == 2) {
		if (!sel.empty())
			lines = fmt_tl("from %d onward", (int64_t)static_cast<json::Object&>(sel.front())["start"]);
	}
	else {
		lines += _("sel ");
		for (auto it = sel.begin(); it != sel.end(); ++it) {
			json::Object& range = *it;
			int64_t beg = range["start"];
			int64_t end = range["end"];
			if (beg == end)
				lines += std::to_wstring(beg);
			else
				lines += fmt_wx("%d-%d", beg, end);
			if (it + 1 != sel.end())
				lines += ";";
		}
	}

	return fmt_wx("%s, %s %s, %s, %s", filename, shift_amount, shift_direction, fields, lines);
}

DialogShiftTimes::DialogShiftTimes(agi::Context *context)
: wxDialog(context->parent, -1, _("Shift Times"))
, context(context)
, history_filename(config::path->Decode("?user/shift_history.json"))
, timecodes_loaded_slot(context->project->AddTimecodesListener(&DialogShiftTimes::OnTimecodesLoaded, this))
, selected_set_changed_slot(context->selectionController->AddSelectionListener("dialog_shift_times.AddSelectionListener", [this](SubtitleModelRebindTicket ticket) { OnSelectedSetChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)))
{
	SetIcons(GETICONS(shift_times_toolbutton));
	theme::UseSurface(this);
	auto dim = [this](wxString const& text) {
		auto label = new wxStaticText(this, -1, text);
		label->SetForegroundColour(flat::Colours(this).dim);
		return label;
	};

	// How far: a time or a number of frames, and which way
	shift_by = new FlatSwitch(this, {_("&Time"), _("&Frames")});
	shift_by->SetItemToolTip(0, _("Shift by time"));
	shift_by->SetItemToolTip(1, _("Shift by frames"));
	shift_by->on_choose = [this](int index) { ShowAmount(index == 0); };

	time_frame = new FlatFrame(this, _("Time"));
	shift_time = new TimeEdit(time_frame, -1, context, "", wxDefaultSize, false, wxBORDER_NONE);
	time_frame->SetContent(shift_time, ToDIP(shift_time->GetSizeFromText(wxS("0:00:00.000")).x));
	shift_time->SetToolTip(_("Enter time in h:mm:ss.cs notation"));

	shift_frames = flat::NumberField::Create(this, _("Frames"), 0, 0, 1e7, 1, 0, 80);
	shift_frames->SetToolTip(_("Enter number of frames to shift by"));

	direction = new FlatSwitch(this, {_("For&ward"), _("&Backward")});
	direction->SetItemToolTip(0, _("Shifts subs forward, making them appear later. Use if they are appearing too soon."));
	direction->SetItemToolTip(1, _("Shifts subs backward, making them appear earlier. Use if they are appearing too late."));

	// Which lines and which of their times
	selection_mode = new FlatSwitch(this, {_("&All rows"), _("Selected &rows"), _("Selection &onward")});
	time_fields = new FlatSwitch(this, {_("Start a&nd End times"), _("&Start times only"), _("&End times only")});

	// What was shifted before, to do again
	history_box = new FlatList(this, false, false, _("Nothing shifted yet"));
	history_box->SetMinSize(FromDIP(wxSize(320, 150)));
	history_box->SetToolTip(_("Click an entry to load its shift"));
	auto clear_button = new FlatButton(this, _("&Clear"));
	clear_button->Bind(wxEVT_BUTTON, &DialogShiftTimes::OnClear, this);

	// Set initial control states
	OnTimecodesLoaded(context->project->Timecodes());
	OnSelectedSetChanged();
	LoadHistory();

	shift_time->SetTime(OPT_GET("Tool/Shift Times/Time")->GetInt());
	shift_frames->SetValue(OPT_GET("Tool/Shift Times/Frames")->GetInt());
	shift_by->SetSelection(!OPT_GET("Tool/Shift Times/ByTime")->GetBool() && shift_by->IsItemEnabled(1) ? 1 : 0);
	time_fields->SetSelection(OPT_GET("Tool/Shift Times/Type")->GetInt());
	if (selection_mode->IsItemEnabled(OPT_GET("Tool/Shift Times/Affect")->GetInt()))
		selection_mode->SetSelection(OPT_GET("Tool/Shift Times/Affect")->GetInt());
	direction->SetSelection(OPT_GET("Tool/Shift Times/Direction")->GetBool() ? 1 : 0);

	// Position controls: a name and its choice on each row, the history beside
	auto amount = new wxBoxSizer(wxHORIZONTAL);
	amount->Add(time_frame, wxSizerFlags(1).Expand());
	amount->Add(shift_frames->Frame(), wxSizerFlags(1).Expand());

	auto grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
	grid->AddGrowableCol(1, 1);
	auto row = [&](wxString const& name, wxWindow *control) {
		grid->Add(dim(name), wxSizerFlags().CenterVertical());
		grid->Add(control, wxSizerFlags().Expand());
	};
	row(_("Shift by"), shift_by);
	grid->AddSpacer(0);
	grid->Add(amount, wxSizerFlags().Expand());
	row(_("Direction"), direction);
	row(_("Affect"), selection_mode);
	row(_("Times"), time_fields);

	auto history_sizer = new wxBoxSizer(wxVERTICAL);
	auto history_title = new wxBoxSizer(wxHORIZONTAL);
	history_title->Add(dim(_("Load from history")), wxSizerFlags(1).CenterVertical());
	history_title->Add(clear_button);
	history_sizer->Add(history_title, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(4)));
	history_sizer->Add(history_box, wxSizerFlags(1).Expand());

	wxSizer *top_sizer = new wxBoxSizer(wxHORIZONTAL);
	top_sizer->Add(grid, wxSizerFlags());
	top_sizer->Add(history_sizer, wxSizerFlags(1).Expand().Border(wxLEFT, FromDIP(16)));

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top_sizer, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(10)));
	main_sizer->Add(new flat::DialogButtons(this, wxOK | wxCANCEL | wxHELP, "Shift Times"), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
	ShowAmount(ByTime());
	SetSizerAndFit(main_sizer);
	CenterOnParent();

	Bind(wxEVT_BUTTON, &DialogShiftTimes::Process, this, wxID_OK);
	shift_time->Bind(wxEVT_TEXT_ENTER, &DialogShiftTimes::Process, this);
	history_box->Bind(wxEVT_LISTBOX, &DialogShiftTimes::OnHistoryClick, this);
}

void DialogShiftTimes::ShowAmount(bool by_time) {
	time_frame->Show(by_time);
	shift_frames->Frame()->Show(!by_time);
	Layout();
	(by_time ? static_cast<wxWindow*>(shift_time) : shift_frames->Text())->SetFocus();
}

DialogShiftTimes::~DialogShiftTimes() {
	OPT_SET("Tool/Shift Times/Time")->SetInt(shift_time->GetTime());
	OPT_SET("Tool/Shift Times/Frames")->SetInt(shift_frames->GetInt());
	OPT_SET("Tool/Shift Times/ByTime")->SetBool(ByTime());
	OPT_SET("Tool/Shift Times/Type")->SetInt(time_fields->GetSelection());
	OPT_SET("Tool/Shift Times/Affect")->SetInt(selection_mode->GetSelection());
	OPT_SET("Tool/Shift Times/Direction")->SetBool(direction->GetSelection() == 1);
}

void DialogShiftTimes::OnTimecodesLoaded(agi::vfr::Framerate const& new_fps) {
	fps = new_fps;
	// Frames only with timecodes to count them by
	shift_by->EnableItem(1, fps.IsLoaded());
	if (!fps.IsLoaded() && !ByTime()) {
		shift_by->SetSelection(0);
		if (GetSizer()) ShowAmount(true);
	}
}

void DialogShiftTimes::OnSelectedSetChanged() {
	bool any = !context->selectionController->ReadSelectedSet().empty();
	selection_mode->EnableItem(1, any);
	selection_mode->EnableItem(2, any);
	if (!any) selection_mode->SetSelection(0);
}

void DialogShiftTimes::OnClear(wxCommandEvent &) {
	agi::fs::Remove(history_filename);
	history_box->Set(wxArrayString());
	history.clear();
}

void DialogShiftTimes::OnHistoryClick(wxCommandEvent &evt) {
	size_t entry = evt.GetInt();
	if (entry >= history.size()) return;

	json::Object& obj = history[entry];
	if (obj["is by time"]) {
		shift_time->SetTime(agi::Time((std::string&)obj["amount"]));
		shift_by->SetSelection(0);
		ShowAmount(true);
	}
	else {
		long frames = 0;
		to_wx((std::string&)obj["amount"]).ToLong(&frames);
		shift_frames->SetValue(frames);
		if (shift_by->IsItemEnabled(1)) {
			shift_by->SetSelection(1);
			ShowAmount(false);
		}
	}

	direction->SetSelection(obj["is backward"] ? 1 : 0);
	int mode = static_cast<int>((int64_t)obj["mode"]);
	if (selection_mode->IsItemEnabled(mode)) selection_mode->SetSelection(mode);
	time_fields->SetSelection((int64_t)obj["fields"]);
}

void DialogShiftTimes::SaveHistory(json::Array shifted_blocks) {
	json::Object new_entry;
	new_entry["filename"] = context->subsController->Filename().filename().string();
	new_entry["is by time"] = ByTime();
	new_entry["is backward"] = direction->GetSelection() == 1;
	new_entry["amount"] = from_wx(ByTime() ? shift_time->GetValue() : wxString::Format("%d", shift_frames->GetInt()));
	new_entry["fields"] = time_fields->GetSelection();
	new_entry["mode"] = selection_mode->GetSelection();
	new_entry["selection"] = std::move(shifted_blocks);

	history.insert(history.begin(), std::move(new_entry));
	if (history.size() > 50)
		history.resize(50);
	ShowHistory();

	try {
		agi::JsonWriter::Write(history, agi::io::Save(history_filename).Get());
	}
	catch (agi::fs::FileSystemError const& e) {
		LOG_E("dialog_shift_times/save_history") << "Cannot save shift times history: " << e.GetMessage();
	}
}

void DialogShiftTimes::LoadHistory() {
	try {
		json::UnknownElement root;
		json::Reader::Read(root, *agi::io::Open(history_filename));
		history = std::move(static_cast<json::Array&>(root));
	}
	catch (agi::fs::FileSystemError const& e) {
		LOG_D("dialog_shift_times/load_history") << "Cannot load shift times history: " << e.GetMessage();
	}
	catch (json::Exception const& e) {
		LOG_D("dialog_shift_times/load_history") << "Cannot load shift times history: " << e.what();
	}
	ShowHistory();
}

void DialogShiftTimes::ShowHistory() {
	wxArrayString entries;
	for (auto& history_entry : history)
		entries.push_back(get_history_string(history_entry));
	history_box->Set(entries);
}

void DialogShiftTimes::Process(wxCommandEvent &) {
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return;
	int mode = selection_mode->GetSelection();
	int type = time_fields->GetSelection();
	bool reverse = direction->GetSelection() == 1;
	bool by_time = ByTime();

	bool start = type != 2;
	bool end = type != 1;

	auto const& sel = context->selectionController->ReadSelectedSet();

	long shift;
	if (by_time) {
		shift = shift_time->GetTime();
		if (shift == 0) {
			Close();
			return;
		}
	}
	else
		shift = shift_frames->GetInt();

	if (reverse)
		shift = -shift;

	// Track which rows were shifted for the log
	int block_start = 0;
	json::Array shifted_blocks;

	SubtitleRowEdit staged(context,true);
    for (auto candidate : staged.rows) {
        auto& line=*candidate;
		if (!staged.selected.count(line.Id)) {
			if (block_start) {
				json::Object block;
				block["start"] = block_start;
				block["end"] = line.Row;
				shifted_blocks.push_back(std::move(block));
				block_start = 0;
			}
			if (mode == 1) continue;
			if (mode == 2 && shifted_blocks.empty()) continue;
		}
		else if (!block_start)
			block_start = line.Row + 1;

		if (start)
			line.Start = Shift(line.Start, shift, by_time, agi::vfr::START);
		if (end)
			line.End = Shift(line.End, shift, by_time, agi::vfr::END);
	}

	auto edit=context->subtitleAccess->Edit(edit_scope->Lease());edit.UpdateRows(staged.Values());edit.Commit(_("shifting"), AssFile::COMMIT_DIAG_TIME);

	if (block_start) {
		json::Object block;
		block["start"] = block_start;
		block["end"] = context->subtitleAccess->Read().Events.back().Row + 1;
		shifted_blocks.push_back(std::move(block));
	}

	SaveHistory(std::move(shifted_blocks));
	Close();
}

int DialogShiftTimes::Shift(int initial_time, int shift, bool by_time, agi::vfr::Time type) {
	return ShiftTime(initial_time, shift, by_time, fps, type);
}
}

void ShowShiftTimesDialog(agi::Context *c) {
	c->dialog->Show<DialogShiftTimes>(c);
}
