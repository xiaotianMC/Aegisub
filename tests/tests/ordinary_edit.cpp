// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <gtest/gtest.h>
#include "include/aegisub/context.h"
#include "ass_file.h"
#include "ass_dialogue.h"
#include "ass_attachment.h"
#include "ass_info.h"
#include "ass_style.h"
#include "subtitle_project_access.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "timeedit_ctrl.h"
#include "base_grid.h"
#include "options.h"
#include "utils.h"
#include <wx/frame.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <wx/dcclient.h>

namespace {
class ordinary_edit : public testing::Test {
protected:
    std::unique_ptr<agi::Context> c;
    wxFrame *window = nullptr;
    SubsEditBox *box = nullptr;
    SubsTextEditCtrl *editor = nullptr;
    void SetUp() override {
        c = std::make_unique<agi::Context>();
        window = new wxFrame(nullptr, wxID_ANY, "isolated editing tests");
        c->parent = window;
        box = new SubsEditBox(window, c.get());
        for (auto child : box->GetChildren())
            if (auto text = dynamic_cast<SubsTextEditCtrl*>(child)) editor = text;
        ASSERT_NE(nullptr, editor);
        auto write = c->subtitleAccess->TryWrite();
        c->ass->LoadDefault(); c->ass->Commit("", AssFile::COMMIT_NEW);
        auto line = &c->ass->Events.front();
        c->selectionController->SetSelectionAndActive({line}, line);
    }
    void TearDown() override { delete window; c.reset(); }
    void Input(std::string const& value) {
        editor->SetTextRaw(value.c_str());
        // Hidden controls do not receive all native WM_NOTIFY deliveries.
        // Deliver the same wx event to the production handler deterministically.
        wxStyledTextEvent event(wxEVT_STC_MODIFIED, editor->GetId());
        event.SetModificationType(wxSTC_MOD_INSERTTEXT | wxSTC_STARTACTION);
        event.SetEventObject(editor); editor->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(ordinary_edit, real_editor_text_event_is_gated_and_preserves_undo_granularity) {
    auto id = c->ass->Events.front().Id;
    Input(" 中 文 {\\i1} ");
    EXPECT_EQ(" 中 文 {\\i1} ", c->ass->Events.front().Text.get());
    EXPECT_EQ(id, c->ass->Events.front().Id);
    EXPECT_FALSE(c->subsController->IsUndoStackEmpty());
    auto before = c->ass->Events.front().Text.get();
    auto comparison = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    Input("blocked");
    EXPECT_EQ(before, c->ass->Events.front().Text.get());
    EXPECT_EQ(before, editor->GetTextRaw().data());
    comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("", c->ass->Events.front().Text.get());
    c->subsController->Redo(); EXPECT_EQ(before, c->ass->Events.front().Text.get());
}
TEST_F(ordinary_edit, real_metadata_and_millisecond_time_events_share_multiline_write_lifetime) {
    {
        auto write=c->subtitleAccess->TryWrite();
        auto other=new AssDialogue; c->ass->Events.push_back(*other);
        c->ass->Commit("insert fixture", AssFile::COMMIT_DIAG_ADDREM);
        c->selectionController->SetSelectedSet({&c->ass->Events.front(), other});
    }
    TimeEdit *start=nullptr; wxSpinCtrl *layer=nullptr; wxComboBox *actor=nullptr;
    for (auto child:box->GetChildren()) {
        if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="Start time") start=t;
        if (auto s=dynamic_cast<wxSpinCtrl*>(child)) layer=s;
        if (auto a=dynamic_cast<wxComboBox*>(child); a && a->GetToolTipText().StartsWith("Actor name")) actor=a;
    }
    ASSERT_NE(nullptr,start); ASSERT_NE(nullptr,layer); ASSERT_NE(nullptr,actor);
    auto send=[&](wxWindow* child, int value, wxString text={}) {
        if (auto combo=dynamic_cast<wxComboBox*>(child)) combo->ChangeValue(text);
        wxCommandEvent event(wxEVT_TEXT,child->GetId()); event.SetInt(value); event.SetString(text);
        event.SetEventObject(child); box->GetEventHandler()->ProcessEvent(event);
    };
    start->SetTime(1001); send(start,0);
    send(layer,7); send(actor,0,"actor");
    for(auto const& row:c->ass->Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    start->SetTime(1002); send(start,0); send(layer,8); send(actor,0,"blocked");
    for(auto const& row:c->ass->Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
}
TEST_F(ordinary_edit, grid_number_cell_direct_event_is_gated) {
    auto grid=new BaseGrid(window,c.get()); c->subsGrid=grid;
    { auto write=c->subtitleAccess->TryWrite(); c->ass->Commit("",AssFile::COMMIT_NEW); }
    wxFont font=*wxNORMAL_FONT; font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());
    auto face=FontFace("Subtitle/Grid"); if(!face.empty())font.SetFaceName(face);
    wxClientDC dc(grid); dc.SetFont(font);
    auto click=[&] {
        wxMouseEvent event(wxEVT_LEFT_DOWN); event.SetPosition({1,dc.GetCharHeight()+6});
        event.SetEventObject(grid); grid->GetEventHandler()->ProcessEvent(event);
    };
    click(); EXPECT_TRUE(c->ass->Events.front().Comment);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(); EXPECT_TRUE(c->ass->Events.front().Comment);
    comparison.reset(); click(); EXPECT_FALSE(c->ass->Events.front().Comment);
    c->subsGrid=nullptr;
}
}
