// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "workspace_tabs.h"

#include "format.h"
#include "include/aegisub/context.h"
#include "base_grid.h"
#include "theme.h"

#include <wx/button.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include <utility>

WorkspaceTabs::WorkspaceTabs(wxWindow *parent, agi::Context *c)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN)
, c(c)
{
    SetName("workspace-tabs");

    // Tab strip, shown only while a comparison is open.
    strip = new wxPanel(this);
    strip->SetName("workspace-strip");
    edit_tab = new wxToggleButton(strip, -1, _("Subtitles"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    edit_tab->SetName("workspace-tab-edit");
    edit_tab->SetToolTip(_("The subtitle editor. It is locked while the comparison is open."));
    // A label from the start: wxGTK 3.3 styles a toggle button through its
    // label widget, which an empty button lacks (GTK criticals on SetFont).
    compare_tab = new wxToggleButton(strip, -1, _("Compare"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    compare_tab->SetName("workspace-tab-compare");
    compare_tab->SetToolTip(_("The open comparison"));
    close_tab = new wxButton(strip, -1, wxString::FromUTF8("✕"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    close_tab->SetName("workspace-tab-close");
    close_tab->SetToolTip(_("End the comparison without writing to the subtitles"));
    notice = new wxStaticText(strip, -1, "");
    notice->SetName("workspace-notice");
    notice->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    auto tabs = new wxBoxSizer(wxHORIZONTAL);
    tabs->Add(edit_tab, wxSizerFlags().Center().Border(wxRIGHT, 2));
    tabs->Add(compare_tab, wxSizerFlags().Center());
    tabs->Add(close_tab, wxSizerFlags().Center().Border(wxRIGHT, 12));
    tabs->Add(notice, wxSizerFlags(1).Center());
    auto strip_sizer = new wxBoxSizer(wxVERTICAL);
    strip_sizer->Add(tabs, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 4));
    strip_sizer->Add(new wxStaticLine(strip), wxSizerFlags().Expand().Border(wxTOP, 4));
    strip->SetSizer(strip_sizer);

    // The editor tab's lock notice.
    banner = new wxPanel(this);
    banner->SetName("workspace-banner");
    banner_text = new wxStaticText(banner, -1, "");
    banner_text->SetName("workspace-banner-text");
    auto back = new wxButton(banner, -1, _("Back to Comparison"));
    back->SetName("workspace-banner-back");
    auto end = new wxButton(banner, -1, _("End Comparison"));
    end->SetName("workspace-banner-end");
    end->SetToolTip(_("End the comparison without writing to the subtitles"));
    auto banner_sizer = new wxBoxSizer(wxHORIZONTAL);
    banner_sizer->Add(banner_text, wxSizerFlags(1).Center().Border(wxLEFT | wxRIGHT, 8));
    banner_sizer->Add(back, wxSizerFlags().Center().Border(wxTOP | wxBOTTOM | wxRIGHT, 4));
    banner_sizer->Add(end, wxSizerFlags().Center().Border(wxTOP | wxBOTTOM | wxRIGHT, 4));
    banner->SetSizer(banner_sizer);

    book = new wxSimplebook(this);
    book->SetName("workspace-book");

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(strip, wxSizerFlags().Expand());
    sizer->Add(banner, wxSizerFlags().Expand());
    sizer->Add(book, wxSizerFlags(1).Expand());
    SetSizer(sizer);

    edit_tab->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { Select(false); });
    compare_tab->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { Select(true); });
    back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Select(true); });
    close_tab->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseComparison(); });
    end->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseComparison(); });
    theme_changed = theme::AddChangeListener([this] { Update(); });
    Update();
}

WorkspaceTabs::~WorkspaceTabs() {
    if (c->workspaceTabs == this) c->workspaceTabs = nullptr;
}

wxWindow *WorkspaceTabs::Book() const { return book; }

void WorkspaceTabs::SetEditor(wxWindow *window) {
    editor = window;
    book->InsertPage(0, editor, _("Subtitles"), true);
    Update();
}

void WorkspaceTabs::OpenComparison(ComparisonPage *p, wxString const& file) {
    page = p;
    name = file;
    last_applied = false;
    book->AddPage(page, name);
    Select(true);
    page->Started();
}

void WorkspaceTabs::ComparisonEnded(bool applied) {
    if (!page) return;
    last_applied = applied;
    auto old = page;
    page = nullptr;
    name.clear();
    // The page is still running the handler that ended it: remove it now and
    // destroy it once that handler returned.
    book->RemovePage(book->FindPage(old));
    old->Hide();
    CallAfter([old] { old->Destroy(); });
    Select(false);
    if (c->subsGrid) c->subsGrid->SetFocus();
}

bool WorkspaceTabs::CloseComparison(bool ask) {
    if (!page) return true;
    if (ask) Select(true); // the confirmation is about what this tab shows
    return page->TryClose(ask);
}

bool WorkspaceTabs::ComparisonInFront() const {
    return page && book->GetSelection() != wxNOT_FOUND && book->GetPage(book->GetSelection()) == page;
}

void WorkspaceTabs::Select(bool comparison) {
    if (comparison && !page) comparison = false;
    if (auto target = comparison ? static_cast<wxWindow*>(page) : editor) {
        int index = book->FindPage(target);
        if (index != wxNOT_FOUND && index != book->GetSelection()) book->ChangeSelection(index);
    }
    notice->SetLabel("");
    Update();
    if (comparison) page->SetFocus();
}

void WorkspaceTabs::Update() {
    bool open = page != nullptr, front = ComparisonInFront();
    // The front tab is pressed, bold and in the accent colour: a pressed
    // toggle alone is hard to tell apart in some themes.
    auto colours = theme::UiColours();
    auto accent = colours.accent;
    auto plain = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    for (auto [tab, on] : {std::pair{edit_tab, !front}, std::pair{compare_tab, front}}) {
        tab->SetValue(on);
        tab->SetFont(on ? GetFont().Bold() : GetFont());
        tab->SetForegroundColour(on ? accent : plain);
    }
    if (open) {
        compare_tab->SetLabel(fmt_tl("Compare: %s", name));
        banner_text->SetLabel(fmt_tl("Comparing with %s. The subtitles cannot be edited until the comparison ends.", name));
    }
    banner->SetBackgroundColour(colours.notice_back);
    banner_text->SetForegroundColour(colours.notice_text);
    banner->Refresh();
    GetSizer()->Show(strip, open);
    GetSizer()->Show(banner, open && !front);
    Layout();
    changed();
}

bool WorkspaceTabs::Permits(std::string_view command) const {
    return !ComparisonInFront() || command.starts_with("app/") || command.starts_with("help/");
}

bool WorkspaceTabs::AllowsCommand(std::string_view command) {
    if (Permits(command)) return true;
    notice->SetLabel(_("This command is not available in the comparison tab. Switch to the Subtitles tab to use it."));
    strip->Layout();
    return false;
}
