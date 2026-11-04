// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// C2-B comparison window: compare the open subtitles with a baseline file and
// apply chosen differences (docs/subtitle-compare-ui.md). Modal: while it is
// open the comparison session freezes every other edit anyway.
#include "dialogs.h"

#include "charset_detect.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "subs_controller.h"
#include "subtitle_compare.h"
#include "subtitle_compare_view.h"
#include "subtitle_format.h"

#include <libaegisub/charset.h>
#include <libaegisub/exception.h>

#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace {
namespace model = agi::ass::compare;
namespace view = subtitle_compare_view;

wxString field_label(model::Field field) {
    switch (field) {
        case model::Text: return _("Text");
        case model::Start: return _("Start");
        case model::End: return _("End");
        case model::Layer: return _("Layer");
        case model::Style: return _("Style");
        case model::Actor: return _("Actor");
        case model::MarginL: return _("Left margin");
        case model::MarginR: return _("Right margin");
        case model::MarginV: return _("Vertical margin");
        case model::Effect: return _("Effect");
        case model::Comment: return _("Comment");
        case model::Extra: return _("Extra data");
        default: return {};
    }
}

wxString kind_label(model::Kind kind) {
    switch (kind) {
        case model::Kind::Modified: return _("Modified");
        case model::Kind::Added: return _("Added");
        case model::Kind::Deleted: return _("Deleted");
        default: return _("Unchanged");
    }
}

wxString ambiguity_label(model::Ambiguity ambiguity) {
    switch (ambiguity) {
        case model::Ambiguity::Duplicate: return _("several identical candidates");
        case model::Ambiguity::Crossing: return _("order changed");
        case model::Ambiguity::ConflictingEvidence: return _("time and text point to different lines");
        case model::Ambiguity::NoReliableMatch: return _("no reliable match");
        default: return {};
    }
}

wxString fields_label(view::Row const& row) {
    wxString out;
    for (auto const& d : row.diffs) {
        if (!out.empty()) out += ", ";
        out += field_label(d.field);
    }
    return out;
}

// One line in a list cell: long text is shortened, line breaks are visible.
wxString cell(std::string const& text) {
    auto s = to_wx(text);
    s.Replace("\n", " ");
    if (s.size() > 200) s = s.Left(200) + wxString::FromUTF8("…");
    return s;
}

wxString describe(subtitle_apply::Report const& r) {
    wxString out;
    if (r.Ok()) {
        out = fmt_tl("Will apply %d modified, %d added and %d deleted lines as one undoable change.",
            (int)r.modified, (int)r.added, (int)r.deleted);
        for (auto const& name : r.imported_styles) out += "\n" + fmt_tl("Style \"%s\" will be copied from the baseline.", name);
        for (auto const& name : r.missing_styles) out += "\n" + fmt_tl("Style \"%s\" is not defined in either file.", name);
        if (!r.imported_keys.empty()) {
            wxString keys;
            for (auto const& k : r.imported_keys) keys += (keys.empty() ? "" : ", ") + to_wx(k);
            out += "\n" + fmt_tl("Extra data will be copied from the baseline: %s", keys);
        }
        return out;
    }
    out = _("These changes cannot be applied:");
    for (auto const& refusal : r.refusals) {
        out += "\n";
        switch (refusal.problem) {
            case subtitle_apply::Problem::PartialBinding:
                out += fmt_tl("Bound lines must be applied together (group %s).", refusal.detail); break;
            case subtitle_apply::Problem::StyleConflict:
                out += fmt_tl("Style \"%s\" is defined differently in the current subtitles and in the baseline.", refusal.detail); break;
            case subtitle_apply::Problem::StyleAmbiguous:
                out += fmt_tl("Style \"%s\" is defined more than once.", refusal.detail); break;
            case subtitle_apply::Problem::InvalidExtradata:
                out += _("The extra data of the subtitles cannot be read."); break;
            default:
                out += _("Invalid selection."); break;
        }
    }
    return out;
}

enum { COL_APPLY, COL_KIND, COL_CURRENT, COL_BASELINE, COL_TIME, COL_CURRENT_TEXT, COL_BASELINE_TEXT, COL_FIELDS, COL_NOTE };

class DialogCompare final : public wxDialog {
    agi::Context *c;
    SubtitleComparison& comparison;
    view::State state;
    std::optional<model::Kind> filter;
    std::vector<size_t> visible; // list row -> state row
    std::optional<size_t> detail;
    wxChoice *filter_choice;
    wxDataViewListCtrl *list;
    wxCheckListBox *fields;
    wxTextCtrl *report;
    wxButton *apply;

    void Populate();
    void RefreshToggles();
    void ShowDetail();
    void UpdateReport();
    void OnToggle(wxDataViewEvent& event);
    void OnSelect(wxDataViewEvent&);
    void OnField(wxCommandEvent& event);
    void OnApply(wxCommandEvent&);
public:
    DialogCompare(agi::Context *c, SubtitleComparison& comparison, wxString const& name);
};

DialogCompare::DialogCompare(agi::Context *c, SubtitleComparison& comparison, wxString const& name)
: wxDialog(c->parent, -1, _("Compare Subtitles"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, c(c)
, comparison(comparison)
, state(comparison.Current(), comparison.Baseline(), comparison.Fields(), comparison.Items())
{
    auto header = new wxStaticText(this, -1, fmt_tl("Baseline: %s\n%d modified, %d added, %d deleted. Checked changes are applied to the open subtitles.",
        name, (int)state.Count(model::Kind::Modified), (int)state.Count(model::Kind::Added), (int)state.Count(model::Kind::Deleted)));

    wxArrayString filters;
    filters.Add(_("All changes")); filters.Add(_("Modified")); filters.Add(_("Added")); filters.Add(_("Deleted"));
    filter_choice = new wxChoice(this, -1, wxDefaultPosition, wxDefaultSize, filters);
    filter_choice->SetSelection(0);
    auto select_all = new wxButton(this, -1, _("Check &all"));
    auto select_none = new wxButton(this, -1, _("Check &none"));

    list = new wxDataViewListCtrl(this, -1, wxDefaultPosition, wxSize(900, 360), wxDV_ROW_LINES | wxDV_SINGLE);
    list->AppendToggleColumn(_("Apply"), wxDATAVIEW_CELL_ACTIVATABLE, wxCOL_WIDTH_AUTOSIZE);
    list->AppendTextColumn(_("Change"));
    list->AppendTextColumn(_("Line"), wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE);
    list->AppendTextColumn(_("Baseline line"), wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE);
    list->AppendTextColumn(_("Time"));
    list->AppendTextColumn(_("Current text"), wxDATAVIEW_CELL_INERT, 220);
    list->AppendTextColumn(_("Baseline text"), wxDATAVIEW_CELL_INERT, 220);
    list->AppendTextColumn(_("Changed fields"));
    list->AppendTextColumn(_("Note"));

    fields = new wxCheckListBox(this, -1, wxDefaultPosition, wxSize(-1, 110));
    report = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE | wxTE_READONLY);

    auto filter_sizer = new wxBoxSizer(wxHORIZONTAL);
    filter_sizer->Add(new wxStaticText(this, -1, _("Show:")), wxSizerFlags().Center().Border(wxRIGHT));
    filter_sizer->Add(filter_choice, wxSizerFlags().Border(wxRIGHT));
    filter_sizer->Add(select_all, wxSizerFlags().Border(wxRIGHT));
    filter_sizer->Add(select_none);

    auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    apply = static_cast<wxButton*>(FindWindow(wxID_OK));
    apply->SetLabel(_("&Apply"));

    auto main = new wxBoxSizer(wxVERTICAL);
    main->Add(header, wxSizerFlags().Expand().Border());
    main->Add(filter_sizer, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM));
    main->Add(list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));
    main->Add(new wxStaticText(this, -1, _("Fields of the selected line to take from the baseline:")), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP));
    main->Add(fields, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT));
    main->Add(report, wxSizerFlags().Expand().Border());
    main->Add(buttons, wxSizerFlags().Right().Border(wxLEFT | wxRIGHT | wxBOTTOM));
    SetSizerAndFit(main);
    CenterOnParent();

    filter_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        static const std::optional<model::Kind> kinds[] = {std::nullopt, model::Kind::Modified, model::Kind::Added, model::Kind::Deleted};
        filter = kinds[std::max(0, filter_choice->GetSelection())];
        Populate();
    });
    select_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { state.CheckAll(true, filter); RefreshToggles(); ShowDetail(); UpdateReport(); });
    select_none->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { state.CheckAll(false, filter); RefreshToggles(); ShowDetail(); UpdateReport(); });
    list->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &DialogCompare::OnToggle, this);
    list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &DialogCompare::OnSelect, this);
    fields->Bind(wxEVT_CHECKLISTBOX, &DialogCompare::OnField, this);
    apply->Bind(wxEVT_BUTTON, &DialogCompare::OnApply, this);

    Populate();
}

void DialogCompare::Populate() {
    list->DeleteAllItems();
    visible.clear();
    auto const& rows = state.Rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        auto const& row = rows[i];
        if (filter && row.kind != *filter) continue;
        wxVector<wxVariant> values;
        values.push_back(wxVariant(state.Checked(i)));
        values.push_back(wxVariant(kind_label(row.kind)));
        values.push_back(wxVariant(row.current_row ? wxString::Format("%zu", *row.current_row) : wxString()));
        values.push_back(wxVariant(row.baseline_row ? wxString::Format("%zu", *row.baseline_row) : wxString()));
        values.push_back(wxVariant(to_wx(row.time)));
        values.push_back(wxVariant(cell(row.current_text)));
        values.push_back(wxVariant(cell(row.baseline_text)));
        values.push_back(wxVariant(fields_label(row)));
        values.push_back(wxVariant(ambiguity_label(row.ambiguity)));
        list->AppendItem(values);
        visible.push_back(i);
    }
    detail.reset();
    ShowDetail();
    UpdateReport();
}

void DialogCompare::RefreshToggles() {
    for (size_t r = 0; r < visible.size(); ++r)
        list->SetToggleValue(state.Checked(visible[r]), r, COL_APPLY);
}

void DialogCompare::ShowDetail() {
    fields->Clear();
    if (!detail) { fields->Enable(false); return; }
    auto const& row = state.Rows()[*detail];
    fields->Enable(row.kind == model::Kind::Modified);
    if (row.kind != model::Kind::Modified) {
        fields->Append(row.kind == model::Kind::Added ? _("Added lines are applied as a whole.") : _("Deleted lines are applied as a whole."));
        return;
    }
    for (auto const& d : row.diffs) {
        auto index = fields->Append(fmt_tl("%s: %s  →  %s", field_label(d.field), cell(d.current), cell(d.baseline)));
        fields->Check(index, (state.Fields(*detail) & d.field) && state.Checked(*detail));
    }
}

void DialogCompare::UpdateReport() {
    if (!state.CheckedCount()) {
        report->SetValue(_("Check the changes to apply."));
        apply->Enable(false);
        return;
    }
    auto r = c->subsController->CheckApply(comparison, state.Choices());
    report->SetValue(describe(r));
    apply->Enable(r.Ok());
}

void DialogCompare::OnToggle(wxDataViewEvent& event) {
    if (event.GetColumn() != COL_APPLY) return;
    int r = list->ItemToRow(event.GetItem());
    if (r < 0 || size_t(r) >= visible.size()) return;
    state.Check(visible[r], list->GetToggleValue(r, COL_APPLY));
    if (detail == visible[r]) ShowDetail();
    UpdateReport();
}

void DialogCompare::OnSelect(wxDataViewEvent&) {
    int r = list->GetSelectedRow();
    if (r == wxNOT_FOUND || size_t(r) >= visible.size()) detail.reset();
    else detail = visible[r];
    ShowDetail();
}

void DialogCompare::OnField(wxCommandEvent& event) {
    if (!detail) return;
    auto const& row = state.Rows()[*detail];
    int index = event.GetInt();
    if (row.kind != model::Kind::Modified || index < 0 || size_t(index) >= row.diffs.size()) return;
    state.SetField(*detail, row.diffs[index].field, fields->IsChecked(index));
    auto it = std::find(visible.begin(), visible.end(), *detail);
    if (it != visible.end()) list->SetToggleValue(state.Checked(*detail), it - visible.begin(), COL_APPLY);
    UpdateReport();
}

void DialogCompare::OnApply(wxCommandEvent&) {
    subtitle_apply::Report r;
    try {
        r = c->subsController->ApplyComparison(comparison, state.Choices());
    }
    catch (SubtitleApplyRejected const& e) {
        wxMessageBox(to_wx(e.what()), _("Compare Subtitles"), wxOK | wxICON_ERROR, this);
        EndModal(wxID_CANCEL);
        return;
    }
    if (!r.Ok()) { report->SetValue(describe(r)); apply->Enable(false); return; }
    EndModal(wxID_OK);
}
}

bool ShowCompareDialog(agi::Context *c, agi::fs::path const& path) {
    auto title = _("Compare Subtitles");
    std::string encoding;
    try {
        encoding = CharSetDetect::GetEncoding(path); // asks when detection is ambiguous
    }
    catch (agi::UserCancelException const&) { return false; }
    catch (agi::Exception const& e) {
        wxMessageBox(to_wx(e.GetMessage()), title, wxOK | wxICON_ERROR, c->parent);
        return false;
    }
    SubtitleFormat const* reader = nullptr;
    try {
        reader = SubtitleFormat::GetReader(path, encoding.c_str());
    }
    catch (agi::Exception const&) {
        wxMessageBox(_("This file is not a supported subtitle format."), title, wxOK | wxICON_ERROR, c->parent);
        return false;
    }
    subtitle_compare::Baseline baseline;
    {
        wxBusyCursor busy;
        // Readers that need a choice (TXT import, MicroDVD frame rate) ask here, on the host thread.
        baseline = subtitle_compare::ReadBaseline(*reader, path, c->project->Timecodes(), encoding);
    }
    if (baseline.state == subtitle_compare::LoadState::Cancelled) return false;
    if (baseline.state != subtitle_compare::LoadState::Completed) {
        wxMessageBox(fmt_tl("The file could not be read:\n%s", baseline.error), title, wxOK | wxICON_ERROR, c->parent);
        return false;
    }
    std::optional<SubtitleComparison> comparison;
    try {
        wxBusyCursor busy;
        comparison.emplace(c->subsController->BeginComparison(std::move(baseline.file), baseline.fields));
    }
    catch (SubtitleApplyRejected const& e) {
        wxMessageBox(to_wx(e.what()), title, wxOK | wxICON_INFORMATION, c->parent);
        return false;
    }
    bool differs = false;
    for (auto const& item : comparison->Items()) differs |= item.kind != model::Kind::Unchanged;
    if (!differs) {
        wxMessageBox(_("The files have no differences that can be applied."), title, wxOK | wxICON_INFORMATION, c->parent);
        return false;
    }
    DialogCompare dialog(c, *comparison, path.filename().wstring());
    return dialog.ShowModal() == wxID_OK;
}
