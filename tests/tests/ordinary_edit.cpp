// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <gtest/gtest.h>
#include <set>
#include "include/aegisub/context.h"
#include "ass_file.h"
#include "ass_dialogue.h"
#include "ass_attachment.h"
#include "ass_info.h"
#include "ass_style.h"
#include "subtitle_project_access.h"
#include "subtitle_row_list_edit.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "timeedit_ctrl.h"
#include "time_unit_toggle.h"
#include "flat_controls.h"
#include "base_grid.h"
#include "options.h"
#include "command/command.h"
#include "command/edit.h"
#include "subtitle_binding.h"
#include "search_replace_engine.h"
#include "text_selection_controller.h"
#include "dialog_style_editor.h"
#include "ass_exporter.h"
#include "dialogs.h"
#include "theme.h"
#include "command_palette.h"
#include "preferences.h"
#include "status_info.h"
#include <wx/checkbox.h>
#include "include/aegisub/toolbar.h"
#include "libresrc/libresrc.h"
#include "visual_tool_rectangle.h"
#include <wx/bmpbuttn.h>
#include <wx/menu.h>
#include "workspace_tabs.h"
#include "dialog_manager.h"
#include "compat.h"
#include "utils.h"
#include "audio_controller.h"
#include "audio_timing.h"
#include "audio_display.h"
#include "audio_box.h"
#include "audio_karaoke.h"
#include "audio_levels.h"
#include "audio_renderer_waveform.h"
#include "audio_marker.h"
#include "audio_colorscheme.h"
#include "audio_rendering_style.h"
#include "audio_edit_session.h"
#include "ass_karaoke.h"
#include "project.h"
#include "video_display.h"
#include "video_box.h"
#include "video_slider.h"
#include "video_time_label.h"
#include "video_zoom_control.h"
#include "style_edit.h"
#include "style_look.h"
#include "style_panel.h"
#include "video_controller.h"
#include "async_video_provider.h"
#include "video_frame.h"
#include "visual_tool_drag.h"
#include "visual_tool_cross.h"
#include "visual_tool_scale.h"
#include "visual_tool_rotatexy.h"
#include "visual_tool_rotatez.h"
#include "visual_tool_clip.h"
#include "visual_tool_vector_clip.h"
#include "visual_tool_rectangle.h"
#include "subtitle_format.h"
#include "subtitle_dialog_edit.h"
#include "dialog_rectangle.h"
#include "dialog_styling_assistant.h"
#include "dialog_translation.h"
#include "dialogs.h"
#include "../support/live_model_test_access.h"
#include <wx/combobox.h>
#include <wx/toolbar.h>
#include <wx/evtloop.h>
#include <thread>
#include <fstream>
#include <chrono>
#include <libaegisub/ass/karaoke.h>
#include <libaegisub/path.h>
#include <libaegisub/mru.h>
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <wx/dcclient.h>
#include <wx/modalhook.h>
#include <wx/msgdlg.h>
#include <wx/textctrl.h>
#include <wx/filedlg.h>
#include <wx/choicdlg.h>
#include <wx/listbox.h>
#include <wx/checklst.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/dataview.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <functional>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <filesystem>
#include <boost/scope_exit.hpp>

namespace {
class AcceptMessages : public wxModalDialogHook {
public:
    AcceptMessages() { Register(); }
    int Enter(wxDialog* dialog) override { return dynamic_cast<wxMessageDialog*>(dialog) ? wxID_OK : wxID_NONE; }
};
class RunModal : public wxModalDialogHook {
    wxString title;
    std::function<void(wxDialog*)> action;
public:
    RunModal(wxString title,std::function<void(wxDialog*)> action):title(title),action(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if(dialog->GetTitle()!=title || !action)return wxID_NONE;
        dialog->CallAfter([this,dialog] {
            action(dialog);
            if(dialog->IsModal() && dialog->IsShown())dialog->EndModal(wxID_CANCEL);
        });
        return wxID_NONE;
    }
};
std::vector<wxWindow*> descendants(wxWindow* parent) {
    std::vector<wxWindow*> found;
    for(auto child:parent->GetChildren()) {
        found.push_back(child); auto nested=descendants(child); found.insert(found.end(),nested.begin(),nested.end());
    }
    return found;
}
void button(wxWindow* window, int id) {
    wxCommandEvent event(wxEVT_BUTTON,id); event.SetEventObject(window); window->GetEventHandler()->ProcessEvent(event);
}
struct testing_comparison_page final : ComparisonPage {
    using ComparisonPage::ComparisonPage;
    bool TryClose(bool) override { return true; }
    void Started() override {}
};
class ordinary_edit : public testing::Test {
protected:
    std::unique_ptr<agi::Context> c;
    wxFrame *window = nullptr;
    WorkspaceTabs *tabs = nullptr;
    SubsEditBox *box = nullptr;
    SubsTextEditCtrl *editor = nullptr;
    void SetUp() override {
        c = std::make_unique<agi::Context>();
        window = new wxFrame(nullptr, wxID_ANY, "isolated editing tests");
        c->parent = window;
        tabs = new WorkspaceTabs(window, c.get());
        tabs->SetSize(1200, 800);
        c->workspaceTabs = tabs;
        tabs->SetEditor(new wxPanel(tabs->Book(), -1));
        box = new SubsEditBox(window, c.get());
        for (auto child : box->GetChildren())
            if (auto text = dynamic_cast<SubsTextEditCtrl*>(child)) editor = text;
        ASSERT_NE(nullptr, editor);
        auto write = c->subtitleAccess->TryWrite();
        LiveWriteForTest(*c).LoadDefault(); LiveWriteForTest(*c).Commit("", AssFile::COMMIT_NEW);
        auto line = &LiveRead(*c).Events.front();
        c->selectionController->SetSelectionAndActiveIDs({line->Id}, line->Id);
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
    auto id = LiveRead(*c).Events.front().Id;
    Input(" 中 文 {\\i1} ");
    EXPECT_EQ(" 中 文 {\\i1} ", LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(id, LiveRead(*c).Events.front().Id);
    EXPECT_FALSE(c->subsController->IsUndoStackEmpty());
    auto before = LiveRead(*c).Events.front().Text.get();
    auto comparison = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    Input("blocked");
    EXPECT_EQ(before, LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(before, editor->GetTextRaw().data());
    comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("", LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo(); EXPECT_EQ(before, LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, real_metadata_and_millisecond_time_events_share_multiline_write_lifetime) {
    {
        auto write=c->subtitleAccess->TryWrite();
        auto other=new AssDialogue; LiveWriteForTest(*c).Events.push_back(*other);
        LiveWriteForTest(*c).Commit("insert fixture", AssFile::COMMIT_DIAG_ADDREM);
        c->selectionController->SetSelectedIDs({(LiveRead(*c).Events.front()).Id,other->Id});
    }
    TimeEdit *start=nullptr; wxTextCtrl *layer=nullptr; wxTextCtrl *actor=nullptr;
    for (auto child:descendants(box)) {
        if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="Start time") start=t;
        else if (auto l=dynamic_cast<wxTextCtrl*>(child); l && l->GetToolTipText().StartsWith("Layer:")) layer=l;
        else if (auto a=dynamic_cast<wxTextCtrl*>(child); a && a->GetToolTipText().StartsWith("Actor name")) actor=a;
    }
    ASSERT_NE(nullptr,start); ASSERT_NE(nullptr,layer); ASSERT_NE(nullptr,actor);
    auto send=[&](wxWindow* child, int value, wxString text={}) {
        if (child==layer) {
            layer->ChangeValue(wxString::Format("%d",value));
            wxCommandEvent event(wxEVT_TEXT_ENTER,layer->GetId()); event.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(event);
            return;
        }
        if (child==actor) actor->ChangeValue(text);
        wxCommandEvent event(wxEVT_TEXT,child->GetId()); event.SetInt(value); event.SetString(text);
        event.SetEventObject(child); box->GetEventHandler()->ProcessEvent(event);
    };
    start->SetTime(1001); send(start,0);
    send(layer,7); send(actor,0,"actor");
    for(auto const& row:LiveRead(*c).Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    start->SetTime(1002); send(start,0); send(layer,8); send(actor,0,"blocked");
    for(auto const& row:LiveRead(*c).Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
}
TEST_F(ordinary_edit, grid_number_cell_direct_event_is_gated) {
    auto grid=new BaseGrid(window,c.get()); c->subsGrid=grid;
    { auto write=c->subtitleAccess->TryWrite(); LiveWriteForTest(*c).Commit("",AssFile::COMMIT_NEW); }
    auto click=[&] {
        wxMouseEvent event(wxEVT_LEFT_DOWN); event.SetPosition({1,grid->LineHeight()+2});
        event.SetEventObject(grid); grid->GetEventHandler()->ProcessEvent(event);
    };
    click(); EXPECT_TRUE(LiveRead(*c).Events.front().Comment);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(); EXPECT_TRUE(LiveRead(*c).Events.front().Comment);
    comparison.reset(); click(); EXPECT_FALSE(LiveRead(*c).Events.front().Comment);
    c->subsGrid=nullptr;
}
TEST_F(ordinary_edit, paste_insert_delete_sort_and_history_restore_ids_order_and_selection) {
    auto original=LiveRead(*c).Events.front().Id;
    cmd::PasteSubtitleLines(c.get(), "Dialogue: 0,0:00:02.001,0:00:03.002,Default,B,0,0,0,,second\nDialogue: 1,0:00:01.001,0:00:02.002,Default,A,0,0,0,,first");
    ASSERT_EQ(3u,LiveRead(*c).Events.size());
    std::vector<int> pasted_ids;
    for(auto const& row:LiveRead(*c).Events)pasted_ids.push_back(row.Id);
    EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
    EXPECT_EQ(pasted_ids.front(),c->selectionController->ReadActiveLine()->Id);
    c->subsController->Undo(); ASSERT_EQ(1u,LiveRead(*c).Events.size()); EXPECT_EQ(original,LiveRead(*c).Events.front().Id);
    c->subsController->Redo(); ASSERT_EQ(3u,LiveRead(*c).Events.size());
    std::vector<int> restored;for(auto const& row:LiveRead(*c).Events)restored.push_back(row.Id);
    EXPECT_EQ(pasted_ids,restored); EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
    cmd::call("grid/sort/start/selected",c.get());
    EXPECT_EQ("first",LiveRead(*c).Events.front().Text.get()); EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    cmd::call("edit/line/delete",c.get()); ASSERT_EQ(1u,LiveRead(*c).Events.size()); EXPECT_EQ(original,LiveRead(*c).Events.front().Id);
    c->subsController->Undo(); EXPECT_EQ(3u,LiveRead(*c).Events.size());
    cmd::call("subtitle/insert/before",c.get()); EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->subsController->Undo(); EXPECT_EQ(3u,LiveRead(*c).Events.size());
}
TEST_F(ordinary_edit, binding_and_extradata_survive_undo_redo_and_blocked_public_entry) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    BindSelectedSubtitles(c.get());
    auto group=GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()); ASSERT_FALSE(group.empty());
    auto extras=LiveRead(*c).Extradata.size();
    c->subsController->Undo(); EXPECT_TRUE(GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()).empty());
    c->subsController->Redo(); EXPECT_EQ(group,GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()));
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    UnbindSelectedSubtitles(c.get()); EXPECT_EQ(group,GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()));
    EXPECT_EQ(extras,LiveRead(*c).Extradata.size()); comparison.reset();
    UnbindSelectedSubtitles(c.get()); EXPECT_TRUE(GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()).empty());
}
TEST_F(ordinary_edit, nested_command_reuses_lease_and_commit_observers_cannot_edit_or_undo) {
    Input("keep");
    auto outer=c->subtitleAccess->TryWrite(); auto generation=c->subtitleEditGate.Revision().generation;
    cmd::call("edit/clear",c.get());
    EXPECT_EQ(generation+1,c->subtitleEditGate.Revision().generation); // Only Commit advanced it.
    bool visited=false;
    agi::signal::Connection observer=LiveRead(*c).AddCommitListener([&](int,const AssDialogue*) {
        visited=true; cmd::call("edit/clear",c.get()); c->subsController->Undo();
    });
    Input("notification"); EXPECT_TRUE(visited); EXPECT_EQ("notification",LiveRead(*c).Events.front().Text.get());
    outer.reset(); EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(ordinary_edit, undo_redo_reject_before_history_move_and_selection_notifications_cannot_reenter_history) {
    Input("one"); Input("two");
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto description=c->subsController->GetUndoDescription();
    c->subsController->Undo(); EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(description,c->subsController->GetUndoDescription()); comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get());
    comparison=c->subtitleEditGate.TryBeginComparison(); c->subsController->Redo();
    EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get()); comparison.reset();
    bool called=false;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&] {
        called=true; c->subsController->Undo();
    });
    c->subsController->Redo(); EXPECT_TRUE(called); EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, saved_modified_flag_tracks_ordinary_undo_redo_without_changing_save_policy) {
    Input("saved baseline");
    { auto write=c->subtitleAccess->TryWrite();c->subsController->Save(agi::fs::path("ordinary-saved.ass")); }
    EXPECT_FALSE(c->subsController->IsModified());
    cmd::call("edit/clear",c.get()); EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo(); EXPECT_FALSE(c->subsController->IsModified()); EXPECT_EQ("saved baseline",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo(); EXPECT_TRUE(c->subsController->IsModified());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    c->subsController->Undo();EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, single_replacement_uses_real_engine_and_comparison_blocks_replace_all) {
    Input("alpha alpha");
    SearchReplaceSettings settings{}; settings.find="alpha";settings.replace_with="beta";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::ALL;
    c->search->Configure(settings); c->textSelectionController->SetSelection(0,5);
    EXPECT_TRUE(c->search->ReplaceNext()); EXPECT_EQ("beta alpha",LiveRead(*c).Events.front().Text.get());
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    EXPECT_FALSE(c->search->ReplaceAll()); EXPECT_FALSE(c->search->ReplaceNext());
    EXPECT_EQ("beta alpha",LiveRead(*c).Events.front().Text.get());
    comparison.reset();
    AcceptMessages messages;
    EXPECT_TRUE(c->search->ReplaceAll()); EXPECT_EQ("beta beta",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo(); EXPECT_EQ("beta alpha",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, canonical_unicode_find_is_readonly_and_returns_original_byte_offsets) {
    std::string original="x e\xCC\x81 {\\i1} e\xCC\x81 y"; Input(original);
    SearchReplaceSettings settings{}; settings.find="\xC3\xA9";settings.replace_with="Z";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::ALL;
    settings.skip_tags=true; settings.match_case=true;
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto revision=c->subtitleEditGate.Revision(); auto matcher=SearchReplaceEngine::GetMatcher(settings);
    auto match=matcher(&LiveRead(*c).Events.front(),0); ASSERT_TRUE(match);
    EXPECT_EQ(2u,match.start); EXPECT_EQ(5u,match.end);
    auto second=matcher(&LiveRead(*c).Events.front(),match.end); ASSERT_TRUE(second);
    EXPECT_EQ("e\xCC\x81",original.substr(second.start,second.end-second.start));
    EXPECT_EQ(original,LiveRead(*c).Events.front().Text.get()); EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    comparison.reset(); c->search->Configure(settings); c->textSelectionController->SetSelection(2,5);
    EXPECT_TRUE(c->search->ReplaceNext()); EXPECT_EQ("x Z {\\i1} e\xCC\x81 y",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo(); EXPECT_EQ(original,LiveRead(*c).Events.front().Text.get());
    settings.use_regex=true;settings.find="(\xC3\xA9)";settings.replace_with="[$1]";
    c->search->Configure(settings); AcceptMessages messages;
    EXPECT_TRUE(c->search->ReplaceAll()); EXPECT_EQ("x [\xC3\xA9] {\\i1} [\xC3\xA9] y",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, real_style_editor_apply_is_gated_and_new_styles_keep_undo_redo) {
    AcceptMessages messages;
    wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,LiveRead(*c).GetStyle("Default"),c.get(),nullptr,"Copied",fonts);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    button(&dialog,wxID_APPLY); EXPECT_EQ(1u,LiveRead(*c).Styles.size()); EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Copied"));
    comparison.reset(); button(&dialog,wxID_APPLY);
    ASSERT_NE(nullptr,LiveRead(*c).GetStyle("Copied")); EXPECT_EQ(2u,LiveRead(*c).Styles.size());
    auto style=LiveRead(*c).GetStyle("Copied");
    button(&dialog,wxID_APPLY); EXPECT_EQ(style,LiveRead(*c).GetStyle("Copied")); // Apply again updates, no duplicate.
    c->subsController->Undo(); EXPECT_EQ(2u,LiveRead(*c).Styles.size());
    c->subsController->Undo(); EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Copied"));
    c->subsController->Redo(); EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Copied"));
}
TEST_F(ordinary_edit, style_editor_rejects_stale_live_style_after_another_edit) {
    AcceptMessages messages; wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,LiveRead(*c).GetStyle("Default"),c.get(),nullptr,"",fonts);
    Input("changed elsewhere"); auto revision=c->subsController->GetUndoDescription();
    button(&dialog,wxID_APPLY);
    EXPECT_EQ(revision,c->subsController->GetUndoDescription()); EXPECT_EQ(1u,LiveRead(*c).Styles.size());
}
TEST_F(ordinary_edit, script_properties_event_rejects_comparison_and_preserves_undo) {
    auto original=std::string(LiveRead(*c).GetScriptInfo("Title"));
    RunModal hook("Script Properties",[&](wxDialog* dialog) {
        wxTextCtrl* title=nullptr;
        for(auto child:descendants(dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child)){title=text;break;}
        ASSERT_NE(nullptr,title);title->ChangeValue("isolated title");button(dialog,wxID_OK);
    });
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    ShowPropertiesDialog(c.get());EXPECT_EQ(original,LiveRead(*c).GetScriptInfo("Title"));comparison.reset();
    ShowPropertiesDialog(c.get());EXPECT_EQ("isolated title",LiveRead(*c).GetScriptInfo("Title"));
    c->subsController->Undo();EXPECT_EQ(original,LiveRead(*c).GetScriptInfo("Title"));
}
TEST_F(ordinary_edit, attachment_delete_event_is_gated_and_undo_restores_content) {
    std::ofstream data("attachment.ttf",std::ios::binary);data << "attachment fixture";data.close();
    { auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).InsertAttachment(agi::fs::path("attachment.ttf"));LiveWriteForTest(*c).Commit("attachment",AssFile::COMMIT_ATTACHMENT); }
    auto original=LiveRead(*c).Attachments.front().GetEntryData();
    RunModal hook("Attachment List",[&](wxDialog* dialog) {
        wxListView* list=nullptr;wxButton* remove=nullptr;
        for(auto child:descendants(dialog)) {
            if(auto view=dynamic_cast<wxListView*>(child))list=view;
            if(auto b=dynamic_cast<wxButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
        }
        ASSERT_NE(nullptr,list);ASSERT_NE(nullptr,remove);list->Select(0);
        wxCommandEvent event(wxEVT_BUTTON,remove->GetId());event.SetEventObject(remove);remove->GetEventHandler()->ProcessEvent(event);
    });
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    ShowAttachmentsDialog(c.get());EXPECT_EQ(1u,LiveRead(*c).Attachments.size());comparison.reset();
    ShowAttachmentsDialog(c.get());EXPECT_TRUE(LiveRead(*c).Attachments.empty());
    c->subsController->Undo();ASSERT_EQ(1u,LiveRead(*c).Attachments.size());EXPECT_EQ(original,LiveRead(*c).Attachments.front().GetEntryData());
}
TEST_F(ordinary_edit, real_style_import_and_delete_preserve_unrelated_data_and_history) {
    class ImportDialogs : public wxModalDialogHook {
    public:
        wxString path;
        int files=0;
        explicit ImportDialogs(wxString path):path(path) { Register(); }
        int Enter(wxDialog* dialog) override {
            if(auto file=dynamic_cast<wxFileDialog*>(dialog)) { ++files; file->SetPath(path); return wxID_OK; }
            if(auto choices=dynamic_cast<wxMultiChoiceDialog*>(dialog)) { wxArrayInt selected;selected.Add(0);selected.Add(1);choices->SetSelections(selected);choices->TransferDataFromWindow(); return wxID_OK; }
            if(dynamic_cast<wxMessageDialog*>(dialog)) return wxID_YES;
            return wxID_NONE;
        }
    };
    AssStyle imported; imported.name="Imported";imported.fontsize=72;imported.UpdateData();
    AssStyle replaced; replaced.fontsize=70;replaced.UpdateData();
    std::ofstream data("styles.ass",std::ios::binary);
    data << "[Script Info]\nScriptType: v4.00+\n[V4+ Styles]\n" << replaced.GetEntryData() << '\n' << imported.GetEntryData() << '\n'; data.close();
    std::ofstream attachment("attachment.ttf",std::ios::binary);attachment << "fixture attachment";attachment.close();
    {
        auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).InsertAttachment(agi::fs::path("attachment.ttf"));
        auto extra=LiveWriteForTest(*c).AddExtradata("opaque","retained"); LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{extra};
        LiveWriteForTest(*c).Commit("fixture metadata",AssFile::COMMIT_ATTACHMENT|AssFile::COMMIT_EXTRADATA);
    }
    ShowStyleManagerDialog(c.get());
    wxDialog* manager=nullptr;
    for(auto top:wxTopLevelWindows) if(auto d=dynamic_cast<wxDialog*>(top);d && d->GetTitle()=="Styles Manager")manager=d;
    ASSERT_NE(nullptr,manager); manager->Hide();
    auto cleanup=std::unique_ptr<wxDialog,std::function<void(wxDialog*)>>(manager,[](wxDialog* d){d->Close();});
    wxButton *import=nullptr,*remove=nullptr;wxListBox *current=nullptr;wxWindow *down=nullptr,*sort=nullptr;
    for(auto child:descendants(manager)) {
        if(auto b=dynamic_cast<wxButton*>(child); b && b->GetLabel()=="&Import from script...")import=b;
        if(child->GetParent()->GetLabel()=="Current script") {
            if(auto b=dynamic_cast<wxButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
            if(auto list=dynamic_cast<wxListBox*>(child))current=list;
            if(child->GetToolTipText()=="Move style down")down=child;
            if(child->GetToolTipText()=="Sort styles alphabetically")sort=child;
        }
    }
    ASSERT_NE(nullptr,import); ASSERT_NE(nullptr,remove); ASSERT_NE(nullptr,current);
    ASSERT_NE(nullptr,down);ASSERT_NE(nullptr,sort);
    auto path=std::filesystem::absolute("styles.ass").wstring(); ImportDialogs dialogs{wxString(path)};
    auto click=[](wxWindow* control) { wxCommandEvent event(wxEVT_BUTTON,control->GetId());event.SetEventObject(control);control->GetEventHandler()->ProcessEvent(event); };
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(import);EXPECT_EQ(0,dialogs.files);EXPECT_EQ(1u,LiveRead(*c).Styles.size());comparison.reset();
    click(import);ASSERT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));EXPECT_EQ(72.,LiveRead(*c).GetStyle("Imported")->fontsize);
    EXPECT_EQ(70.,LiveRead(*c).GetStyle("Default")->fontsize);EXPECT_EQ(1,dialogs.files);
    EXPECT_EQ(1u,LiveRead(*c).Attachments.size());EXPECT_EQ("retained",LiveRead(*c).Extradata.front().value);
    current->DeselectAll();current->SetStringSelection("Default");click(down);
    EXPECT_EQ("Imported",LiveRead(*c).Styles.front().name);c->subsController->Undo();
    EXPECT_EQ("Default",LiveRead(*c).Styles.front().name);click(sort);c->subsController->Undo();
    current->DeselectAll();current->SetStringSelection("Imported");
    comparison=c->subtitleEditGate.TryBeginComparison();click(remove);EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));comparison.reset();
    click(remove);EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Imported"));
    c->subsController->Undo();EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));
    c->subsController->Undo();EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Imported"));EXPECT_EQ(48.,LiveRead(*c).GetStyle("Default")->fontsize);
    c->subsController->Redo();EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));
    EXPECT_EQ(1u,LiveRead(*c).Attachments.size());EXPECT_EQ("retained",LiveRead(*c).Extradata.front().value);
}

TEST_F(ordinary_edit, all_migrated_command_operators_reject_comparison_before_any_model_or_history_change) {
    Input("unchanged");
    auto id=LiveRead(*c).Events.front().Id;
    auto description=c->subsController->GetUndoDescription();
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    char const* commands[] = {
        "tool/resampleres",
        "edit/color/primary",
        "edit/color/secondary",
        "edit/color/outline",
        "edit/color/shadow",
        "edit/style/bold",
        "edit/style/italic",
        "edit/style/underline",
        "edit/style/strikeout",
        "edit/font",
        "edit/line/cut",
        "edit/line/delete",
        "edit/line/duplicate",
        "edit/line/split/after",
        "edit/line/split/before",
        "edit/line/join/as_karaoke",
        "edit/line/join/concatenate",
        "edit/line/join/keep_first",
        "edit/line/paste",
        "edit/line/paste/over",
        "edit/line/recombine",
        "edit/line/split/by_karaoke",
        "edit/line/split/estimate",
        "edit/line/split/preserve",
        "edit/line/split/video",
        "edit/redo",
        "edit/undo",
        "edit/line/reverse_move",
        "subtitle/bind",
        "subtitle/unbind",
        "edit/revert",
        "edit/clear",
        "edit/clear/text",
        "edit/insert_original",
        "grid/line/next/create",
        "grid/sort/actor",
        "grid/sort/actor/selected",
        "grid/sort/effect",
        "grid/sort/effect/selected",
        "grid/sort/end",
        "grid/sort/end/selected",
        "grid/sort/layer",
        "grid/sort/layer/selected",
        "grid/sort/start",
        "grid/sort/start/selected",
        "grid/sort/style",
        "grid/sort/style/selected",
        "grid/move/up",
        "grid/move/down",
        "grid/swap",
        "subtitle/insert/after",
        "subtitle/insert/after/videotime",
        "subtitle/insert/before",
        "subtitle/insert/before/videotime",
        "time/continuous/end",
        "time/continuous/start",
        "time/frame/current",
        "time/snap/end_video",
        "time/snap/scene",
        "time/lead/both",
        "time/lead/in",
        "time/lead/out",
        "time/length/increase",
        "time/length/increase/shift",
        "time/length/decrease",
        "time/length/decrease/shift",
        "time/start/increase",
        "time/start/decrease",
        "time/snap/start_video",
        "time/next",
        "time/prev",
    };
    for(auto name:commands) {
        SCOPED_TRACE(name); (*cmd::get(name))(c.get());
        EXPECT_EQ(1u,LiveRead(*c).Events.size()); EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
        EXPECT_EQ("unchanged",LiveRead(*c).Events.front().Text.get());
        EXPECT_EQ(description,c->subsController->GetUndoDescription());
        EXPECT_EQ(id,c->selectionController->ReadActiveLine()->Id);
    }
    cmd::PasteSubtitleLines(c.get(),"blocked\nblocked"); EXPECT_EQ(1u,LiveRead(*c).Events.size());
}

TEST_F(ordinary_edit, command_palette_matches_word_starts_runs_and_pinyin) {
    auto keys = palette::MakeKeys(L"Open Subtitles...", "subtitle/open");
    auto match = palette::BestMatch(palette::NormaliseQuery(L"O S"), keys);
    ASSERT_TRUE(match);
    EXPECT_EQ((std::vector<size_t>{0, 5}), match->positions) << "the s that starts a word";
    EXPECT_FALSE(palette::BestMatch(L"xyz", keys));
    auto run = palette::BestMatch(L"sub", keys), scattered = palette::BestMatch(L"sbs", keys);
    ASSERT_TRUE(run && scattered);
    EXPECT_GT(run->score, scattered->score);
    auto by_id = palette::BestMatch(L"subtitleopen", keys);
    ASSERT_TRUE(by_id);
    EXPECT_TRUE(by_id->positions.empty()) << "the command id is searched but not drawn";

    ASSERT_EQ(L"zi", palette::Pinyin(L'\u5b57')) << "ICU's Han-Latin transliterator";
    auto chinese = palette::MakeKeys(L"\u6253\u5f00\u5b57\u5e55...", "subtitle/open"); // 打开字幕
    auto initials = palette::BestMatch(L"dkzm", chinese);
    ASSERT_TRUE(initials);
    EXPECT_EQ((std::vector<size_t>{0, 1, 2, 3}), initials->positions);
    auto syllables = palette::BestMatch(L"zimu", chinese);
    ASSERT_TRUE(syllables);
    EXPECT_EQ((std::vector<size_t>{2, 3}), syllables->positions);
    auto characters = palette::BestMatch(palette::NormaliseQuery(L"\u5b57\u5e55"), chinese);
    ASSERT_TRUE(characters);
    EXPECT_EQ((std::vector<size_t>{2, 3}), characters->positions);
}

TEST_F(ordinary_edit, command_palette_lists_commands_remembers_choices_and_runs_after_closing) {
    OPT_SET("App/Command Palette/Recent")->SetListString({});
    {
        CommandPalette palette(c.get());
        auto all = palette.Shown();
        EXPECT_GT(all.size(), 200u);
        EXPECT_EQ(all.end(), std::find(all.begin(), all.end(), "app/command_palette"));
        EXPECT_TRUE(std::none_of(all.begin(), all.end(), [](auto const& name) { return name.starts_with("recent/"); }));
        auto exit = std::find(all.begin(), all.end(), "app/exit");
        ASSERT_NE(all.end(), exit);
        EXPECT_EQ("Ctrl-Q", palette.ShownItem(exit - all.begin()).hotkey);

        palette.SetQuery("copy coordinates");
        ASSERT_FALSE(palette.Shown().empty());
        EXPECT_EQ("video/copy_coordinates", palette.Shown()[0]);
        EXPECT_FALSE(palette.ShownItem(0).enabled) << "no video is open";
        EXPECT_FALSE(palette.Accept());
        EXPECT_TRUE(palette.Chosen().empty());

        palette.SetQuery("hide tags");
        ASSERT_FALSE(palette.Shown().empty());
        EXPECT_EQ("grid/tags/hide", palette.Shown()[0]);
        palette.SetQuery("qqqqzzzz");
        EXPECT_TRUE(palette.Shown().empty());
        EXPECT_FALSE(palette.Accept());
    }

    // Through the command: the palette closes, then the chosen command runs.
    OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(0);
    // The query is typed before the palette is first drawn: the fixture has
    // no video display, which some commands' state needs (the app always has one).
    struct Choose : wxModalDialogHook {
        Choose() { Register(); }
        int Enter(wxDialog *dialog) override {
            auto palette = dynamic_cast<CommandPalette*>(dialog);
            if (!palette) return wxID_NONE;
            palette->SetQuery("grid tags hide");
            palette->CallAfter([palette] { EXPECT_TRUE(palette->Accept()); });
            return wxID_NONE;
        }
        void Exit(wxDialog *) override {}
    } choose;
    cmd::call("app/command_palette", c.get());
    EXPECT_EQ(2, OPT_GET("Subtitle/Grid/Hide Overrides")->GetInt());
    EXPECT_EQ("grid/tags/hide", OPT_GET("App/Command Palette/Recent")->GetListString().at(0));

    // Recent first when nothing is typed; a setting that is on is marked.
    CommandPalette::Remember("app/exit");
    CommandPalette again(c.get());
    auto order = again.Shown();
    ASSERT_GE(order.size(), 2u);
    EXPECT_EQ("app/exit", order[0]);
    EXPECT_EQ("grid/tags/hide", order[1]);
    EXPECT_TRUE(again.ShownItem(1).checkable && again.ShownItem(1).checked);

    // While the comparison tab is in front only application commands run.
    auto page = new testing_comparison_page(tabs->Book());
    tabs->OpenComparison(page, "other.ass");
    CommandPalette locked(c.get());
    locked.SetQuery("hide tags");
    ASSERT_FALSE(locked.Shown().empty());
    EXPECT_FALSE(locked.ShownItem(0).enabled);
    locked.SetQuery("app/exit");
    ASSERT_FALSE(locked.Shown().empty());
    EXPECT_TRUE(locked.ShownItem(0).enabled);
    tabs->ComparisonEnded(false);
}

double MeanLightness(wxImage const& image, bool *any_dark_inside = nullptr) {
    double total = 0, sum = 0;
    auto alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
    int w = image.GetWidth(), h = image.GetHeight();
    if (any_dark_inside) *any_dark_inside = false;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int i = y * w + x, a = alpha ? alpha[i] : 255;
        if (a < 128) continue;
        auto rgb = image.GetData() + i * 3;
        double l = (std::max({rgb[0], rgb[1], rgb[2]}) + std::min({rgb[0], rgb[1], rgb[2]})) / 510.0;
        total += 1; sum += l;
        if (any_dark_inside && l < 0.25 && x > 0 && y > 0 && x < w - 1 && y < h - 1) {
            bool inside = true;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (alpha && alpha[(y + dy) * w + x + dx] < 128) inside = false;
            if (inside) *any_dark_inside = true;
        }
    }
    return total ? sum / total : 0;
}

wxImage IconImage(wxBitmapBundle const& bundle, int size) {
    return bundle.GetBitmap(wxSize(size, size)).ConvertToImage();
}

TEST_F(ordinary_edit, dark_appearance_lightens_dark_icons_and_keeps_coloured_ones) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Theme")->SetInt(1); OPT_SET("App/Classic Icons")->SetBool(false); };
    OPT_SET("App/Theme")->SetInt(1);
    OPT_SET("App/Classic Icons")->SetBool(true); // The coloured bitmaps
    theme::Init();
    ASSERT_FALSE(libresrc_dark());
    // Only Windows with wxWidgets 3.3 draws its own controls dark; elsewhere
    // borders are left to the toolkit's theme.
#if !defined(__WXMSW__)
    EXPECT_FALSE(theme::NativeDark());
#endif
    if (!theme::NativeDark()) EXPECT_EQ(long(wxBORDER_SUNKEN), theme::Border(wxBORDER_SUNKEN));
    auto bold_light = IconImage(cmd::get("edit/style/bold")->Icon(16), 16);
    auto save_light = IconImage(cmd::get("subtitle/save")->Icon(24), 24);
    EXPECT_LT(MeanLightness(bold_light), 0.3);

    OPT_SET("App/Theme")->SetInt(2);
    ASSERT_TRUE(libresrc_dark()) << "the appearance reaches the icon loader before windows are told";
    auto bold_dark = IconImage(cmd::get("edit/style/bold")->Icon(16), 16);
    EXPECT_GT(MeanLightness(bold_dark), 0.6) << "a dark glyph becomes light";

    // A coloured icon keeps its colours: only near-black parts at its edge change.
    auto save_dark = IconImage(cmd::get("subtitle/save")->Icon(24), 24);
    ASSERT_EQ(save_light.GetSize(), save_dark.GetSize());
    int changed = 0, opaque = 0;
    for (int i = 0; i < save_light.GetWidth() * save_light.GetHeight(); ++i) {
        if (save_light.GetAlpha()[i] < 128) continue;
        ++opaque;
        if (memcmp(save_light.GetData() + i * 3, save_dark.GetData() + i * 3, 3)) ++changed;
    }
    EXPECT_LT(changed, opaque / 3);

    // A black mark drawn on a light part (the plus on the zoom lens) keeps its contrast.
    bool dark_inside = false;
    MeanLightness(IconImage(cmd::get("video/zoom/in")->Icon(24), 24), &dark_inside);
    EXPECT_TRUE(dark_inside);

    // The rule itself, on a black square with a black dot inside a white one.
    wxImage glyph(8, 8); glyph.SetAlpha(); memset(glyph.GetData(), 0, 8 * 8 * 3); memset(glyph.GetAlpha(), 255, 64);
    EXPECT_TRUE(libresrc_is_glyph(glyph));
    libresrc_adapt_for_dark(glyph, true);
    EXPECT_GT(MeanLightness(glyph), 0.8);
    wxImage marked(8, 8); marked.SetAlpha(); memset(marked.GetData(), 255, 8 * 8 * 3); memset(marked.GetAlpha(), 255, 64);
    memset(marked.GetAlpha(), 0, 8); // a transparent top row
    for (int x = 0; x < 8; ++x) { auto p = marked.GetData() + (8 + x) * 3; p[0] = p[1] = p[2] = 0; } // black outline under it
    auto dot = marked.GetData() + (4 * 8 + 4) * 3; dot[0] = dot[1] = dot[2] = 0;
    EXPECT_FALSE(libresrc_is_glyph(marked));
    libresrc_adapt_for_dark(marked, false);
    EXPECT_GT(marked.GetData()[(8 + 3) * 3], 200) << "the black edge is lightened";
    EXPECT_EQ(0, marked.GetData()[(4 * 8 + 4) * 3]) << "the dot inside is kept";

    // The shape tool icon follows at once.
    OPT_SET("App/Theme")->SetInt(1);
    auto shape = DrawingShapeIcon(DrawingShape::Triangle, 24).GetBitmap(wxSize(48, 48)).ConvertToImage();
    ASSERT_TRUE(shape.HasAlpha());
    EXPECT_EQ(48, shape.GetWidth());
    EXPECT_EQ(0, shape.GetAlpha()[0]) << "transparent around the shape";
    EXPECT_GT(shape.GetAlpha()[24 * 48 + 24], 200);
}

TEST_F(ordinary_edit, icons_are_monochrome_in_the_appearance_colour_unless_classic) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Theme")->SetInt(1); OPT_SET("App/Classic Icons")->SetBool(false); };
    OPT_SET("App/Theme")->SetInt(1);
    OPT_SET("App/Classic Icons")->SetBool(false);
    theme::Init();
    ASSERT_FALSE(libresrc_classic());

    // Every toolbar's commands have one, and so do the sub-tools and shapes.
    for (auto name : {"visual_move_conv_move", "visual_move_conv_pos", "shape_rectangle", "shape_ellipse",
                      "shape_triangle", "shape_polygon", "visual_vector_clip_freehand_smooth"})
        EXPECT_NE(nullptr, libresrc_modern_icon(name)) << name;
    // And every other bitmap: dialogs, the edit box, menus
    for (auto name : {"eyedropper_tool", "button_bold", "button_color_four", "arrow_sort", "kara_split_accept",
                      "save_timecodes_menu", "use_dummy_video_menu", "website_button", "toggle_audio_link"})
        EXPECT_NE(nullptr, libresrc_modern_icon(name)) << name;
    EXPECT_EQ(nullptr, libresrc_modern_icon("wxicon")) << "the application's own icon stays";
    EXPECT_EQ(nullptr, libresrc_modern_icon("visual_rectang")) << "exact names only";

    // One colour: dark grey on light, light grey on dark, at any size.
    auto colours = [](wxImage image) {
        if (!image.HasAlpha()) image.InitAlpha();
        std::set<std::tuple<int, int, int>> found;
        const unsigned char *rgb = image.GetData(), *alpha = image.GetAlpha();
        for (int i = 0, n = image.GetWidth() * image.GetHeight(); i < n; ++i)
            // To the nearest 8: antialiased edges are a unit or two off
            if (alpha[i] > 200) found.emplace((rgb[i * 3] + 4) / 8 * 8, (rgb[i * 3 + 1] + 4) / 8 * 8, (rgb[i * 3 + 2] + 4) / 8 * 8);
        return found;
    };
    for (auto name : {"subtitle/save", "audio/play/selection", "audio/opt/autocommit", "video/play",
                      "video/tool/drag", "video/tool/vclip/line", "help/video", "app/options", "time/snap/scene",
                      "edit/style/bold", "edit/undo", "subtitle/find", "audio/close", "keyframe/save"})
        EXPECT_EQ(1u, colours(IconImage(cmd::get(name)->Icon(16), 16)).size()) << name;
    for (int size : {16, 24, 32}) {
        auto save = IconImage(cmd::get("subtitle/save")->Icon(size), size);
        ASSERT_TRUE(save.HasAlpha());
        EXPECT_EQ(size, save.GetWidth());
        EXPECT_EQ(0, save.GetAlpha()[0]) << "transparent around the drawing";
        auto found = colours(save);
        ASSERT_EQ(1u, found.size()) << size;
        EXPECT_EQ(std::make_tuple(0x28, 0x28, 0x28), *found.begin());
    }
    OPT_SET("App/Theme")->SetInt(2);
    auto dark = colours(IconImage(cmd::get("subtitle/save")->Icon(24), 24));
    ASSERT_EQ(1u, dark.size());
    EXPECT_EQ(std::make_tuple(0xd8, 0xd8, 0xd8), *dark.begin());
    OPT_SET("App/Theme")->SetInt(1);

    // Hinted to the pixel grid: a whole number of pixels for the stroke, so
    // lines are solid pixels rather than two half-covered columns.
    auto partial = [](wxImage const& image) {
        int on = 0, soft = 0;
        for (int i = 0; i < image.GetWidth() * image.GetHeight(); ++i) {
            int a = image.GetAlpha()[i];
            if (a > 30) ++on;
            if (a > 30 && a < 225) ++soft;
        }
        return double(soft) / on;
    };
    for (int size : {16, 20, 24, 32})
        EXPECT_LT(partial(IconImage(cmd::get("subtitle/save")->Icon(size), size)), size == 16 ? 0.25 : 0.2) << size << " (unhinted, about half)";
    auto body = libresrc_modern_icon("save_toolbutton");
    // On Lucide's 24 grid: 1 pixel at 16 is 1.5 units
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 16).find("stroke-width='1.5'"));
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 16).find("V18.75A2 2 0 0 1 18.75 21.75H5.25")) << "on pixel centres";
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 20).find("stroke-width='2.4'")) << "2 pixels";
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 32).find("stroke-width='2.25'")) << "3 pixels";
    // Lucide's dots (zero-length lines it draws round) are small squares of pixels at 16
    EXPECT_NE(std::string::npos, libresrc_modern_svg(libresrc_modern_icon("about_menu"), 16).find("<rect")) << "the i's dot";

    // Right to left, the drawing is mirrored.
    auto next = IconImage(cmd::get("time/next")->Icon(32), 32);
    auto next_rtl = IconImage(cmd::get("time/next")->Icon(32, wxLayout_RightToLeft), 32);
    // Compared as shapes: antialiasing differs a little either way round
    auto shape_of = [](wxImage const& image) {
        std::string mask;
        for (int i = 0; i < image.GetWidth() * image.GetHeight(); ++i) mask += image.GetAlpha()[i] > 128 ? '#' : '.';
        return mask;
    };
    EXPECT_EQ(shape_of(next.Mirror()), shape_of(next_rtl));
    EXPECT_NE(shape_of(next), shape_of(next_rtl)) << "not symmetric";

    // The shape tool's icon has the flyout corner.
    auto shape = DrawingShapeIcon(DrawingShape::Ellipse, 32).GetBitmap(wxSize(32, 32)).ConvertToImage();
    EXPECT_GT(shape.GetAlpha()[29 * 32 + 29], 200);

    // Classic: the coloured bitmaps again, and toolbars are rebuilt with them.
    auto bar = toolbar::GetToolbar(window, "main", c.get(), "Default");
    auto first_icon = [&] { return bar->GetToolByPos(0)->GetNormalBitmap().ConvertToImage(); };
    EXPECT_EQ(1u, colours(first_icon()).size());
    OPT_SET("App/Classic Icons")->SetBool(true);
    EXPECT_TRUE(libresrc_classic());
    EXPECT_GT(colours(first_icon()).size(), 1u) << "the bitmap's own colours";
    OPT_SET("App/Classic Icons")->SetBool(false);
    EXPECT_EQ(1u, colours(first_icon()).size());
    bar->Destroy();
}

TEST_F(ordinary_edit, main_toolbar_groups_dialogs_in_dropdowns_and_has_a_classic_layout) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Classic Toolbar")->SetBool(false); };
    OPT_SET("App/Classic Toolbar")->SetBool(false);
    OPT_SET("App/Theme")->SetInt(1);
    theme::Init();
    auto bar = toolbar::GetToolbar(window, "main", c.get(), "Default");
    auto buttons = [&] {
        std::vector<wxToolBarToolBase const*> found;
        for (size_t i = 0; i < bar->GetToolsCount(); ++i)
            if (auto tool = bar->GetToolByPos(i); !tool->IsSeparator()) found.push_back(tool);
        return found;
    };
    auto grouped = buttons();
    EXPECT_EQ(13u, grouped.size()) << "zoom in and out live on the zoom control below the video";
    int dropdowns = 0;
    wxToolBarToolBase const *jump = nullptr;
    size_t menu_entries = 0;
    for (auto tool : grouped)
        if (tool->GetKind() == wxITEM_DROPDOWN) {
            ++dropdowns;
            ASSERT_NE(nullptr, tool->GetDropdownMenu());
            menu_entries += tool->GetDropdownMenu()->GetMenuItemCount();
            if (tool->GetLabel() == cmd::get("video/jump")->StrDisplay(c.get())) jump = tool;
        }
    EXPECT_EQ(3, dropdowns);
    EXPECT_EQ(15u, menu_entries) << "every grouped command, the button's own one first";
    ASSERT_NE(nullptr, jump);
    auto jump_menu = jump->GetDropdownMenu();
    ASSERT_EQ(3u, jump_menu->GetMenuItemCount());
    EXPECT_EQ(cmd::get("video/jump/start")->StrDisplay(c.get()), jump_menu->FindItemByPosition(1)->GetItemLabelText());

    // Opening a dropdown enables its entries as a menu would: no video is open.
    for (auto item : jump_menu->GetMenuItems()) item->Enable(true);
    wxCommandEvent open(wxEVT_TOOL_DROPDOWN, jump->GetId());
    open.SetEventObject(bar);
    bar->GetEventHandler()->ProcessEvent(open);
    EXPECT_FALSE(jump_menu->FindItemByPosition(1)->IsEnabled());

    // The classic layout: every command a button of its own.
    OPT_SET("App/Classic Toolbar")->SetBool(true);
    auto classic = buttons();
    EXPECT_EQ(27u, classic.size());
    EXPECT_TRUE(std::none_of(classic.begin(), classic.end(), [](auto tool) { return tool->GetKind() == wxITEM_DROPDOWN; }));

    // The appearance rebuilds it with icons for the new background.
    OPT_SET("App/Theme")->SetInt(2);
    EXPECT_EQ(27u, buttons().size());
    OPT_SET("App/Theme")->SetInt(1);
    OPT_SET("App/Classic Toolbar")->SetBool(false);
    EXPECT_EQ(13u, buttons().size());
    bar->Destroy();
}

TEST_F(ordinary_edit, preferences_search_highlights_labels_and_shows_a_matching_page) {
    OPT_SET("Tool/Preferences/Page")->SetInt(0);
    Preferences prefs(window);
    ASSERT_EQ(0u, prefs.CurrentPage());

    prefs.Search("undo levels");
    ASSERT_FALSE(prefs.MatchingPages().empty());
    EXPECT_EQ(0u, prefs.MatchingPages().front());
    EXPECT_EQ(0u, prefs.CurrentPage()) << "the page shown has a match: stay";
    auto controls = prefs.MatchingControls(0);
    ASSERT_EQ(1u, controls.size());
    EXPECT_EQ("Undo Levels", controls[0]->GetLabel());
    EXPECT_EQ(wxFONTWEIGHT_BOLD, controls[0]->GetFont().GetWeight());
    EXPECT_FALSE(prefs.SearchStatus().empty());

    // Words in any case and without spaces; the shown page moves to a match.
    prefs.Search("MOUSE WHEEL");
    ASSERT_FALSE(prefs.MatchingPages().empty());
    EXPECT_NE(0u, prefs.CurrentPage());
    EXPECT_EQ(prefs.MatchingPages().front(), prefs.CurrentPage());
    EXPECT_EQ(wxFONTWEIGHT_NORMAL, controls[0]->GetFont().GetWeight()) << "no longer matched";

    // Page names count: the hotkeys page has no labels of its own.
    prefs.Search("hotkeys");
    EXPECT_FALSE(prefs.MatchingPages().empty());

    prefs.Search("qqqzzzxx");
    EXPECT_TRUE(prefs.MatchingPages().empty());
    EXPECT_EQ(_("No matching settings"), prefs.SearchStatus());

    prefs.Search("");
    EXPECT_TRUE(prefs.MatchingPages().empty());
    EXPECT_TRUE(prefs.SearchStatus().empty());
    for (size_t page = 0; page < 20; ++page) EXPECT_TRUE(prefs.MatchingControls(page).empty());
}

TEST_F(ordinary_edit, status_bar_shows_the_current_line_and_the_file_state) {
    std::vector<int> ids;
    {
        auto write = c->subtitleAccess->TryWrite();
        auto& file = LiveWriteForTest(*c);
        for (int i = 0; i < 2; ++i) {
            auto line = new AssDialogue;
            line->Start = 1000 * (i + 1); line->End = 1000 * (i + 2);
            file.Events.push_back(*line);
        }
        file.Commit("", AssFile::COMMIT_DIAG_ADDREM);
    }
    for (auto const& line : LiveRead(*c).Events) ids.push_back(line.Id);
    ASSERT_EQ(3u, ids.size());

    c->selectionController->SetSelectionAndActiveIDs({ids[1]}, ids[1]);
    EXPECT_EQ(wxString::FromUTF8("Line 2 of 3 · 0:00:01.00 → 0:00:02.00"), status_info::Line(c.get()));
    c->selectionController->SetSelectionAndActiveIDs({ids[1], ids[2]}, ids[2]);
    EXPECT_EQ(wxString::FromUTF8("Line 3 of 3 (2 selected) · 0:00:02.00 → 0:00:03.00"), status_info::Line(c.get()));
    c->selectionController->SetSelectionAndActiveIDs({}, 0);
    EXPECT_TRUE(status_info::Line(c.get()).empty());

    EXPECT_EQ(c->subsController->IsModified() ? _("Unsaved changes") : _("No unsaved changes"), status_info::File(c.get()));

    // The tabs announce a comparison opening and ending.
    int changes = 0;
    auto connection = tabs->AddChangeListener([&] { ++changes; });
    auto page = new testing_comparison_page(tabs->Book());
    tabs->OpenComparison(page, "other.ass");
    EXPECT_GT(changes, 0);
    EXPECT_EQ("Comparing with other.ass", status_info::File(c.get()));
    changes = 0;
    tabs->ComparisonEnded(false);
    EXPECT_GT(changes, 0);
    EXPECT_NE("Comparing with other.ass", status_info::File(c.get()));
}

TEST_F(ordinary_edit, visual_tools_sit_to_the_right_of_the_video) {
    for (bool detached : {false, true}) {
        auto video_box = new VideoBox(window, detached, c.get());
        VideoDisplay *display = nullptr;
        std::vector<wxToolBar*> bars;
        std::function<void(wxWindow*)> find = [&](wxWindow *w) {
            for (auto child : w->GetChildren()) {
                if (auto d = dynamic_cast<VideoDisplay*>(child)) display = d;
                if (auto bar = dynamic_cast<wxToolBar*>(child); bar && bar->HasFlag(wxTB_VERTICAL)) bars.push_back(bar);
                find(child);
            }
        };
        find(video_box);
        ASSERT_NE(nullptr, display);
        ASSERT_EQ(2u, bars.size()) << "the visual tools and their sub-tools";
        display->SetMinSize(wxSize(320, 240));
        video_box->SetSize(0, 0, 600, 400);
        video_box->Layout();
        auto x_in_box = [&](wxWindow *w) { int x = 0; for (; w && w != video_box; w = w->GetParent()) x += w->GetPosition().x; return x; };
        for (auto bar : bars)
            EXPECT_GE(x_in_box(bar), x_in_box(display) + display->GetSize().x) << "detached: " << detached;
        video_box->Destroy();
    }
}

// Fetch a frame with the main dispatch queue running, so libass finishes its
// setup on it and closing the video does not wait for it (see visual_edit).
void PumpFirstFrame(agi::Context *c, wxWindow *window) {
    wxEventLoop loop;
    wxEventLoopActivator activate(&loop);
    auto provider=c->project->VideoProvider();
    std::thread reader([&] {
        try { provider->GetFrame(0,0); } catch (...) {}
        window->CallAfter([&] { loop.Exit(); });
    });
    loop.Run();reader.join();
}
TEST_F(ordinary_edit, video_slider_and_readouts_are_flat_and_hold_still) {
    OPT_SET("Video/Subtitle Sync")->SetBool(false);
    agi::fs::path kf((std::filesystem::current_path() / "slider-keyframes.txt").wstring());
    {std::ofstream out(kf.string());out<<"# keyframe format v1\nfps 0\n0\n50\n150\n";}
    {auto scope=c->subtitleAccess->TryWrite();
     auto& row=LiveWriteForTest(*c).Events.front();row.Start=2000;row.End=4000;
     LiveWriteForTest(*c).Commit("line",AssFile::COMMIT_DIAG_TIME);}
    auto video_box=new VideoBox(window,false,c.get());
    BOOST_SCOPE_EXIT_ALL(&) { video_box->Destroy(); c->project->CloseVideo(); c->project->CloseKeyframes(); };
    VideoSlider *slider=nullptr;std::vector<VideoTimeLabel*> labels;VideoZoomControl *zoom=nullptr;VideoDisplay *display=nullptr;
    std::vector<wxToolBar*> rows;int shown_text_ctrls=0,combos=0;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto ch:w->GetChildren()){
        if(auto s=dynamic_cast<VideoSlider*>(ch))slider=s;
        if(auto l=dynamic_cast<VideoTimeLabel*>(ch))labels.push_back(l);
        if(auto z=dynamic_cast<VideoZoomControl*>(ch))zoom=z;
        if(auto d=dynamic_cast<VideoDisplay*>(ch))display=d;
        if(auto b=dynamic_cast<wxToolBar*>(ch);b&&!b->HasFlag(wxTB_VERTICAL))rows.push_back(b);
        if(dynamic_cast<wxComboBox*>(ch))++combos;
        else if(dynamic_cast<wxTextCtrl*>(ch)&&ch->IsShown())++shown_text_ctrls;
        find(ch);}};
    find(video_box);
    ASSERT_NE(nullptr,slider);ASSERT_EQ(2u,labels.size());ASSERT_NE(nullptr,zoom);ASSERT_NE(nullptr,display);
    EXPECT_EQ(0,shown_text_ctrls) << "the readouts are drawn, not text boxes";
    EXPECT_EQ(0,combos) << "the zoom is a drawn control";
    auto position=labels[0],relative=labels[1];

    // Play and play line, no stop: play pauses while playing.
    ASSERT_EQ(2u,rows.size());
    wxToolBar *play_bar=nullptr,*options_bar=nullptr;
    for(auto bar:rows)(bar->GetToolsCount()==2?play_bar:options_bar)=bar;
    ASSERT_NE(nullptr,play_bar);ASSERT_NE(nullptr,options_bar);
    EXPECT_EQ(cmd::get("video/play")->StrDisplay(c.get()),play_bar->GetToolByPos(0)->GetLabel());
    EXPECT_EQ(cmd::get("video/play/line")->StrDisplay(c.get()),play_bar->GetToolByPos(1)->GetLabel());
    EXPECT_TRUE(cmd::get("video/play")->Type()&cmd::COMMAND_DYNAMIC_ICON);

    // Without video: dashes and no line span.
    EXPECT_EQ(wxString(wxUniChar(0x2014)),position->GetMain());
    EXPECT_EQ(-1,slider->GetLineStart());
    position->StartEditing();
    EXPECT_FALSE(position->IsEditing()) << "nothing to type without a video";

    c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    c->project->LoadKeyframes(kf);
    display->SetMinSize(wxSize(640,360));
    video_box->SetSize(0,0,900,600);video_box->Layout();

    // The active line's frames are marked on the slider.
    EXPECT_EQ(c->videoController->FrameAtTime(2000,agi::vfr::START),slider->GetLineStart());
    EXPECT_EQ(c->videoController->FrameAtTime(4000,agi::vfr::END),slider->GetLineEnd());
    EXPECT_LT(slider->GetLineStart(),slider->GetLineEnd());

    // The track runs between equal margins, the last frame at the far end.
    int w=slider->GetClientSize().x;
    EXPECT_EQ(w-slider->XAtFrame(249),slider->XAtFrame(0));
    EXPECT_GT(slider->XAtFrame(0),0);

    // Time, frame number and a keyframe badge instead of a coloured box;
    // the width does not change from frame to frame.
    c->videoController->JumpToFrame(50);
    EXPECT_EQ("0:00:02.000",position->GetMain());
    EXPECT_EQ("50",position->GetSecondary());
    EXPECT_TRUE(position->BadgeShown());
    int width=position->GetSize().x;
    c->videoController->JumpToFrame(51);
    EXPECT_FALSE(position->BadgeShown());
    EXPECT_EQ("51",position->GetSecondary());
    EXPECT_EQ(width,position->GetSize().x);

    // Offsets from the line's start and end, a true minus for negatives.
    c->videoController->JumpToFrame(c->videoController->FrameAtTime(3000,agi::vfr::START));
    EXPECT_EQ("+1000ms",relative->GetMain());
    EXPECT_EQ(wxString(wxUniChar(0x2212))+"1000ms",relative->GetSecondary());

    // Clicking the time turns it into a text box only while typing. The
    // click is handled and the label never takes the focus, or the toolkit
    // would focus it after the click and close the box straight away.
    ASSERT_NE(nullptr,position->EditBox());
    EXPECT_FALSE(position->IsEditing());
    EXPECT_FALSE(position->AcceptsFocus());
    {wxMouseEvent click(wxEVT_LEFT_DOWN);click.SetPosition({20,position->GetSize().y/2});click.SetEventObject(position);
     EXPECT_TRUE(position->GetEventHandler()->ProcessEvent(click)) << "not skipped";
     wxYield();
     EXPECT_TRUE(position->IsEditing()) << "the box opens once the click is over";
     position->CancelEditing();}
    int height=position->GetSize().y;
    position->StartEditing();
    EXPECT_TRUE(position->IsEditing());
    EXPECT_EQ(position->GetMain(),position->EditBox()->GetValue());
    EXPECT_EQ(height,position->GetSize().y) << "the label is as tall as the box already";
    position->EditBox()->ChangeValue("100");position->CommitEditing();
    EXPECT_EQ(100,c->videoController->GetFrameN()) << "digits are a frame number";
    EXPECT_FALSE(position->IsEditing());
    position->StartEditing();position->EditBox()->ChangeValue("0:00:06.000");position->CommitEditing();
    EXPECT_EQ(c->videoController->FrameAtTime(6000,agi::vfr::START),c->videoController->GetFrameN());
    position->StartEditing();position->EditBox()->ChangeValue("4.5");position->CommitEditing();
    EXPECT_EQ(c->videoController->FrameAtTime(4500,agi::vfr::START),c->videoController->GetFrameN()) << "seconds";
    position->StartEditing();position->EditBox()->ChangeValue("99999");position->CommitEditing();
    EXPECT_EQ(249,c->videoController->GetFrameN()) << "past the end: the last frame";
    position->StartEditing();position->EditBox()->ChangeValue("abc");position->CommitEditing();
    EXPECT_TRUE(position->IsEditing()) << "bad input keeps the box open";
    EXPECT_EQ(249,c->videoController->GetFrameN());
    position->CancelEditing();
    EXPECT_FALSE(position->IsEditing());
    EXPECT_EQ(249,c->videoController->GetFrameN());

    // The frame number is edited on its own: clicking it opens a box over
    // it alone, holding the frame number, which takes only digits.
    {int frame_x=position->GetSize().x-position->FromDIP(4);
     EXPECT_EQ(0,position->PartAt(position->FromDIP(12)));
     EXPECT_EQ(1,position->PartAt(frame_x));
     wxMouseEvent click(wxEVT_LEFT_DOWN);click.SetPosition({frame_x,position->GetSize().y/2});click.SetEventObject(position);
     position->GetEventHandler()->ProcessEvent(click);
     wxYield();
     ASSERT_EQ(1,position->EditingPart());
     EXPECT_EQ("249",position->EditBox()->GetValue());
     EXPECT_GT(position->EditBox()->GetPosition().x,position->FromDIP(40)) << "the box covers the frame number only";
     position->EditBox()->ChangeValue("0:00:01.000");position->CommitEditing();
     EXPECT_EQ(1,position->EditingPart()) << "not a frame number";
     position->EditBox()->ChangeValue(" 120 ");position->CommitEditing();
     EXPECT_EQ(120,c->videoController->GetFrameN());
     EXPECT_FALSE(position->IsEditing());}

    // A click anywhere else closes the box, even on something that never
    // takes the keyboard, so the box never loses the focus.
    position->StartEditing();
    ASSERT_EQ(0,position->EditingPart());
    {wxMouseEvent away(wxEVT_LEFT_DOWN);away.SetEventObject(relative);
     relative->GetEventHandler()->ProcessEvent(away);}
    EXPECT_FALSE(position->IsEditing());
    EXPECT_EQ(120,c->videoController->GetFrameN());
    // A click in the box itself keeps it; one on the other part switches.
    position->StartEditing();
    {wxMouseEvent inside(wxEVT_LEFT_DOWN);inside.SetEventObject(position->EditBox());
     position->EditBox()->GetEventHandler()->ProcessEvent(inside);}
    EXPECT_EQ(0,position->EditingPart());
    {wxMouseEvent other(wxEVT_LEFT_DOWN);other.SetPosition({position->GetSize().x-position->FromDIP(4),position->GetSize().y/2});other.SetEventObject(position);
     position->GetEventHandler()->ProcessEvent(other);
     wxYield();}
    EXPECT_EQ(1,position->EditingPart());
    position->CancelEditing();
    EXPECT_EQ(-1,position->EditingPart());

    // The slider is tall enough for the handle at its largest: hovered,
    // with the focus ring.
    EXPECT_GE(slider->GetClientSize().y,slider->FromDIP(26));

    // − and + keep the 12.5% steps; the presets are whole percentages.
    display->SetWindowZoom(.75);
    EXPECT_EQ("75%",zoom->ZoomText());
    zoom->Press(VideoZoomControl::Part::In);
    EXPECT_DOUBLE_EQ(.875,display->GetWindowZoom());
    EXPECT_EQ("87.5%",zoom->ZoomText());
    zoom->Press(VideoZoomControl::Part::Out);zoom->Press(VideoZoomControl::Part::Out);
    EXPECT_DOUBLE_EQ(.625,display->GetWindowZoom());
    EXPECT_DOUBLE_EQ(.25,VideoZoomControl::StepIn(.125));
    EXPECT_DOUBLE_EQ(.125,VideoZoomControl::StepOut(.125));
    for(int p:VideoZoomControl::Presets())EXPECT_EQ(0,p%25);
    cmd::call("video/zoom/in",c.get());
    EXPECT_DOUBLE_EQ(.75,display->GetWindowZoom()) << "the zoom commands step the same way";
    // The wheel steps the same way too, up zooming in
    wxMouseEvent wheel(wxEVT_MOUSEWHEEL); wheel.m_wheelRotation=120; wheel.m_wheelDelta=120; wheel.SetEventObject(zoom);
    zoom->GetEventHandler()->ProcessEvent(wheel);
    EXPECT_DOUBLE_EQ(.875,display->GetWindowZoom());
    zoom->Wheel(-2); EXPECT_DOUBLE_EQ(.625,display->GetWindowZoom());
    zoom->Wheel(1);

    // Readouts at the left, play buttons centred, the zoom at the right.
    wxWindow *block=zoom->GetParent();
    int gap=theme::BlockGap(video_box);
    auto right=[](wxWindow*w){return w->GetPosition().x+w->GetSize().x;};
    for(int video_w:{960,480}){
        display->SetMinSize(wxSize(video_w,video_w*9/16));
        video_box->SetSize(0,0,1400,800);video_box->Layout();
        int block_w=block->GetClientSize().x;
        EXPECT_EQ(gap,position->GetPosition().x);
        EXPECT_EQ(block_w-gap,right(zoom));
        EXPECT_LT(right(relative),play_bar->GetPosition().x) << video_w;
        EXPECT_LT(right(play_bar),options_bar->GetPosition().x) << video_w;
        int centre=play_bar->GetPosition().x+play_bar->GetSize().x/2;
        if(video_w==960) EXPECT_NEAR(block_w/2,centre,1) << "centred on the video";
        else EXPECT_NEAR((right(relative)+options_bar->GetPosition().x)/2,centre,gap) << "narrow: centred between the groups";
    }

    // Offsets longer than the sample widen the readout, and the whole row
    // makes room, so nothing is cut off or covered.
    int relative_w=relative->GetSize().x;
    {auto scope=c->subtitleAccess->TryWrite();
     auto& row=LiveWriteForTest(*c).Events.front();row.Start=3000000;row.End=3600000;
     LiveWriteForTest(*c).Commit("line",AssFile::COMMIT_DIAG_TIME);}
    EXPECT_EQ(10u,relative->GetSecondary().length()) << "seven digits, more than the sample's five";
    EXPECT_GT(relative->GetSize().x,relative_w);
    EXPECT_GE(relative->GetSize().x,relative->GetMinSize().x);
    EXPECT_LT(right(relative),play_bar->GetPosition().x);
}

TEST_F(ordinary_edit, style_look_applies_the_tags_before_a_point_of_the_line) {
    AssStyle base; base.name="Base"; base.font="Arial"; base.fontsize=40; base.outline_w=2; base.shadow_w=3; base.alignment=2;
    base.primary=agi::Color(255,255,255); base.outline=agi::Color(0,0,0);
    AssStyle other; other.name="Other"; other.font="Other Font"; other.fontsize=20; other.alignment=8;
    StyleLookup find=[&](std::string const& name)->AssStyle const* { return name=="Base"?&base:name=="Other"?&other:nullptr; };
    AssDialogue line; line.Style="Base";
    line.Text="{\\an7\\fs50\\b1}ab{\\i1\\1c&H0000FF&\\1a&H80&}cd{\\r}ef{\\rOther\\an3}gh";
    auto at=[&](int plain) { return LookAt(line,plain,find); };
    // Leading tags count from the start; the first alignment counts everywhere
    EXPECT_EQ(50,at(0).size); EXPECT_EQ(700,at(0).weight); EXPECT_EQ(7,at(0).alignment); EXPECT_FALSE(at(0).italic);
    EXPECT_FALSE(at(1).italic);
    // The tags in front of a character apply to it
    EXPECT_TRUE(at(2).italic); EXPECT_EQ(agi::Color(255,0,0,0x80),at(2).primary); EXPECT_EQ(50,at(2).size);
    // \r goes back to the line's style, \rName to that one, alignment stays
    EXPECT_EQ(40,at(4).size); EXPECT_FALSE(at(4).italic); EXPECT_EQ(400,at(4).weight); EXPECT_EQ(agi::Color(255,255,255),at(4).primary);
    EXPECT_EQ("Other Font",at(6).font); EXPECT_EQ(20,at(6).size); EXPECT_EQ(7,at(6).alignment); EXPECT_EQ("Base",at(6).style);
    // Shadow, border, alpha for all colours, weights, a tag without value
    line.Text="{\\xshad4\\yshad-2\\xbord1\\ybord5\\alpha&HFF&\\b600\\fsp3\\blur1.5\\fs}x";
    auto look=at(0);
    EXPECT_EQ(4,look.shadow_x); EXPECT_EQ(-2,look.shadow_y); EXPECT_EQ(5,look.border); EXPECT_EQ(600,look.weight); EXPECT_FALSE(look.Bold());
    EXPECT_EQ(255,look.primary.a); EXPECT_EQ(255,look.outline.a); EXPECT_EQ(255,look.shadow.a);
    EXPECT_EQ(3,look.spacing); EXPECT_EQ(1.5,look.blur); EXPECT_EQ(40,look.size);
    // Missing styles look like a new style; comments are not text
    line.Style="Missing"; line.Text="{note}ab{\\u1}c";
    EXPECT_EQ(AssStyle().font,at(0).font); EXPECT_FALSE(at(1).underline); EXPECT_TRUE(at(2).underline);
    // Tags inside \t are where an animation ends, not how the text looks
    line.Style="Base"; line.Text="{\\t(\\fs99\\b1)}x";
    EXPECT_EQ(40,at(0).size); EXPECT_EQ(400,at(0).weight);
    EXPECT_EQ(0,PlainPosition("{\\b1}abc",5)); EXPECT_EQ(2,PlainPosition("{\\b1}abc",7)); EXPECT_EQ(3,PlainPosition("a{b",3));
}

TEST_F(ordinary_edit, style_panel_changes_the_text_through_its_controls) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->fontsize=40;style->alignment=2;file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="{\\b1}Hello world";
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    auto id=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({id},id);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Buttons
    panel->UpdateNow(); panel->Press("bold");
    EXPECT_EQ("Hello world",line()) << "not bold is the style's own";
    panel->UpdateNow(); panel->Press("italic");
    EXPECT_EQ("{\\i1}Hello world",line());
    panel->UpdateNow(); panel->Press("align_top");
    EXPECT_EQ("{\\i1\\an8}Hello world",line());
    panel->UpdateNow(); panel->Press("align_right");
    EXPECT_EQ("{\\i1\\an9}Hello world",line());
    EXPECT_EQ("on",shown()["align_right"]);

    // Dragging a number: one undo step
    panel->EnterNumber("size",41,false); panel->EnterNumber("size",45,false); panel->EnterNumber("size",52,true);
    EXPECT_EQ("{\\i1\\an9\\fs52}Hello world",line());
    EXPECT_EQ("52",shown()["size"]);
    c->subsController->Undo();
    EXPECT_EQ("{\\i1\\an9}Hello world",line());
    panel->EnterNumber("size",5000,true);
    EXPECT_EQ("{\\i1\\an9\\fs1000}Hello world",line()) << "kept in range";
    c->subsController->Undo();

    // Typing a number
    panel->UpdateNow();
    panel->StartEditing("spacing");
    auto box=panel->EditBox("spacing");
    ASSERT_NE(nullptr,box); EXPECT_TRUE(box->IsShown()); EXPECT_EQ("0",box->GetValue());
    box->ChangeValue("1,5");
    wxCommandEvent enter(wxEVT_TEXT_ENTER,box->GetId()); enter.SetEventObject(box); box->GetEventHandler()->ProcessEvent(enter);
    EXPECT_FALSE(box->IsShown());
    EXPECT_EQ("{\\i1\\an9\\fsp1.5}Hello world",line());
    panel->StartEditing("spacing"); box->ChangeValue("abc");
    box->GetEventHandler()->ProcessEvent(enter);
    EXPECT_TRUE(box->IsShown()) << "not a number: stays open";
    wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode=WXK_ESCAPE; escape.SetEventObject(box); box->GetEventHandler()->ProcessEvent(escape);
    EXPECT_FALSE(box->IsShown());
    EXPECT_EQ("{\\i1\\an9\\fsp1.5}Hello world",line());

    // Font and weight
    panel->ChooseFont("Test Font");
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello world",line());
    panel->ChooseWeight(700,false);
    EXPECT_EQ("{\\an9\\fsp1.5\\fnTest Font\\b1}Hello world",line());
    c->subsController->Undo();
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello world",line()) << "weight and slant are one step";

    // Selected text
    std::string text=line(); int w=static_cast<int>(text.find("world"));
    c->textSelectionController->SetSelection(w,w+5);
    EXPECT_EQ("The selected text",shown()["scope"]);
    panel->Press("underline");
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello {\\u1}world",line());
    EXPECT_EQ("on",shown()["underline"]) << "the selection follows the text";
    panel->EnterNumber("size",60,false); panel->EnterNumber("size",70,true);
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello {\\u1\\fs70}world",line());
    text=line(); int h=static_cast<int>(text.find("Hello"));
    c->textSelectionController->SetSelection(h,h+5);
    panel->UpdateNow(); panel->Press("strikeout");
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font\\s1}Hello{\\s0} {\\u1\\fs70}world",line());
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_changes_fill_outline_shadow_and_background) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->outline_w=3;style->shadow_w=0;
     style->primary=agi::Color(255,255,255);style->outline=agi::Color(0,0,0);style->shadow=agi::Color(0,0,0,0x80);file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="Hello";
     auto other=new AssDialogue();other->Style="Look";other->Text="Other";file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    auto id=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({id},id);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Check boxes: hide, and show again with the style's value
    panel->Check("fill",false); EXPECT_EQ("{\\1a&HFF&}Hello",line()); EXPECT_EQ("off #FFFFFF",shown()["fill"]);
    panel->Check("fill",true); EXPECT_EQ("Hello",line());
    panel->Check("outline",false); EXPECT_EQ("{\\bord0}Hello",line()); EXPECT_EQ("off #000000",shown()["outline"]);
    panel->Check("outline",true); EXPECT_EQ("Hello",line()) << "the style's width comes back";
    panel->Check("shadow",true); EXPECT_EQ("{\\shad2}Hello",line()); EXPECT_EQ("on #000000",shown()["shadow"]);
    EXPECT_EQ("135",shown()["shadow_angle"]); EXPECT_EQ("2.83",shown()["shadow_distance"]);
    // Distance keeps the angle, angle keeps the distance
    panel->EnterNumber("shadow_distance",4,true);
    EXPECT_EQ("{\\shad2.83}Hello",line()) << "equal offsets are one tag";
    panel->UpdateNow(); panel->EnterNumber("shadow_angle",90,true);
    EXPECT_EQ("{\\xshad4\\yshad0}Hello",line());
    panel->UpdateNow(); panel->EnterNumber("shadow_angle",180,true);
    EXPECT_EQ("{\\xshad0\\yshad4}Hello",line());
    panel->Check("shadow",false); EXPECT_EQ("Hello",line()) << "no shadow is the style's";

    // Numbers at the right of the colours
    panel->EnterNumber("fill_opacity",50,true); EXPECT_EQ("{\\1a&H80&}Hello",line());
    EXPECT_EQ("50",shown()["fill_opacity"]);
    panel->EnterNumber("fill_opacity",100,true); EXPECT_EQ("Hello",line());
    panel->EnterNumber("outline_width",1.5,false); panel->EnterNumber("outline_width",4,true);
    EXPECT_EQ("{\\bord4}Hello",line());
    c->subsController->Undo(); EXPECT_EQ("Hello",line()) << "one drag, one step";
    panel->EnterNumber("shadow_opacity",0,true); EXPECT_EQ("{\\4a&HFF&}Hello",line());
    c->subsController->Undo();
    panel->EnterNumber("blur",0.8,true); EXPECT_EQ("{\\blur0.8}Hello",line());
    c->subsController->Undo();

    // Colours: only what changed is written
    panel->UpdateNow();
    panel->ChooseColour("fill",agi::Color(255,0,0));
    EXPECT_EQ("{\\c&H0000FF&}Hello",line());
    panel->UpdateNow();
    panel->ChooseColour("fill",agi::Color(255,0,0,0x40));
    EXPECT_EQ("{\\c&H0000FF&\\1a&H40&}Hello",line());
    panel->UpdateNow();
    panel->ChooseColour("outline",agi::Color(0,0,255));
    EXPECT_EQ("{\\c&H0000FF&\\1a&H40&\\3c&HFF0000&}Hello",line());
    panel->UpdateNow();
    panel->ChooseColour("shadow",agi::Color(0,255,0,0x80));
    EXPECT_EQ("{\\c&H0000FF&\\1a&H40&\\3c&HFF0000&\\4c&H00FF00&}Hello",line()) << "the same alpha needs no tag";
    EXPECT_EQ("off #00FF00",shown()["shadow"]) << "the colour of a shadow turned off";

    // Only a style has a background (an opaque box, and then no outline):
    // the line gets a copy of its style with one; the style and its other
    // lines stay as they are.
    auto style_of=[&](int i) { return std::string(std::next(LiveRead(*c).Events.begin(),i)->Style.get()); };
    size_t styles=LiveRead(*c).Styles.size();
    panel->Check("background",true);
    EXPECT_EQ("Look Box",style_of(0));
    EXPECT_EQ("Look",style_of(1)) << "the other line keeps its style";
    EXPECT_EQ(1,LiveRead(*c).GetStyle("Look")->borderstyle) << "the style stays";
    ASSERT_NE(nullptr,LiveRead(*c).GetStyle("Look Box"));
    EXPECT_EQ(3,LiveRead(*c).GetStyle("Look Box")->borderstyle);
    {AssStyle a(*LiveRead(*c).GetStyle("Look")),b(*LiveRead(*c).GetStyle("Look Box"));
     a.name=b.name="";b.borderstyle=1;a.UpdateData();b.UpdateData();
     EXPECT_EQ(a.GetEntryData(),b.GetEntryData()) << "a copy, but for the box";}
    EXPECT_EQ(styles+1,LiveRead(*c).Styles.size());
    EXPECT_EQ("on #0000FF",shown()["background"]);
    std::string before=line();
    panel->Check("outline",true);
    EXPECT_EQ(before,line()) << "a box has no outline to show";
    // Off: back to the style it came from; on again: the copy made before
    panel->Check("background",false);
    EXPECT_EQ("Look",style_of(0));
    panel->Check("background",true);
    EXPECT_EQ("Look Box",style_of(0));
    EXPECT_EQ(styles+1,LiveRead(*c).Styles.size()) << "no second copy";
    // A line whose style has a box of its own gets a copy without one
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto boxed=new AssStyle();boxed->name="Boxed";boxed->fontsize=33;boxed->borderstyle=3;file.Styles.push_back(*boxed);
     file.Events.front().Style="Boxed";file.Commit("boxed",AssFile::COMMIT_STYLES|AssFile::COMMIT_DIAG_META);}
    panel->Check("background",false);
    EXPECT_EQ("Boxed No Box",style_of(0));
    EXPECT_EQ(1,LiveRead(*c).GetStyle("Boxed No Box")->borderstyle);
    EXPECT_EQ(3,LiveRead(*c).GetStyle("Boxed")->borderstyle);
    c->subsController->Undo();
    EXPECT_EQ("Boxed",style_of(0));
    EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Boxed No Box")) << "one undo step takes the copy away too";
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_sets_updates_resets_and_makes_styles) {
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->fontsize=40;style->alignment=2;style->bold=false;file.Styles.push_back(*style);
     style=new AssStyle();style->name="Other";style->fontsize=20;file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="{\\fs60\\b1\\an8\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small";
     auto other=new AssDialogue();other->Style="Look";other->Text="{\\fs60}two";second=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto text=[&](int n) { return std::string(std::next(LiveRead(*c).Events.begin(),n)->Text.get()); };
    auto style=[&](std::string const& name) { return LiveRead(*c).GetStyle(name); };
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };
    EXPECT_EQ("enabled",shown()["style_update"]);

    // Choosing a style
    panel->ChooseStyle("Other");
    EXPECT_EQ("Other",std::string(LiveRead(*c).Events.front().Style.get()));
    EXPECT_EQ("Other",shown()["style"]);
    c->subsController->Undo();

    // Update the style from the line: what a style can hold moves into it
    panel->Press("style_update");
    ASSERT_NE(nullptr,style("Look"));
    EXPECT_EQ(60,style("Look")->fontsize); EXPECT_TRUE(style("Look")->bold); EXPECT_EQ(8,style("Look")->alignment);
    EXPECT_EQ("{\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small",text(0)) << "blur stays: a style has none; tags inside the line stay";
    EXPECT_EQ("{\\fs60}two",text(1)) << "lines not selected keep their tags";
    EXPECT_EQ("60",shown()["size"]);
    c->subsController->Undo();
    EXPECT_EQ(40,style("Look")->fontsize);
    EXPECT_EQ("{\\fs60\\b1\\an8\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small",text(0));

    // Reset to the style: the panel's tags go, others stay
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    panel->Press("style_reset");
    EXPECT_EQ("{\\pos(10,20)}Big small",text(0));
    EXPECT_EQ("two",text(1));
    c->subsController->Undo();

    // A new style from the current line, for the selected lines
    EXPECT_TRUE(panel->NewStyle("Big"));
    ASSERT_NE(nullptr,style("Big"));
    EXPECT_EQ(60,style("Big")->fontsize); EXPECT_TRUE(style("Big")->bold); EXPECT_EQ(8,style("Big")->alignment);
    EXPECT_EQ(40,style("Look")->fontsize) << "the old style stays";
    EXPECT_EQ("Big",std::string(LiveRead(*c).Events.front().Style.get()));
    EXPECT_EQ("Big",std::string(LiveRead(*c).Events.back().Style.get()));
    EXPECT_EQ("{\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small",text(0));
    EXPECT_EQ("two",text(1)) << "its \\fs60 is now the style's";
    EXPECT_FALSE(panel->NewStyle("big")) << "names are unique without regard to case";
    c->subsController->Undo();
    EXPECT_EQ(nullptr,style("Big"));
    EXPECT_EQ("Look",std::string(LiveRead(*c).Events.front().Style.get()));

    // Nothing to do: no undo step
    c->selectionController->SetSelectionAndActiveIDs({},0);
    EXPECT_EQ("disabled",shown()["style_update"]);
    EXPECT_EQ(-1,style_edit::ResetToStyle(c.get()));
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_moves_scales_turns_and_fades_lines) {
    int moving;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     file.SetScriptInfo("PlayResX","640");file.SetScriptInfo("PlayResY","480");
     auto style=new AssStyle();style->name="Look";style->alignment=2;style->Margin={{10,20,30}};
     style->primary.a=0;style->outline.a=0;style->shadow.a=0;file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="Hello";
     auto other=new AssDialogue();other->Style="Look";other->Text="{\\move(1,2,3,4)}moving";moving=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto text=[&](int n) { return std::string(std::next(LiveRead(*c).Events.begin(),n)->Text.get()); };
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };
    EXPECT_FALSE(panel->IsSectionShown("transform")) << "folded at first";

    // Without \pos: where alignment and margins put it
    auto s=shown();
    EXPECT_EQ("315",s["position_x"]); EXPECT_EQ("450",s["position_y"]);
    EXPECT_EQ("100",s["scale_x"]); EXPECT_EQ("0",s["rotation"]); EXPECT_EQ("100",s["opacity"]);
    panel->EnterNumber("position_x",100,true);
    EXPECT_EQ("{\\pos(100,450)}Hello",text(0));
    panel->UpdateNow(); panel->EnterNumber("position_y",200,true);
    EXPECT_EQ("{\\pos(100,200)}Hello",text(0));
    EXPECT_EQ("200",shown()["position_y"]);

    panel->EnterNumber("scale_x",150,true); panel->UpdateNow(); panel->EnterNumber("rotation",-10,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10}Hello",text(0));
    panel->UpdateNow(); panel->EnterNumber("opacity",50,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10\\alpha&H80&}Hello",text(0));
    EXPECT_EQ("50",shown()["opacity"]); EXPECT_EQ("50",shown()["fill_opacity"]);
    panel->EnterNumber("fill_opacity",100,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10\\alpha&H80&\\1a&H00&}Hello",text(0)) << "\\alpha would still apply without it";
    EXPECT_EQ(wxS("—"),shown()["opacity"]) << "fill, outline and shadow differ";
    panel->EnterNumber("opacity",100,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10}Hello",text(0)) << "the style's opacity needs no tag";

    // A moving line keeps its \move
    c->selectionController->SetSelectionAndActiveIDs({first,moving},first);
    panel->UpdateNow(); panel->EnterNumber("position_x",5,true);
    EXPECT_EQ("{\\fscx150\\frz-10\\pos(5,200)}Hello",text(0));
    EXPECT_EQ("{\\move(1,2,3,4)}moving",text(1));
    // Updating the style takes scale and rotation, not the position
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    panel->Press("style_update");
    EXPECT_EQ(150,LiveRead(*c).GetStyle("Look")->scalex); EXPECT_EQ(-10,LiveRead(*c).GetStyle("Look")->angle);
    EXPECT_EQ("{\\pos(5,200)}Hello",text(0));
    panel->Destroy();
}

TEST_F(ordinary_edit, edit_box_has_line_fields_below_the_text_and_a_time_unit_switch) {
    BOOST_SCOPE_EXIT_ALL(&) { OPT_SET("Subtitle/Edit Box/Fields Below Text")->SetBool(true); c->project->CloseTimecodes(); c->subsGrid=nullptr; };
    int bitmap_buttons=0; bool margins=false, edit_button=false; TimeUnitToggle *unit=nullptr; TimeEdit *end=nullptr;
    wxWindow *comment=nullptr,*actor=nullptr,*effect=nullptr,*original=nullptr; FlatChoice *style=nullptr; FlatFrame *layer=nullptr;
    for (auto child:descendants(box)) {
        if (dynamic_cast<wxBitmapButton*>(child)) ++bitmap_buttons;
        if (child->GetToolTipText().Contains("Margin")) margins=true;
        if (auto b=dynamic_cast<wxButton*>(child); b && b->GetLabel()=="Edit") edit_button=true;
        if (auto t=dynamic_cast<TimeUnitToggle*>(child)) unit=t;
        if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="End time") end=t;
        if (auto s=dynamic_cast<FlatChoice*>(child)) style=s;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetLabel()=="Layer") layer=f;
        if (auto b=dynamic_cast<FlatCheck*>(child)) (b->GetLabel()=="Show Original" ? original : comment)=b;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetToolTipText().StartsWith("Actor")) actor=f;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetToolTipText().StartsWith("Effect")) effect=f;
    }
    EXPECT_EQ(0,bitmap_buttons) << "text and colours are in the style panel, Enter goes to the next line";
    EXPECT_FALSE(margins) << "the margins are in the style panel";
    EXPECT_FALSE(edit_button) << "the style panel opens the style editor";
    ASSERT_NE(nullptr,unit); ASSERT_NE(nullptr,end); ASSERT_NE(nullptr,style); ASSERT_NE(nullptr,layer);
    ASSERT_NE(nullptr,comment); ASSERT_NE(nullptr,actor); ASSERT_NE(nullptr,effect); ASSERT_NE(nullptr,original);
    auto end_frame=end->GetParent();
    wxWindow *start_frame=nullptr;
    for (auto child:descendants(box)) if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="Start time") start_frame=t->GetParent();
    ASSERT_NE(nullptr,start_frame);

    // Where things are: what the line is in the first row, the original at
    // its end; when and on which layer in the second, the layer at its end
    box->SetSize(0,0,900,300); box->Layout();
    auto at=[&](wxWindow *w) { wxPoint p; for(;w&&w!=box;w=w->GetParent()) p+=w->GetPosition(); return p; };
    auto centre_y=[&](wxWindow *w) { return at(w).y+w->GetSize().y/2; };
    for (wxWindow *w:{static_cast<wxWindow*>(style),actor,effect,original}) EXPECT_NEAR(centre_y(comment),centre_y(w),2);
    EXPECT_LT(at(comment).x,at(style).x); EXPECT_LT(at(style).x,at(actor).x); EXPECT_LT(at(actor).x,at(effect).x); EXPECT_LT(at(effect).x,at(original).x);
    for (wxWindow *w:{static_cast<wxWindow*>(unit),static_cast<wxWindow*>(layer)}) EXPECT_NEAR(centre_y(end_frame),centre_y(w),2);
    EXPECT_LT(at(layer).x,at(start_frame).x) << "the layer comes first"; EXPECT_LT(at(end_frame).x,at(unit).x);
    EXPECT_LT(centre_y(comment),centre_y(end_frame)) << "the times are next to the grid";
    EXPECT_LE(at(effect).x+effect->GetSize().x,at(original).x) << "the effect is narrower than the actor";
    EXPECT_LT(effect->GetSize().x,actor->GetSize().x);

    // The times are labelled when there is room, and only then
    auto end_label=static_cast<FlatFrame*>(end_frame);
    EXPECT_TRUE(end_label->IsLabelShown());
    box->SetSize(0,0,480,300); box->Layout();
    EXPECT_FALSE(end_label->IsLabelShown()) << "the arrow and the tooltips still say which is which";
    EXPECT_LE(at(unit).x+unit->GetSize().x,box->GetClientSize().x) << "nothing is cut off";
    box->SetSize(0,0,900,300); box->Layout();
    EXPECT_TRUE(end_label->IsLabelShown());

    // The fields sit below the text, next to the grid; the option puts them back above
    EXPECT_GT(at(end).y,at(editor).y);
    OPT_SET("Subtitle/Edit Box/Fields Below Text")->SetBool(false);
    EXPECT_LT(at(end).y,at(editor).y);

    // Frames need timecodes
    c->subsGrid=new BaseGrid(window,c.get());
    EXPECT_FALSE(unit->FramesAllowed());
    unit->Choose(1); EXPECT_FALSE(unit->ByFrame());
    {std::ofstream timecodes("timecodes.txt"); timecodes << "# timecode format v1\nAssume 25\n";}
    c->project->LoadTimecodes("timecodes.txt");
    ASSERT_TRUE(unit->FramesAllowed());
    wxString time=end->GetValue();
    unit->Choose(1); EXPECT_TRUE(unit->ByFrame());
    EXPECT_EQ("124",end->GetValue()) << "the last frame shown before 5 s at 25 fps";
    unit->Choose(0); EXPECT_EQ(time,end->GetValue());
    unit->Choose(1); c->project->CloseTimecodes();
    EXPECT_FALSE(unit->FramesAllowed()); EXPECT_FALSE(unit->ByFrame()); EXPECT_EQ(time,end->GetValue());

    // Enter on the last line makes a new one after it and goes there
    ASSERT_EQ(1u,LiveRead(*c).Events.size());
    editor->SetFocus();
    wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(enter);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());
    EXPECT_EQ(std::next(LiveRead(*c).Events.begin())->Id,c->selectionController->ActiveID());
}

TEST_F(ordinary_edit, edit_box_style_layer_and_character_count) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("Subtitle/Character Limit")->SetInt(40); };
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     for (auto name:{"Alpha","Beta"}) {auto style=new AssStyle();style->name=name;file.Styles.push_back(*style);}
     auto other=new AssDialogue();other->Layer=4;second=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    FlatChoice *style=nullptr; FlatFrame *layer_frame=nullptr; FlatBadge *count=nullptr; FlatCheck *original=nullptr; wxStaticText *caption=nullptr;
    for (auto child:descendants(box)) {
        if (auto s=dynamic_cast<FlatChoice*>(child)) style=s;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetLabel()=="Layer") layer_frame=f;
        if (auto b=dynamic_cast<FlatBadge*>(child)) count=b;
        if (auto b=dynamic_cast<FlatCheck*>(child); b && b->GetLabel()=="Show Original") original=b;
        if (auto t=dynamic_cast<wxStaticText*>(child); t && t->GetLabel().StartsWith("Original")) caption=t;
    }
    ASSERT_NE(nullptr,style); ASSERT_NE(nullptr,layer_frame); ASSERT_NE(nullptr,count); ASSERT_NE(nullptr,original); ASSERT_NE(nullptr,caption);
    auto layer=static_cast<wxTextCtrl*>(layer_frame->GetContent());
    auto row=[&](int n) { return &*std::next(LiveRead(*c).Events.begin(),n); };

    // The style list: the file's styles, the wheel goes down them, turns close together are one undo step
    EXPECT_EQ((std::vector<wxString>{"Default","Alpha","Beta"}),style->GetItems());
    EXPECT_EQ("Default",style->GetStringSelection());
    EXPECT_EQ("Default",style->GetToolTipText()) << "hovering shows the style's name";
    wxMouseEvent wheel(wxEVT_MOUSEWHEEL); wheel.m_wheelRotation=-120; wheel.m_wheelDelta=120; wheel.SetEventObject(style);
    style->GetEventHandler()->ProcessEvent(wheel);
    EXPECT_EQ("Alpha",row(0)->Style.get());
    style->Step(1); EXPECT_EQ("Beta",row(0)->Style.get()); EXPECT_EQ("Beta",style->GetStringSelection()); EXPECT_EQ("Beta",style->GetToolTipText());
    style->Step(1); EXPECT_EQ("Beta",row(0)->Style.get()) << "stays at the end";
    c->subsController->Undo(); EXPECT_EQ("Default",row(0)->Style.get()) << "one undo step";
    wxKeyEvent up(wxEVT_KEY_DOWN); up.m_keyCode=WXK_DOWN; up.SetEventObject(style); style->GetEventHandler()->ProcessEvent(up);
    EXPECT_EQ("Alpha",row(0)->Style.get());

    // The layer: typed, or moved from each line's own by a drag of the label, the wheel or the keys
    EXPECT_EQ("0",layer->GetValue());
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    EXPECT_EQ(wxS("—"),layer->GetValue()) << "the lines are on different layers";
    layer_frame->Scrub(1,false); layer_frame->Scrub(2,true);
    EXPECT_EQ(2,row(0)->Layer); EXPECT_EQ(6,row(1)->Layer) << "each from its own";
    c->subsController->Undo(); EXPECT_EQ(0,row(0)->Layer); EXPECT_EQ(4,row(1)->Layer) << "a drag is one undo step";
    layer_frame->Scrub(-3,true); EXPECT_EQ(0,row(0)->Layer); EXPECT_EQ(1,row(1)->Layer) << "not below 0";
    layer_frame->Wheel(1); EXPECT_EQ(1,row(0)->Layer); EXPECT_EQ(2,row(1)->Layer);
    wxKeyEvent key(wxEVT_CHAR_HOOK); key.m_keyCode=WXK_DOWN; key.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(key);
    EXPECT_EQ(0,row(0)->Layer); EXPECT_EQ(1,row(1)->Layer);
    layer->ChangeValue("5");
    wxCommandEvent typed(wxEVT_TEXT_ENTER,layer->GetId()); typed.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(typed);
    EXPECT_EQ(5,row(0)->Layer); EXPECT_EQ(5,row(1)->Layer); EXPECT_EQ("5",layer->GetValue());
    layer->ChangeValue("x"); layer->GetEventHandler()->ProcessEvent(typed);
    EXPECT_EQ(5,row(0)->Layer); EXPECT_EQ("5",layer->GetValue()) << "not a number: back to the layer";
    c->selectionController->SetSelectionAndActiveIDs({first},first);

    // Enter in a field is done with it: it does not go to the next line
    wxTextCtrl *actor=nullptr;
    for (auto child:descendants(box)) if (auto a=dynamic_cast<wxTextCtrl*>(child); a && a->GetToolTipText().StartsWith("Actor name")) actor=a;
    ASSERT_NE(nullptr,actor);
    size_t lines=LiveRead(*c).Events.size();
    for (wxTextCtrl *field:{actor,layer}) {
        wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(field);
        field->GetEventHandler()->ProcessEvent(enter);
        EXPECT_EQ(lines,LiveRead(*c).Events.size());
        EXPECT_EQ(first,c->selectionController->ActiveID());
    }
    // ...and goes back to the text, as a click anywhere else does
    window->Show(); box->SetSize(0,0,900,300); box->Layout();
    for (int i=0;i<20;++i) wxYield();
    actor->SetFocus(); for (int i=0;i<20;++i) wxYield();
    if (wxWindow::FindFocus()==actor) {
        wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(actor);
        actor->GetEventHandler()->ProcessEvent(enter);
        EXPECT_EQ(static_cast<wxWindow*>(editor),wxWindow::FindFocus());
        layer->SetFocus(); for (int i=0;i<20;++i) wxYield();
        ASSERT_EQ(static_cast<wxWindow*>(layer),wxWindow::FindFocus());
        wxMouseEvent click(wxEVT_LEFT_DOWN); click.SetEventObject(box); box->GetEventHandler()->ProcessEvent(click);
        EXPECT_EQ(static_cast<wxWindow*>(editor),wxWindow::FindFocus()) << "a click on the box's background";
        layer->SetFocus(); for (int i=0;i<20;++i) wxYield();
        wxMouseEvent in_field(wxEVT_LEFT_DOWN); in_field.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(in_field);
        EXPECT_EQ(static_cast<wxWindow*>(layer),wxWindow::FindFocus()) << "a click in the field stays in it";
    }
    else
        std::cerr << "focus not available here: skipped the focus checks\n";
    window->Hide();

    // The character count in the text box's corner: used of the limit, red over it
    Input("Hello");
    EXPECT_EQ("5 / 40",count->GetText()); EXPECT_FALSE(count->IsWarning());
    EXPECT_TRUE(count->GetToolTipText().Contains("40"));
    Input(std::string(41,'a')+"\\Nshort");
    EXPECT_EQ("41 / 40",count->GetText()); EXPECT_TRUE(count->IsWarning()) << "the longest line counts";
    OPT_SET("Subtitle/Character Limit")->SetInt(0);
    Input("{\\b1}abc");
    EXPECT_EQ("3",count->GetText()) << "no limit; tags are not counted";
    EXPECT_GT(count->GetPosition().x,editor->GetClientSize().x/2); EXPECT_GT(count->GetPosition().y,0);

    // The original text, under a caption saying what it is
    EXPECT_FALSE(caption->IsShown());
    original->SetValue(true);
    wxCommandEvent check(wxEVT_CHECKBOX,original->GetId()); check.SetInt(1); check.SetEventObject(original);
    original->GetEventHandler()->ProcessEvent(check);
    EXPECT_TRUE(caption->IsShown());
    original->SetValue(false); check.SetInt(0); original->GetEventHandler()->ProcessEvent(check);
    OPT_SET("Subtitle/Show Original")->SetBool(false);
}

TEST_F(ordinary_edit, style_panel_wheel_goes_through_lists_and_transform_has_titles) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     for (auto name:{"Alpha","Beta"}) {auto style=new AssStyle();style->name=name;file.Styles.push_back(*style);}
     file.Events.front().Text="Hello";
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto row=[&] { return &LiveRead(*c).Events.front(); };
    int first=row()->Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Styles in the file's order: down the list, then back up; one undo step
    panel->Wheel("style",-1); EXPECT_EQ("Alpha",row()->Style.get()); EXPECT_EQ("Alpha",shown()["style"]);
    panel->Wheel("style",-1); EXPECT_EQ("Beta",row()->Style.get());
    panel->Wheel("style",-5); EXPECT_EQ("Beta",row()->Style.get()) << "stays at the end";
    panel->Wheel("style",1); EXPECT_EQ("Alpha",row()->Style.get());
    c->subsController->Undo(); EXPECT_EQ("Default",row()->Style.get()) << "turns close together are one undo step";

    // Weight: heavier going up, each line keeping its slant
    panel->UpdateNow(); panel->Wheel("weight",1); EXPECT_EQ("{\\b500}Hello",std::string(row()->Text.get()));
    panel->UpdateNow(); panel->Wheel("weight",2); EXPECT_EQ("{\\b1}Hello",std::string(row()->Text.get()));
    panel->UpdateNow(); panel->Wheel("weight",-3); EXPECT_EQ("Hello",std::string(row()->Text.get()));

    // Fonts: the installed ones, in order
    wxString before=shown()["font"];
    panel->Wheel("font",-1);
    wxString after=shown()["font"];
    EXPECT_NE(before,after);
    EXPECT_EQ("{\\fn"+after+"}Hello",wxString::FromUTF8(row()->Text.get().c_str()));
    panel->UpdateNow(); panel->Wheel("font",-1); EXPECT_NE(after,shown()["font"]);
    panel->UpdateNow(); panel->Wheel("font",1); EXPECT_EQ(after,shown()["font"]) << "and back up";

    // Transform: a title over each group
    auto s=shown();
    EXPECT_EQ("Position",s["title_position"]); EXPECT_EQ("Margins",s["title_margins"]); EXPECT_EQ("Scale",s["title_scale"]);
    EXPECT_EQ("Rotation",s["title_rotation"]); EXPECT_EQ("Opacity",s["title_opacity"]);
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_sets_karaoke_colour_and_margins) {
    int placed;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->Margin={{10,20,30}};style->secondary=agi::Color(0,0,255);file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="Hello";
     auto other=new AssDialogue();other->Style="Look";other->Text="{\\pos(1,2)}placed";placed=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto text=[&](int n) { return std::string(std::next(LiveRead(*c).Events.begin(),n)->Text.get()); };
    auto margin=[&](int n, int i) { return std::next(LiveRead(*c).Events.begin(),n)->Margin[i]; };
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Karaoke colour: \2c and \2a, hidden like the fill
    EXPECT_EQ("on #0000FF",shown()["secondary"]); EXPECT_EQ("100",shown()["secondary_opacity"]);
    panel->Check("secondary",false); EXPECT_EQ("{\\2a&HFF&}Hello",text(0)); EXPECT_EQ("off #0000FF",shown()["secondary"]);
    panel->Check("secondary",true); EXPECT_EQ("Hello",text(0));
    panel->ChooseColour("secondary",agi::Color(0,255,0)); EXPECT_EQ("{\\2c&H00FF00&}Hello",text(0));
    panel->UpdateNow(); panel->EnterNumber("secondary_opacity",50,true);
    EXPECT_EQ("{\\2c&H00FF00&\\2a&H80&}Hello",text(0)); EXPECT_EQ("50",shown()["secondary_opacity"]);

    // Margins: 0 shows the style's, dim
    auto s=shown();
    EXPECT_EQ("10 (style)",s["margin_left"]); EXPECT_EQ("20 (style)",s["margin_right"]); EXPECT_EQ("30 (style)",s["margin_vertical"]);
    EXPECT_EQ(0u,s.count("margin_left disabled"));
    panel->EnterNumber("margin_left",5,true); EXPECT_EQ(5,margin(0,0)); EXPECT_EQ("5",shown()["margin_left"]);
    EXPECT_EQ(_("left margin change"),c->subsController->GetUndoDescription());
    // A drag is one undo step
    panel->EnterNumber("margin_vertical",31,false); panel->EnterNumber("margin_vertical",40,false); panel->EnterNumber("margin_vertical",42,true);
    EXPECT_EQ(42,margin(0,2));
    c->subsController->Undo(); EXPECT_EQ(0,margin(0,2)); EXPECT_EQ(5,margin(0,0));
    panel->EnterNumber("margin_left",0,true); EXPECT_EQ("10 (style)",shown()["margin_left"]);
    panel->EnterNumber("margin_right",-4,true); EXPECT_EQ(0,margin(0,1)) << "not below 0";

    // Lines differ; a line placed by \pos doesn't use them
    panel->EnterNumber("margin_left",7,true);
    c->selectionController->SetSelectionAndActiveIDs({first,placed},first);
    EXPECT_EQ(wxS("—"),shown()["margin_left"]); EXPECT_EQ("30 (style)",shown()["margin_vertical"]);
    EXPECT_EQ(0u,shown().count("margin_left disabled"));
    c->selectionController->SetSelectionAndActiveIDs({placed},placed);
    EXPECT_EQ(0u,shown().count("margin_left disabled")) << "a line placed by \\pos can still have its margins changed";
    EXPECT_EQ("{\\pos(1,2)}placed",text(1));
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_folds_away_in_narrow_windows_and_opens_from_its_edge) {
    OPT_SET("App/Style Panel/Show")->SetBool(true);
    auto frame=new wxFrame(nullptr,-1,"edge");
    auto root=new wxPanel(frame);
    auto panel=new StylePanel(root,c.get());
    auto edge=new StylePanelEdge(root,panel);
    auto sizer=new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(new wxPanel(root),1,wxEXPAND);sizer->Add(edge,0,wxEXPAND);sizer->Add(panel,0,wxEXPAND);
    root->SetSizer(sizer);
    auto resize=[&](int width) { root->SetSize(width,600); edge->Update(); root->Layout(); };
    resize(root->FromDIP(1400));
    EXPECT_FALSE(edge->Folded());
    EXPECT_GE(panel->GetSize().x,panel->FromDIP(StylePanel::min_width));
    resize(root->FromDIP(900));
    EXPECT_TRUE(edge->Folded()) << "narrow: folded";
    EXPECT_GT(edge->GetSize().x,theme::BlockGap(root)) << "a strip wide enough to click";
    edge->Open();
    EXPECT_FALSE(edge->Folded()) << "opened by hand";
    resize(root->FromDIP(1000));
    EXPECT_FALSE(edge->Folded()) << "stays open while narrow";
    resize(root->FromDIP(1400)); resize(root->FromDIP(900));
    EXPECT_TRUE(edge->Folded()) << "folds again after being wide";
    // The option hides it everywhere; asking for it opens it even when narrow
    resize(root->FromDIP(1400));
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    EXPECT_TRUE(edge->Folded());
    resize(root->FromDIP(900));
    OPT_SET("App/Style Panel/Show")->SetBool(true);
    EXPECT_FALSE(edge->Folded());
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    edge->Open();
    EXPECT_TRUE(OPT_GET("App/Style Panel/Show")->GetBool()) << "the strip turns the option back on";
    EXPECT_FALSE(edge->Folded());
    // The » button folds it away: the option off when wide, back to folded
    // when it was opened by hand in a narrow window
    resize(root->FromDIP(1400));
    panel->Press("collapse");
    EXPECT_FALSE(OPT_GET("App/Style Panel/Show")->GetBool());
    EXPECT_TRUE(edge->Folded());
    edge->Open();
    EXPECT_FALSE(edge->Folded());
    resize(root->FromDIP(900));
    edge->Open();
    ASSERT_FALSE(edge->Folded());
    panel->Press("collapse");
    EXPECT_TRUE(edge->Folded());
    EXPECT_TRUE(OPT_GET("App/Style Panel/Show")->GetBool()) << "still shown once the window is wide";
    resize(root->FromDIP(1400));
    EXPECT_FALSE(edge->Folded());
    // A shape line asks for the panel: it opens
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    ASSERT_TRUE(edge->Folded());
    panel->EditShape();
    EXPECT_FALSE(edge->Folded());
    delete frame; // now: it watches an option the next test changes
}

TEST_F(ordinary_edit, style_edit_writes_one_property_for_lines_or_selected_text) {
    using namespace style_edit;
    auto fs=TagNames(Property::Size), b=TagNames(Property::Weight), i=TagNames(Property::Italic);
    // Whole lines: one tag at the start, the others gone, \t and comments kept
    EXPECT_EQ("{\\b1\\fs50}ab",SetForLine("{\\b1\\fs20}a{\\fs30}b",fs,"\\fs50"));
    EXPECT_EQ("{\\b1}abc",SetForLine("abc",b,"\\b1"));
    EXPECT_EQ("abc",SetForLine("{\\fs20}abc",fs,""));
    EXPECT_EQ("{\\t(\\fs80)\\fs50}x",SetForLine("{\\t(\\fs80)\\fs20}x",fs,"\\fs50"));
    EXPECT_EQ("{\\fs50}{note}abc",SetForLine("{note}abc",fs,"\\fs50"));
    EXPECT_EQ("{\\fscx120\\fsp2\\fs50}x",SetForLine("{\\fscx120\\fs+2\\fsp2}x",fs,"\\fs50")) << "\\fsp and \\fscx are other tags";
    EXPECT_EQ("{\\pos(1,2)\\an7}x",SetForLine("{\\pos(1,2)\\a5}x{\\an3}",TagNames(Property::Alignment),"\\an7"));
    // Selected text: the tag in front, the old value after
    auto r=SetForRange("hello world",0,5,b,"\\b1","\\b0");
    EXPECT_EQ("{\\b1}hello{\\b0} world",r.text); EXPECT_EQ(5,r.start); EXPECT_EQ(10,r.end);
    r=SetForRange("hello world",6,11,b,"\\b1","\\b0");
    EXPECT_EQ("hello {\\b1}world",r.text) << "nothing follows"; EXPECT_EQ(11,r.start); EXPECT_EQ(16,r.end);
    std::string t="a{\\b1}bc{\\b0}d";
    r=SetForRange(t,6,8,i,"\\i1","\\i0");
    EXPECT_EQ("a{\\b1\\i1}bc{\\b0\\i0}d",r.text); EXPECT_EQ("bc",r.text.substr(r.start,r.end-r.start));
    t="ab{\\fs10}cd{\\fs20}ef";
    r=SetForRange(t,0,static_cast<int>(t.find("e")),fs,"\\fs50","\\fs10");
    EXPECT_EQ("{\\fs50}abcd{\\fs20}ef",r.text) << "inside goes, the tag at the end already restores";
    r=SetForRange("{\\fs30}abc",7,8,fs,"\\fs50","\\fs30");
    EXPECT_EQ("{\\fs50}a{\\fs30}bc",r.text) << "the first character: into the block before it";
    EXPECT_EQ("a",r.text.substr(r.start,r.end-r.start));

    // Through the editing gate, with undo
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\fs20}Hello world";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    double style_size=LiveRead(*c).GetStyle("Default")->fontsize;
    c->textSelectionController->SetSelection(0,0);
    int id=Apply(c.get(),Property::Size,[](StyleLook &l){ l.size=50; });
    EXPECT_NE(-1,id); EXPECT_EQ("{\\fs50}Hello world",line());
    // Dragging: every step amends the same undo step
    id=Apply(c.get(),Property::Size,[](StyleLook &l){ l.size=60; },id);
    id=Apply(c.get(),Property::Size,[&](StyleLook &l){ l.size=style_size; },id);
    EXPECT_EQ("Hello world",line()) << "the style's own size needs no tag";
    c->subsController->Undo();
    EXPECT_EQ("{\\fs20}Hello world",line());
    // Selected text, then the selection still covers it
    std::string text=line(); int w=static_cast<int>(text.find("world"));
    c->textSelectionController->SetSelection(w,w+5);
    Apply(c.get(),Property::Italic,[](StyleLook &l){ l.italic=true; });
    EXPECT_EQ("{\\fs20}Hello {\\i1}world",line());
    text=line();
    EXPECT_EQ("world",text.substr(c->textSelectionController->GetSelectionStart(),c->textSelectionController->GetSelectionEnd()-c->textSelectionController->GetSelectionStart()));
    // Alignment is for the whole line even then
    Apply(c.get(),Property::Alignment,[](StyleLook &l){ l.alignment=8; });
    EXPECT_EQ("{\\fs20\\an8}Hello {\\i1}world",line());
    c->textSelectionController->SetSelection(0,0);
    // Several lines, each from its own value
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue();row->Text="{\\an1}two";second=row->Id;LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    Apply(c.get(),Property::Alignment,[](StyleLook &l){ l.alignment=(l.alignment-1)/3*3+3; });
    EXPECT_EQ("{\\fs20\\an9}Hello {\\i1}world",line());
    EXPECT_EQ("{\\an3}two",std::string(LiveRead(*c).Events.back().Text.get()));
    // Nothing changes: no undo step
    EXPECT_EQ(-1,Apply(c.get(),Property::Alignment,[](StyleLook &l){ l.alignment=(l.alignment-1)/3*3+3; }));
    c->subsController->Undo();
    EXPECT_EQ("{\\an1}two",std::string(LiveRead(*c).Events.back().Text.get())) << "undo goes to before the last real change";
}

TEST_F(ordinary_edit, style_panel_shows_the_selected_text_lines_and_where_they_differ) {
    std::vector<int> ids;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->font="Look Sans";style->fontsize=40;style->outline_w=2;style->shadow_w=0;
     style->primary=agi::Color(255,255,255);style->outline=agi::Color(0,0,0);style->shadow=agi::Color(0,0,0,0x80);style->alignment=2;
     file.Styles.push_back(*style);
     for(auto text:{"{\\b1}one","{\\an8\\fs60}two {\\i1}more","{\\bord0\\xshad2\\yshad2}three"}){
        auto row=new AssDialogue();row->Style="Look";row->Text=text;ids.push_back(row->Id);file.Events.push_back(*row);}
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0]);
    auto s=shown();
    EXPECT_EQ("The current line",s["scope"]);
    EXPECT_EQ("Look",s["style"]); EXPECT_EQ("Look Sans",s["font"]); EXPECT_EQ("40",s["size"]); EXPECT_EQ("Bold",s["weight"]);
    EXPECT_EQ("on",s["bold"]); EXPECT_EQ("off",s["italic"]);
    EXPECT_EQ("on",s["align_centre"]); EXPECT_EQ("on",s["align_bottom"]); EXPECT_EQ("off",s["align_top"]);
    EXPECT_EQ("on #FFFFFF",s["fill"]); EXPECT_EQ("100",s["fill_opacity"]);
    EXPECT_EQ("on #000000",s["outline"]); EXPECT_EQ("2",s["outline_width"]);
    EXPECT_EQ("off #000000",s["background"]);
    EXPECT_EQ("off #000000",s["shadow"]); EXPECT_EQ("50",s["shadow_opacity"]);
    EXPECT_EQ("0",s["shadow_distance"]);

    // Several lines: what they share, and "—" where they differ
    c->selectionController->SetSelectionAndActiveIDs({ids[0],ids[1],ids[2]},ids[0]);
    s=shown();
    EXPECT_EQ("3 selected lines",s["scope"]);
    EXPECT_EQ("Look Sans",s["font"]); EXPECT_EQ(wxS("—"),s["size"]); EXPECT_EQ(wxS("—"),s["weight"]);
    EXPECT_EQ("mixed",s["bold"]); EXPECT_EQ("off",s["italic"]);
    EXPECT_EQ("mixed",s["align_top"]); EXPECT_EQ("mixed",s["align_bottom"]); EXPECT_EQ("on",s["align_centre"]);
    EXPECT_EQ("mixed #000000",s["outline"]); EXPECT_EQ(wxS("—"),s["outline_width"]);
    EXPECT_EQ("mixed #000000",s["shadow"]); EXPECT_EQ("50",s["shadow_opacity"]);
    EXPECT_EQ(wxS("—"),s["shadow_distance"]);

    // Text selected in the editor: how that text looks
    c->selectionController->SetSelectionAndActiveIDs({ids[1]},ids[1]);
    std::string two="{\\an8\\fs60}two {\\i1}more";
    int more=static_cast<int>(two.find("more"));
    c->textSelectionController->SetSelection(more,more+4);
    s=shown();
    EXPECT_EQ("The selected text",s["scope"]); EXPECT_EQ("on",s["italic"]); EXPECT_EQ("Italic",s["weight"]); EXPECT_EQ("60",s["size"]);
    EXPECT_EQ("on",s["align_top"]);
    c->textSelectionController->SetSelection(0,3);
    s=shown();
    EXPECT_EQ("The current line",s["scope"]) << "a selection inside the tags is no text";
    int word=static_cast<int>(two.find("two"));
    c->textSelectionController->SetSelection(word,word+3);
    s=shown();
    EXPECT_EQ("The selected text",s["scope"]); EXPECT_EQ("off",s["italic"]);
    c->textSelectionController->SetSelection(0,0);

    // Shadow distance and angle: 0° up, clockwise
    c->selectionController->SetSelectionAndActiveIDs({ids[2]},ids[2]);
    s=shown();
    EXPECT_EQ("2.83",s["shadow_distance"]); EXPECT_EQ("135",s["shadow_angle"]);
    EXPECT_EQ("on #000000",s["shadow"]); EXPECT_EQ("off #000000",s["outline"]); EXPECT_EQ("0",s["outline_width"]);

    // An opaque box is the style's background, and then there is no outline
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).GetStyle("Look")->borderstyle=3;LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_STYLES);}
    c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0]);
    s=shown();
    EXPECT_EQ("on #000000",s["background"]); EXPECT_EQ("off #000000",s["outline"]);

    // Edits show without asking, once the events have run
    {auto write=c->subtitleAccess->TryWrite();
     for(auto &line:LiveWriteForTest(*c).Events) if(line.Id==ids[0]) line.Text="{\\fs33}one";
     LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    wxTheApp->ProcessPendingEvents();
    EXPECT_EQ("33",panel->Shown()["size"]);

    c->selectionController->SetSelectionAndActiveIDs({},0);
    s=shown();
    EXPECT_EQ("No line selected",s["scope"]); EXPECT_EQ(wxS("—"),s["font"]); EXPECT_EQ("off",s["bold"]);

    // Sections fold away
    EXPECT_TRUE(panel->IsSectionShown("appearance"));
    panel->ShowSection("appearance",false);
    EXPECT_FALSE(panel->IsSectionShown("appearance"));
    panel->ShowSection("appearance",true);
    EXPECT_TRUE(panel->IsSectionShown("appearance"));
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_is_shown_and_hidden_from_the_view_menu) {
    auto command=cmd::get("app/toggle/style_panel");
    ASSERT_NE(nullptr,command);
    bool before=OPT_GET("App/Style Panel/Show")->GetBool();
    EXPECT_EQ(before?_("Hide Style Panel"):_("Show Style Panel"),command->StrMenu(c.get()));
    (*command)(c.get());
    EXPECT_NE(before,OPT_GET("App/Style Panel/Show")->GetBool());
    (*command)(c.get());
    EXPECT_EQ(before,OPT_GET("App/Style Panel/Show")->GetBool());
    std::ifstream menu(AEGISUB_SOURCE_ROOT "/src/libresrc/default_menu.json");
    std::string json((std::istreambuf_iterator<char>(menu)),std::istreambuf_iterator<char>());
    EXPECT_NE(std::string::npos,json.find("\"app/toggle/style_panel\""));
}

class audio_edit : public ordinary_edit {
protected:
    agi::ass::Karaoke karaoke;
    agi::signal::Connection file_slot;
    AudioTimingController* Timing() { return c->audioController->GetTimingController(); }
    void SetUp() override {
        ordinary_edit::SetUp();
        OPT_SET("Audio/Auto/Commit")->SetBool(false);
        OPT_SET("Audio/Snap/Enable")->SetBool(false);
        OPT_SET("Audio/Auto/Scroll")->SetBool(false);
        OPT_SET("Audio/Inactive Lines Display Mode")->SetInt(0);
        OPT_SET("Timing/Link Adjacent Lines")->SetBool(false);
        auto write=c->subtitleAccess->TryWrite();
        auto& row=LiveWriteForTest(*c).Events.front();row.Start=1001;row.End=2002;
        row.Text="{\\k50}ab{\\k50}cd";
        LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_NEW);
        c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    }
    void TearDown() override {
        c->audioController->SetTimingController(nullptr);
        ordinary_edit::TearDown();
    }
    void Karaoke() {
        SetKaraokeLine(karaoke,&LiveRead(*c).Events.front(),true);
        c->audioController->SetTimingController(CreateKaraokeTimingController(c.get(),&karaoke,file_slot));
    }
    AudioDisplay* Display() {
        auto display=new AudioDisplay(window,c->audioController.get(),c.get());
        c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
        display->SetSize(0,0,900,160);
        return display;
    }
    void Mouse(AudioDisplay* display,wxEventType type,int ms,bool down=false) {
        wxMouseEvent event(type);event.SetLeftDown(down);
        event.SetPosition({display->RelativeXFromTime(ms),80});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(audio_edit, studio_waveform_shades_only_selected_ranges_and_draws_a_db_scale) {
    // The dB scale: 0 dB at the edge, -6 dB at half the height, linear amplitude.
    auto levels = AudioWaveformRenderer::DbGrid(200, 1.f, 16);
    ASSERT_GE(levels.size(), 3u);
    EXPECT_EQ(0, levels[0].db);
    EXPECT_EQ(100, levels[0].offset);
    auto minus6 = std::find_if(levels.begin(), levels.end(), [](auto const& l) { return l.db == -6; });
    ASSERT_NE(levels.end(), minus6);
    EXPECT_EQ(50, minus6->offset);
    for (size_t i = 1; i < levels.size(); ++i) {
        EXPECT_LT(levels[i].db, levels[i - 1].db);
        EXPECT_GE(levels[i - 1].offset - levels[i].offset, 16);
    }
    EXPECT_GE(levels.back().offset, 16);
    // Zoomed in vertically, 0 dB is off the display.
    auto zoomed = AudioWaveformRenderer::DbGrid(200, 4.f, 16);
    ASSERT_FALSE(zoomed.empty());
    EXPECT_LT(zoomed.front().db, 0);
    EXPECT_TRUE(AudioWaveformRenderer::DbGrid(20, 1.f, 16).empty());

    c->project->LoadAudio("dummy-audio:noise?sr=44100&bd=16&ch=1&ln=441000");
    // Spread of lightness over the waveform's colours (not the black
    // background or the dark scale lines): one shade, or peaks and averages.
    // Lines are antialiased on some platforms, so blends of a shade appear too.
    auto spread = [&](std::string const& scheme, AudioRenderingStyle style) {
        AudioWaveformRenderer renderer(scheme);
        renderer.SetProvider(c->project->AudioProvider());
        renderer.SetMillisecondsPerPixel(10);
        renderer.SetAmplitudeScale(0.5f);
        wxBitmap bmp(32, 200, 24);
        renderer.Render(bmp, 0, style);
        auto image = bmp.ConvertToImage();
        int low = 255, high = 0;
        for (int y = 0; y < image.GetHeight(); ++y)
            for (int x = 0; x < image.GetWidth(); ++x) {
                int r = image.GetRed(x, y), g = image.GetGreen(x, y), b = image.GetBlue(x, y);
                int lightness = (std::max({r, g, b}) + std::min({r, g, b})) / 2;
                if (lightness < 80) continue;
                low = std::min(low, lightness);
                high = std::max(high, lightness);
            }
        return high < low ? 0 : high - low;
    };
    OPT_SET("Audio/Display/Waveform Style")->SetInt(0);
    OPT_SET("Audio/Display/Draw/dB Scale")->SetBool(true);
    EXPECT_LT(spread("Studio", AudioStyle_Normal), 40) << "one shade outside the selection";
    EXPECT_LT(spread("Studio", AudioStyle_Inactive), 40);
    EXPECT_GT(spread("Studio", AudioStyle_Primary), 80) << "peaks and averages in the selection";
    EXPECT_GT(spread("Studio", AudioStyle_Selected), 80);
    // The earlier schemes keep following the waveform style.
    EXPECT_LT(spread("Green", AudioStyle_Primary), 40);

    // Seconds lines: like the dB scale with Studio, unless the user chose a colour.
    {
        OPT_SET("Audio/Spectrum")->SetBool(false);
        OPT_SET("Audio/Display/Draw/Seconds")->SetBool(true);
        OPT_SET("Colour/Audio Display/Seconds Line")->Reset();
        OPT_SET("Colour/Audio Display/Waveform")->SetString("Studio");
        SecondsMarkerProvider seconds;
        AudioMarkerVector markers;
        seconds.GetMarkers(TimeRange(0, 3000), markers);
        ASSERT_FALSE(markers.empty());
        auto colour = [&] { return markers[0]->GetStyle().GetColour(); };
        EXPECT_EQ(AudioWaveformRenderer::DbGridColour(), colour());
        OPT_SET("Colour/Audio Display/Waveform")->SetString("Green");
        EXPECT_EQ(to_wx(OPT_GET("Colour/Audio Display/Seconds Line")->GetColor()), colour());
        OPT_SET("Colour/Audio Display/Waveform")->SetString("Studio");
        OPT_SET("Audio/Spectrum")->SetBool(true);
        EXPECT_NE(AudioWaveformRenderer::DbGridColour(), colour()) << "the spectrum keeps its colours";
        OPT_SET("Audio/Spectrum")->SetBool(false);
        OPT_SET("Colour/Audio Display/Seconds Line")->SetColor(agi::Color(200, 10, 10));
        EXPECT_EQ(wxColour(200, 10, 10), colour()) << "a colour the user chose wins";
        OPT_SET("Colour/Audio Display/Seconds Line")->Reset();
    }

    // A configuration saved before the scheme existed gets it once.
    OPT_SET("Audio/Colour Schemes")->SetListString({"Green", "Icy Blue"});
    OPT_SET("Colour/Audio Display/Waveform")->SetString("Green");
    MigrateAudioColourSchemes();
    EXPECT_EQ("Studio", OPT_GET("Audio/Colour Schemes")->GetListString().at(0));
    EXPECT_EQ("Studio", OPT_GET("Colour/Audio Display/Waveform")->GetString());
    OPT_SET("Colour/Audio Display/Waveform")->SetString("Green");
    MigrateAudioColourSchemes();
    EXPECT_EQ("Green", OPT_GET("Colour/Audio Display/Waveform")->GetString()) << "a later choice stays";
    EXPECT_EQ(3u, OPT_GET("Audio/Colour Schemes")->GetListString().size());

    // The first Studio's green ruler and scrollbar turn grey; a colour the
    // user picked stays.
    OPT_SET("Colour/Schemes/Studio/UI/Light")->SetColor(agi::Color(64, 220, 152));
    OPT_SET("Colour/Schemes/Studio/UI/Dark")->SetColor(agi::Color(0, 0, 0));
    OPT_SET("Colour/Schemes/Studio/UI Focused/Light")->SetColor(agi::Color(1, 2, 3));
    MigrateAudioColourSchemes();
    EXPECT_EQ(agi::Color(150, 150, 150), OPT_GET("Colour/Schemes/Studio/UI/Light")->GetColor());
    EXPECT_EQ(agi::Color(38, 38, 38), OPT_GET("Colour/Schemes/Studio/UI/Dark")->GetColor());
    EXPECT_EQ(agi::Color(1, 2, 3), OPT_GET("Colour/Schemes/Studio/UI Focused/Light")->GetColor());
    auto grey = OPT_GET("Colour/Schemes/Studio/UI/Light")->GetColor();
    EXPECT_TRUE(grey.r == grey.g && grey.g == grey.b) << "the ruler's ticks and seconds are grey";
}

TEST_F(audio_edit, db_scale_values_never_overlap) {
    // Any height, any vertical zoom, a large font (high display scale): the
    // values sit on their lines, inside the display and apart.
    OPT_SET("Audio/Spectrum")->SetBool(false);
    OPT_SET("Audio/Display/Draw/dB Scale")->SetBool(true);
    auto display=Display();
    wxClientDC dc(display);
    for (int point : {9, 16}) {
        wxFont font = display->GetFont(); font.SetPointSize(point); display->SetFont(font);
        dc.SetFont(font.Scaled(0.8f));
        for (int height = 60; height <= 400; height += 7) {
            for (float scale : {0.5f, 1.f, 1.7f, 4.f}) {
                display->SetSize(0,0,900,height+40);
                display->SetAmplitudeScale(scale);
                auto labels = display->DbLabelRects(dc);
                int top = display->GetClientSize().y; int bottom = 0;
                for (size_t i = 0; i < labels.size(); ++i) {
                    for (size_t j = i + 1; j < labels.size(); ++j)
                        EXPECT_FALSE(labels[i].rect.Intersects(labels[j].rect)) << point << "pt, height " << height << ", scale " << scale << ": " << labels[i].text << " and " << labels[j].text;
                    top = std::min(top, labels[i].rect.y); bottom = std::max(bottom, labels[i].rect.GetBottom());
                }
                if (!labels.empty()) EXPECT_LT(bottom, display->GetClientSize().y - 10) << "above the scrollbar";
            }
        }
        // Lines are at least a value apart, so the lines and values agree.
        EXPECT_GE(display->DbLabelGap(), dc.GetCharHeight());
    }
}

TEST_F(audio_edit, dialogue_manual_drag_cancel_never_writes_or_adds_history) {
    auto description=c->subsController->GetUndoDescription();
    auto token=Timing()->MarkerLifetime();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);ASSERT_EQ(1u,markers.size());
    Timing()->OnMarkerDrag(markers,1103,0);
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->EndDrag(true);
    EXPECT_TRUE(token.expired());
    EXPECT_EQ(1001,Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, dialogue_manual_commit_and_history_preserve_exact_milliseconds_and_identity) {
    auto id=LiveRead(*c).Events.front().Id;
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->subsController->Redo();EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    EXPECT_EQ(id,c->selectionController->ReadActiveLine()->Id);
}
TEST_F(audio_edit, continuous_autocommit_drag_keeps_marker_borrow_and_coalesces_undo) {
    OPT_SET("Audio/Auto/Commit")->SetBool(true);
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);ASSERT_FALSE(markers.empty());
    auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1051,0);Timing()->OnMarkerDrag(markers,1101,0);
    EXPECT_FALSE(token.expired());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    OPT_SET("Audio/Auto/Commit")->SetBool(false); // preference changed during a completed auto drag
    Timing()->EndDrag(false);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1101,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_EQ(1101,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, selection_change_invalidates_markers_and_releases_pending_task) {
    AssDialogue* next;
    {auto write=c->subtitleAccess->TryWrite();next=new AssDialogue;
     next->Start=3001;next->End=4002;LiveWriteForTest(*c).Events.push_back(*next);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_NEW);}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1111,0);
    c->selectionController->SetSelectionAndActiveIDs({next->Id}, next->Id);
    EXPECT_TRUE(token.expired());EXPECT_EQ(3001,Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, multiple_selected_rows_share_pending_lifetime_and_undo) {
    AssDialogue* next;
    {auto write=c->subtitleAccess->TryWrite();next=new AssDialogue;
     next->Start=3001;next->End=4002;LiveWriteForTest(*c).Events.push_back(*next);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_NEW);
     c->selectionController->SetSelectedIDs({(LiveRead(*c).Events.front()).Id,next->Id});}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnRightClick(4503,false,0,0);ASSERT_EQ(2u,markers.size());
    Timing()->EndDrag(false);Timing()->Commit();
    for(auto const& row:LiveRead(*c).Events)EXPECT_EQ(4503,row.End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ(4002,LiveRead(*c).Events.back().End.GetMilliseconds());
    EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
}
TEST_F(audio_edit, comparison_rejects_dialogue_and_karaoke_cache_changes) {
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    auto original=Timing()->GetActiveLineRange();
    EXPECT_FALSE(Timing()->BeginDrag());Timing()->ModifyStart(1);Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(original.begin(),Timing()->GetActiveLineRange().begin());
    comparison.reset();Karaoke();
    comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    auto text=karaoke.GetText();original=Timing()->GetActiveLineRange();
    EXPECT_FALSE(Timing()->BeginDrag());Timing()->AddLeadIn();Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_EQ(text,karaoke.GetText());EXPECT_EQ(original.begin(),Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, karaoke_lead_is_staged_cancelled_then_committed_with_undo) {
    Karaoke();auto text=LiveRead(*c).Events.front().Text.get();
    auto lead=OPT_GET("Audio/Lead/IN")->GetInt();
    Timing()->AddLeadIn();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->Revert();EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(1001-lead,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ(1001-lead,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, karaoke_drag_and_controller_switch_cancel_pending_cache) {
    Karaoke();auto text=LiveRead(*c).Events.front().Text.get();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1500,false,false,10,0);ASSERT_EQ(1u,markers.size());
    auto token=Timing()->MarkerLifetime();Timing()->OnMarkerDrag(markers,1600,0);
    EXPECT_NE(text,karaoke.GetText());EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    EXPECT_TRUE(token.expired());EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, real_wx_audio_drag_release_and_destroyed_controller_borrow) {
    auto display=Display();
    Mouse(display,wxEVT_LEFT_DOWN,1001,true);EXPECT_TRUE(display->HasCapture());
    Mouse(display,wxEVT_MOTION,1200,true);
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(display,wxEVT_LEFT_UP,1200);
    Timing()->Commit();EXPECT_GT(LiveRead(*c).Events.front().Start.GetMilliseconds(),1100);
    Mouse(display,wxEVT_LEFT_DOWN,1200,true);
    auto token=Timing()->MarkerLifetime();
    c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    EXPECT_TRUE(token.expired());EXPECT_FALSE(display->HasCapture());
    Mouse(display,wxEVT_MOTION,1400,true); // no callback into the deleted controller
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->project->CloseAudio();
}
TEST_F(audio_edit, real_wx_display_destruction_discards_uncommitted_drag) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);delete display;
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());c->project->CloseAudio();
}
TEST_F(audio_edit, real_wx_drag_exception_releases_task_and_capture) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    agi::signal::Connection observer=Timing()->AddMarkerMovedListener([] {throw std::runtime_error("marker observer");});
    EXPECT_THROW(Mouse(display,wxEVT_MOTION,1200,true),std::runtime_error);
    EXPECT_FALSE(display->HasCapture());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    observer.Disconnect();c->project->CloseAudio();
}
TEST_F(audio_edit, stale_revision_missing_target_and_document_change_never_write_back) {
    AudioEditSession edit(c.get());ASSERT_TRUE(edit.Begin());
    auto row=LiveRead(*c).Events.front(); // independent AssDialogue copy
    {auto scope=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Start=1002;LiveWriteForTest(*c).Commit("other",AssFile::COMMIT_DIAG_TIME);}
    EXPECT_FALSE(edit.Begin());
    EXPECT_EQ(1002,LiveRead(*c).Events.front().Start.GetMilliseconds());
    edit.Refresh();ASSERT_TRUE(edit.Begin());
    row.Id+=100000;
    EXPECT_THROW(edit.Update({row}),std::invalid_argument);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.Refresh();c->audioController->SetTimingController(nullptr);c->subtitleEditGate.Close();EXPECT_FALSE(edit.Begin());
}
TEST_F(audio_edit, outer_time_command_scope_is_reused_without_spurious_stale_rejection) {
    (*cmd::get("time/lead/in"))(c.get());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_LT(Timing()->GetActiveLineRange().begin(),1001);
    Timing()->Commit();EXPECT_LT(LiveRead(*c).Events.front().Start.GetMilliseconds(),1001);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, real_karaoke_split_buttons_cancel_apply_and_rebuild_after_history) {
    c->audioBox=new AudioBox(window,c.get());
    c->audioBox->SetSize(900,260);
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    c->karaoke->SetEnabled(true);
    wxWindow* panel=nullptr;wxWindow* accept=nullptr;wxWindow* cancel=nullptr;
    for(auto child:c->karaoke->GetChildren()) {
        if(dynamic_cast<wxPanel*>(child))panel=child;
        if(child->GetToolTipText()=="Commit splits")accept=child;
        if(child->GetToolTipText()=="Discard all uncommitted splits")cancel=child;
    }
    ASSERT_NE(nullptr,panel);ASSERT_NE(nullptr,accept);ASSERT_NE(nullptr,cancel);
    panel->SetSize(0,0,600,35);
    auto click=[&] {wxMouseEvent event(wxEVT_LEFT_DOWN);event.SetPosition({25,10});event.SetEventObject(panel);panel->GetEventHandler()->ProcessEvent(event);};
    auto text=LiveRead(*c).Events.front().Text.get();
    click();EXPECT_TRUE(cancel->IsEnabled());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    button(cancel,cancel->GetId());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    auto token=Timing()->MarkerLifetime();
    click();button(accept,accept->GetId());
    EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(token.expired());
    auto changed=LiveRead(*c).Events.front().Text.get();
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    click();button(accept,accept->GetId());
    EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
    comparison.reset();c->project->CloseAudio();
}
TEST_F(audio_edit, audio_source_close_invalidates_active_drag_and_releases_registration) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);auto token=Timing()->MarkerLifetime();
    c->project->CloseAudio();
    EXPECT_TRUE(token.expired());EXPECT_FALSE(display->HasCapture());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, linked_neighbor_is_authorized_and_restored_by_undo) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue;
     other->Start=2002;other->End=3003;LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_DIAG_ADDREM);}
    OPT_SET("Timing/Link Adjacent Lines")->SetBool(true);
    Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_EQ(2012,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ(2012,LiveRead(*c).Events.back().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(2002,LiveRead(*c).Events.back().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    auto session=c->subtitleAccess->TryBeginInteraction({LiveRead(*c).Events.front().Id});ASSERT_TRUE(session);
    // C1-C1c-13 (audit C6): the refused link is validated before the row
    // write, so the requested value is not left behind uncommitted.
    AssDialogueBase moved=LiveRead(*c).Events.front();moved.End=2050;
    EXPECT_THROW(session->UpdateRowsLinkingAdjacent({moved},{moved.Id,false,2002,100,true}),std::logic_error);
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.back().Start.GetMilliseconds());
    EXPECT_FALSE(session->Current());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, real_wx_capture_lost_cancels_pending_drag) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);display->ReleaseMouse();
    wxMouseCaptureLostEvent lost(display->GetId());
    display->GetEventHandler()->ProcessEvent(lost);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->project->CloseAudio();
}
TEST_F(audio_edit, throwing_cancellation_observer_cannot_keep_destroyed_cache_borrower) {
    Karaoke();ASSERT_TRUE(Timing()->BeginDrag());
    auto token=Timing()->MarkerLifetime();
    agi::signal::Connection throwing=Timing()->AddMarkerMovedListener([] {throw std::runtime_error("cancel");});
    EXPECT_THROW(c->audioController->SetTimingController(nullptr),std::runtime_error);
    EXPECT_EQ(nullptr,Timing());EXPECT_TRUE(token.expired());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, karaoke_continuous_auto_drag_coalesces_text_undo_without_rounding_row_times) {
    OPT_SET("Audio/Auto/Commit")->SetBool(true);Karaoke();
    auto text=LiveRead(*c).Events.front().Text.get();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1500,false,false,10,0);ASSERT_EQ(1u,markers.size());
    auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1600,0);Timing()->OnMarkerDrag(markers,1700,0);
    EXPECT_FALSE(token.expired());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->EndDrag(false);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    auto changed=LiveRead(*c).Events.front().Text.get();
    c->subsController->Undo();EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
}
TEST_F(audio_edit, inactive_neighbor_cache_survives_undo_node_replacement) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue;
     other->Start=3001;other->End=4002;LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_DIAG_ADDREM);}
    OPT_SET("Audio/Inactive Lines Display Mode")->SetInt(2);
    Timing()->ModifyStart(1);Timing()->Commit();
    c->subsController->Undo();c->subsController->Redo();
    EXPECT_EQ(1011,Timing()->GetActiveLineRange().begin());
    AudioMarkerVector markers;Timing()->GetMarkers(TimeRange(0,5000),markers);
    EXPECT_GE(markers.size(),4u);
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1021,LiveRead(*c).Events.front().Start.GetMilliseconds());
}

TEST_F(audio_edit, no_op_navigation_and_boundary_commands_do_not_stale_audio_cache) {
    {auto no_op=c->subtitleAccess->TryWrite();ASSERT_TRUE(no_op);}
    (*cmd::get("time/prev"))(c.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
    Karaoke();auto text=LiveRead(*c).Events.front().Text.get();
    (*cmd::get("time/prev"))(c.get());
    (*cmd::get("time/start/increase"))(c.get()); // first syllable: no movable left boundary
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    (*cmd::get("time/next"))(c.get());
    (*cmd::get("time/length/increase"))(c.get()); // last syllable: no movable right boundary
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    (*cmd::get("time/prev"))(c.get());
    Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, stale_pending_audio_task_stays_rejected_until_explicit_rebuild) {
    Timing()->ModifyStart(1);
    {auto foreign=c->subtitleEditGate.TryBeginWrite();ASSERT_TRUE(foreign);}
    Timing()->Commit();Timing()->Commit();
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Timing()->Revert();Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
}

TEST_F(audio_edit, karaoke_one_millisecond_lead_is_not_lost_to_tag_rounding) {
    auto lead=OPT_GET("Audio/Lead/IN")->GetInt();
    OPT_SET("Audio/Lead/IN")->SetInt(1);Karaoke();
    Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(1000,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    OPT_SET("Audio/Lead/IN")->SetInt(lead);
}

TEST_F(audio_edit, cancelling_an_untimed_row_cannot_be_committed_later) {
    {auto scope=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Start=0;LiveWriteForTest(*c).Events.front().End=0;
     LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_DIAG_TIME);}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnRightClick(1500,false,0,0);ASSERT_FALSE(markers.empty());
    Timing()->EndDrag(true);Timing()->Commit();
    EXPECT_EQ(0,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(0,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

class visual_edit : public ordinary_edit {
protected:
    VideoDisplay* display=nullptr;
    wxToolBar* toolbar=nullptr;
    void SetUp() override {
        ordinary_edit::SetUp();
        OPT_SET("Video/Subtitle Sync")->SetBool(false);
        c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
        ASSERT_NE(nullptr,c->project->VideoProvider());
        {auto scope=c->subtitleAccess->TryWrite();
         auto& row=LiveWriteForTest(*c).Events.front();row.Start=0;row.End=5000;
         row.Text="{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail";
         LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_FULL);}
        toolbar=new wxToolBar(window,wxID_ANY);
        auto zoom=new VideoZoomControl(window);
        display=new VideoDisplay(toolbar,false,zoom,window,c.get());
        display->SetSize(640,480);
        FrameData(); // finish renderer initialization with the main dispatch queue running
    }
    void TearDown() override {
        if(display) {delete display;display=nullptr;c->videoDisplay=nullptr;}
        c->project->CloseVideo();
        ordinary_edit::TearDown();
    }
    template<class Tool> Tool* Install() {
        auto tool=std::make_unique<Tool>(display,c.get());auto ptr=tool.get();
        display->SetTool(std::move(tool));
        ptr->SetCanvasSize(640,480);ptr->SetDisplayArea(0,0,640,480);
        return ptr;
    }
    void Mouse(wxEventType type,int x,int y,bool down=false,bool ctrl=false) {
        wxMouseEvent event(type);event.SetLeftDown(down);event.SetControlDown(ctrl);
        event.SetPosition({x,y});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    }
    void Drag(int x,int y,int x2,int y2) {
        Mouse(wxEVT_LEFT_DOWN,x,y,true);Mouse(wxEVT_MOTION,x2,y2,true);Mouse(wxEVT_LEFT_UP,x2,y2);
    }
    std::string Text() { return LiveRead(*c).Events.front().Text.get(); }
    std::vector<unsigned char> FrameData() {
        // Libass font setup may need the main dispatch queue. Keep the normal
        // wx loop responsive rather than synchronously waiting on that worker.
        wxEventLoop loop;
        wxEventLoopActivator activate(&loop);
        std::vector<unsigned char> data;
        std::exception_ptr error;
        auto provider=c->project->VideoProvider();
        std::thread reader([&] {
            try { data=provider->GetFrame(0,0)->data; }
            catch (...) { error=std::current_exception(); }
            window->CallAfter([&] { loop.Exit(); });
        });
        loop.Run();reader.join();
        if(error)std::rethrow_exception(error);
        return data;
    }
};
TEST_F(visual_edit, position_drag_previews_without_model_write_then_commits_once_and_undoes) {
    Install<VisualToolDrag>();auto text=Text();auto id=LiveRead(*c).Events.front().Id;
    auto before=FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);ASSERT_TRUE(display->HasCapture());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_MOTION,140,160,true);EXPECT_EQ(text,Text());
    auto preview=FrameData();
    EXPECT_NE(before,preview);
    Mouse(wxEVT_LEFT_UP,140,160);
    EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\pos(140,160)"));
    EXPECT_NE(std::string::npos,Text().find("\\bord3"));EXPECT_NE(std::string::npos,Text().find("中 文 {\\b1} tail"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    c->subsController->Undo();EXPECT_EQ(text,Text());EXPECT_EQ("visual fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_NE(text,Text());
}
TEST_F(visual_edit, capture_loss_discards_preview_without_undo_record) {
    Install<VisualToolDrag>();auto text=Text();
    auto before=FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,200,200,true);
    display->ReleaseMouse();wxMouseCaptureLostEvent lost(display->GetId());display->GetEventHandler()->ProcessEvent(lost);
    EXPECT_EQ(text,Text());EXPECT_EQ("visual fixture",c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(before,FrameData());
}
TEST_F(visual_edit, lost_button_release_cancels_instead_of_committing) {
    Install<VisualToolClip>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,50,50,true);Mouse(wxEVT_MOTION,200,200,true);
    Mouse(wxEVT_MOTION,210,210,false);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, all_standard_tools_reject_comparison_writes) {
    auto blocked=[&] {
        auto text=Text();auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        Drag(100,100,160,170);Mouse(wxEVT_LEFT_DCLICK,200,220,true);Mouse(wxEVT_LEFT_UP,200,220);
        EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());
    };
    Install<VisualToolCross>();blocked();Install<VisualToolDrag>();blocked();
    Install<VisualToolScale>();blocked();Install<VisualToolRotateXY>();blocked();
    Install<VisualToolRotateZ>();blocked();Install<VisualToolClip>();blocked();
    Install<VisualToolVectorClip>();blocked();Install<VisualToolRectangle>();blocked();
    EXPECT_EQ(1u,LiveRead(*c).Events.size());
}
TEST_F(visual_edit, scale_rotation_and_rectangular_clip_preserve_unedited_tags) {
    auto text=Text();Install<VisualToolScale>();Drag(200,200,240,160);
    EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\fscx"));EXPECT_NE(std::string::npos,Text().find("\\fscy"));
    Install<VisualToolRotateXY>();Drag(200,200,220,230);
    EXPECT_NE(std::string::npos,Text().find("\\frx"));EXPECT_NE(std::string::npos,Text().find("\\fry"));
    Install<VisualToolRotateZ>();Drag(200,200,250,180);EXPECT_NE(std::string::npos,Text().find("\\frz"));
    Install<VisualToolClip>();Drag(30,40,300,250);EXPECT_NE(std::string::npos,Text().find("\\clip(30,40,300,250)"));
    EXPECT_NE(std::string::npos,Text().find("\\bord3"));EXPECT_NE(std::string::npos,Text().find("中 文 {\\b1} tail"));
}
TEST_F(visual_edit, drawing_finish_inserts_before_source_with_style_and_preserves_identity) {
    Install<VisualToolRectangle>();auto source=LiveRead(*c).Events.front().Id;auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,100,150,true);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_UP,100,150);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());EXPECT_NE(source,LiveRead(*c).Events.front().Id);
    EXPECT_EQ(source,LiveRead(*c).Events.back().Id);EXPECT_EQ(text,LiveRead(*c).Events.back().Text.get());
    EXPECT_NE(nullptr,LiveRead(*c).GetStyle("图形"));EXPECT_EQ("图形",LiveRead(*c).Events.front().Style.get());
    c->subsController->Undo();EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("图形"));
    c->subsController->Redo();EXPECT_EQ(2u,LiveRead(*c).Events.size());
}
TEST_F(visual_edit, vector_clip_drag_and_cancel_rebuild_path_without_losing_inverse) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\iclip(m 50 50 l 200 50 200 200)\\bord3}字幕";LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_TEXT);}
    auto tool=Install<VisualToolVectorClip>();tool->SetSubTool(VCLIP_DRAG);auto text=Text();
    Drag(50,50,70,80);EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\iclip("));
    auto changed=Text();Mouse(wxEVT_LEFT_DOWN,70,80,true);Mouse(wxEVT_MOTION,90,100,true);
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(changed,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Undo();EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, tool_switch_and_window_close_cancel_pending_drag) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,200,200,true);
    Install<VisualToolScale>();EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,200,200,true);Mouse(wxEVT_MOTION,250,160,true);
    delete display;display=nullptr;c->videoDisplay=nullptr;
    EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, selection_change_and_delete_cancel_without_writing_other_row) {
    AssDialogue* other;
    {auto write=c->subtitleAccess->TryWrite();other=new AssDialogue;
     other->Text="{\\pos(300,300)}other";LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();auto text=Text();auto other_id=other->Id;
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,150,150,true);
    c->selectionController->SetSelectionAndActiveIDs({other->Id}, other->Id);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,300,300,true);Mouse(wxEVT_MOTION,350,350,true);
    (*cmd::get("edit/line/delete"))(c.get());
    Mouse(wxEVT_LEFT_UP,350,350);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_NE(other_id,LiveRead(*c).Events.front().Id);EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, foreign_generation_rejects_pending_result_and_releases_registration) {
    Install<VisualToolScale>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,200,200,true);Mouse(wxEVT_MOTION,230,180,true);
    {auto foreign=c->subtitleEditGate.TryBeginWrite();ASSERT_TRUE(foreign);}
    Mouse(wxEVT_LEFT_UP,230,180);
    EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Drag(200,200,240,160);EXPECT_NE(text,Text());
}
TEST_F(visual_edit, video_seek_replacement_and_close_cancel_old_previews) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    c->videoController->JumpToFrame(2);EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    c->project->LoadVideo("?dummy:25:200:640:480:96:96:96:");
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,180,190,true);
    c->project->CloseVideo();EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, continuous_drags_have_separate_undo_records_and_refresh_after_redo) {
    Install<VisualToolDrag>();auto text=Text();
    Drag(100,100,150,150);auto first=Text();Drag(150,150,200,180);auto second=Text();
    EXPECT_NE(text,first);EXPECT_NE(first,second);
    c->subsController->Undo();EXPECT_EQ(first,Text());c->subsController->Undo();EXPECT_EQ(text,Text());
    c->subsController->Redo();EXPECT_EQ(first,Text());c->subsController->Redo();EXPECT_EQ(second,Text());
    Drag(200,180,220,200);EXPECT_NE(second,Text());
}
TEST_F(visual_edit, multirow_position_changes_preserve_metadata_extras_and_binding) {
    int first_id,second_id;std::string group;
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue(LiveRead(*c).Events.front());
     row->Text="{\\pos(300,300)\\blur2}other";LiveWriteForTest(*c).Events.push_back(*row);
     first_id=LiveRead(*c).Events.front().Id;second_id=row->Id;
     auto extra=LiveWriteForTest(*c).AddExtradata("unknown-test","opaque value");
     LiveWriteForTest(*c).Events.front().Actor="actor";LiveWriteForTest(*c).Events.front().Layer=7;LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{extra};
     LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);
     c->selectionController->SetSelectedIDs({(LiveRead(*c).Events.front()).Id,row->Id});BindSelectedSubtitles(c.get());
     group=GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front());}
    auto extras=LiveRead(*c).Extradata;Install<VisualToolDrag>();
    Drag(100,100,130,140);
    EXPECT_EQ(first_id,LiveRead(*c).Events.front().Id);EXPECT_EQ(second_id,LiveRead(*c).Events.back().Id);
    EXPECT_EQ(7,LiveRead(*c).Events.front().Layer);EXPECT_EQ("actor",LiveRead(*c).Events.front().Actor.get());
    EXPECT_EQ(group,GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()));
    EXPECT_NE(std::string::npos,LiveRead(*c).Events.back().Text.get().find("\\pos(330,340)"));
    ASSERT_EQ(extras.size(),LiveRead(*c).Extradata.size());
    for(size_t i=0;i<extras.size();++i){EXPECT_EQ(extras[i].key,LiveRead(*c).Extradata[i].key);EXPECT_EQ(extras[i].value,LiveRead(*c).Extradata[i].value);}
}
TEST_F(visual_edit, selecting_another_visible_line_with_subtitle_sync_keeps_feature_alive) {
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;
     row->Start=1000;row->End=5000;row->Text="{\\pos(300,300)}other";LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    c->videoController->JumpToFrame(50);OPT_SET("Video/Subtitle Sync")->SetBool(true);
    Install<VisualToolDrag>();auto first=Text();
    Drag(300,300,340,350);
    EXPECT_EQ(first,Text());EXPECT_NE(std::string::npos,LiveRead(*c).Events.back().Text.get().find("\\pos(340,350)"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, vector_tools_insert_convert_remove_and_freehand_use_authorized_completion) {
    auto tool=Install<VisualToolVectorClip>();tool->SetSubTool(VCLIP_LINE);
    Drag(40,40,40,40);Drag(200,50,200,50);Drag(200,200,200,200);
    auto path=Text();EXPECT_NE(std::string::npos,path.find("\\clip("));
    tool->SetSubTool(VCLIP_CONVERT);Mouse(wxEVT_LEFT_DOWN,120,45,true);Mouse(wxEVT_LEFT_UP,120,45);
    EXPECT_NE(path,Text());auto converted=Text();
    tool->SetSubTool(VCLIP_INSERT);Mouse(wxEVT_LEFT_DOWN,120,45,true);Mouse(wxEVT_LEFT_UP,120,45);
    EXPECT_NE(converted,Text());
    tool->SetSubTool(VCLIP_REMOVE);Mouse(wxEVT_LEFT_DOWN,40,40,true);Mouse(wxEVT_LEFT_UP,40,40);
    tool->SetSubTool(VCLIP_FREEHAND_SMOOTH);Mouse(wxEVT_LEFT_DOWN,40,40,true);
    Mouse(wxEVT_MOTION,120,100,true);Mouse(wxEVT_MOTION,240,180,true);Mouse(wxEVT_LEFT_UP,260,200);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(VCLIP_DRAG,tool->GetSubTool());
}
TEST_F(visual_edit, rectangle_escape_polygon_and_existing_style_are_safe) {
    auto tool=Install<VisualToolRectangle>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,90,100,true);
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ(text,Text());EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("图形"));
    tool->SetSubTool(static_cast<int>(DrawingShape::Polygon));
    Mouse(wxEVT_LEFT_DOWN,20,20,true);Mouse(wxEVT_LEFT_UP,20,20);
    Mouse(wxEVT_LEFT_DOWN,100,20,true);Mouse(wxEVT_LEFT_UP,100,20);
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_LEFT_UP,100,100);
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    wxKeyEvent enter(wxEVT_CHAR_HOOK);enter.m_keyCode=WXK_RETURN;display->GetEventHandler()->ProcessEvent(enter);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());auto style=LiveRead(*c).GetStyle("图形");ASSERT_NE(nullptr,style);auto data=style->GetEntryData();
    tool->SetSubTool(static_cast<int>(DrawingShape::Ellipse));Drag(200,200,240,260);
    EXPECT_EQ(3u,LiveRead(*c).Events.size());EXPECT_EQ(data,LiveRead(*c).GetStyle("图形")->GetEntryData());
}
TEST_F(visual_edit, commit_observer_exception_ends_task_and_preserves_normal_undo) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    agi::signal::Connection observer=LiveRead(*c).AddCommitListener([]{throw std::runtime_error("observer");});
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,160,170),std::runtime_error);
    EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    observer.Disconnect();c->subsController->Undo();EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, no_op_ordinary_scope_does_not_drop_next_visual_gesture) {
    Install<VisualToolScale>();auto text=Text();
    {auto ordinary=c->subtitleAccess->TryWrite();ASSERT_TRUE(ordinary);}
    Drag(200,200,230,170);EXPECT_NE(text,Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, toolbar_callbacks_detach_on_tool_destruction) {
    Install<VisualToolDrag>();auto id=toolbar->GetToolByPos(1)->GetId();
    wxCommandEvent toggle(wxEVT_TOOL,id);toolbar->GetEventHandler()->ProcessEvent(toggle);
    EXPECT_NE(std::string::npos,Text().find("\\move("));
    auto text=Text();Install<VisualToolCross>();
    toolbar->GetEventHandler()->ProcessEvent(toggle);EXPECT_EQ(text,Text());
    Install<VisualToolVectorClip>();Install<VisualToolCross>();
    wxCommandEvent old_clip(wxEVT_TOOL,1301);toolbar->GetEventHandler()->ProcessEvent(old_clip);
    EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, rejected_structural_insertion_does_not_add_style_or_row) {
    auto session=c->subtitleAccess->TryBeginInteraction({LiveRead(*c).Events.front().Id});ASSERT_TRUE(session);
    AssDialogue drawing;drawing.Id=LiveRead(*c).Events.front().Id;AssStyle style;style.name="must-not-be-added";
    auto count=LiveRead(*c).Styles.size();
    EXPECT_THROW(session->InsertBefore(drawing.Id,drawing,&style),std::invalid_argument);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ(count,LiveRead(*c).Styles.size());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, outside_video_rectangle_click_does_not_leave_a_write_task) {
    Install<VisualToolRectangle>();Mouse(wxEVT_LEFT_DOWN,-10,-10,true);Mouse(wxEVT_LEFT_UP,-10,-10);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(1u,LiveRead(*c).Events.size());
}
TEST_F(visual_edit, cross_double_click_preserves_move_origin_and_unrelated_tags) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\move(100,100,200,200,10,900)\\org(80,80)\\fax0.2\\i1}字幕";LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_TEXT);}
    Install<VisualToolCross>();Mouse(wxEVT_LEFT_DCLICK,150,160,true);Mouse(wxEVT_LEFT_UP,150,160);
    EXPECT_NE(std::string::npos,Text().find("\\move(150,160,250,260,10,900)"));
    EXPECT_NE(std::string::npos,Text().find("\\org(130,140)"));EXPECT_NE(std::string::npos,Text().find("\\fax0.2"));
}
TEST_F(visual_edit, replacing_document_content_invalidates_old_geometry_and_target_ids) {
    Install<VisualToolDrag>();auto previous=LiveRead(*c).Events.front().Id;
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    {auto write=c->subtitleAccess->TryWrite();AssFile replacement;replacement.LoadDefault();
     replacement.SetScriptInfo("PlayResX","640");replacement.SetScriptInfo("PlayResY","480");
     replacement.Events.front().Text="{\\pos(300,300)}new document";LiveWriteForTest(*c).swap(replacement);
     LiveWriteForTest(*c).Commit("new document",AssFile::COMMIT_NEW);
     auto active=&LiveRead(*c).Events.front();c->selectionController->SetSelectionAndActiveIDs({active->Id}, active->Id);}
    Mouse(wxEVT_LEFT_UP,160,170);
    EXPECT_NE(previous,LiveRead(*c).Events.front().Id);EXPECT_EQ("{\\pos(300,300)}new document",Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Drag(300,300,350,350);EXPECT_NE(std::string::npos,Text().find("\\pos(350,350)"));
}
TEST_F(visual_edit, feature_hit_order_follows_document_order_after_sort) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue(LiveRead(*c).Events.front());
     LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Events.reverse();LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_ORDER);}
    auto expected=LiveRead(*c).Events.back().Id;auto untouched=LiveRead(*c).Events.front().Text.get();
    Install<VisualToolDrag>();Drag(100,100,160,170);
    EXPECT_EQ(expected,c->selectionController->ReadActiveLine()->Id);
    EXPECT_EQ(untouched,LiveRead(*c).Events.front().Text.get());EXPECT_NE(untouched,LiveRead(*c).Events.back().Text.get());
}
TEST_F(visual_edit, missing_target_without_commit_cannot_partially_write_other_rows) {
    AssDialogue* other;
    {auto write=c->subtitleAccess->TryWrite();other=new AssDialogue;other->Text="untouched";
     LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    // Deliberately simulate a not-yet-migrated C1-C caller which removes a row without Commit.
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.erase_and_dispose(LiveWriteForTest(*c).Events.begin(),[](auto row){delete row;});}
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,160,170),std::out_of_range);
    EXPECT_EQ("untouched",Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->selectionController->SetSelectionAndActiveIDs({other->Id}, other->Id);
}
class VisualAccessProbe : public VisualTool<VisualDraggableFeature> {
public:
    using VisualTool::VisualTool;
    void Draw() override {}
    void Override() { SetSelectedOverride("\\pos","(200,200)"); }
};
TEST_F(visual_edit, override_interface_rejects_calls_without_interaction) {
    auto tool=std::make_unique<VisualAccessProbe>(display,c.get());auto probe=tool.get();
    display->SetTool(std::move(tool));auto text=Text();
    EXPECT_THROW(probe->Override(),std::logic_error);EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, invalid_preview_batch_changes_neither_renderer_nor_live_model) {
    auto before=FrameData();auto text=Text();
    AssDialogueBase valid=LiveRead(*c).Events.front();valid.Text="{\\pos(500,400)}different";
    auto missing=valid;missing.Id+=100000;
    c->project->VideoProvider()->PreviewSubtitles({valid,missing});
    EXPECT_EQ(before,FrameData());EXPECT_EQ(text,Text());
    c->project->VideoProvider()->PreviewSubtitles({valid,valid});EXPECT_EQ(before,FrameData());
}
TEST_F(visual_edit, ten_thousand_rows_keep_mouse_motion_local_to_edited_targets) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=1;i<10000;++i){auto row=new AssDialogue;
     row->Start=10000;row->End=15000;row->Text="inactive subtitle";LiveWriteForTest(*c).Events.push_back(*row);}
     LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);
    auto start=std::chrono::steady_clock::now();
    for(int i=1;i<=100;++i)Mouse(wxEVT_MOTION,100+i,100+i,true);
    auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("B2 10000 rows, 100 mouse moves: %.3f ms UI dispatch\n",milliseconds);
    EXPECT_LT(milliseconds,10000);EXPECT_EQ("{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail",Text());
    Mouse(wxEVT_LEFT_UP,200,200);EXPECT_EQ(10000u,LiveRead(*c).Events.size());
    EXPECT_NE(std::string::npos,Text().find("\\pos(200,200)"));
    EXPECT_EQ("inactive subtitle",LiveRead(*c).Events.back().Text.get());
}

TEST_F(visual_edit, toolbar_can_be_destroyed_before_its_tool_and_canvas) {
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,140,150,true);
    delete toolbar;toolbar=nullptr;
    delete display;display=nullptr;c->videoDisplay=nullptr;
    EXPECT_EQ("{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail",Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, changing_vector_subtool_cancels_uncommitted_spline) {
    auto tool=Install<VisualToolVectorClip>();tool->SetSubTool(VCLIP_LINE);auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,50,50,true);Mouse(wxEVT_MOTION,150,150,true);
    tool->SetSubTool(VCLIP_DRAG);Mouse(wxEVT_LEFT_UP,150,150);
    EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, ass_save_reopen_contains_only_confirmed_visual_edits_and_drawing_style) {
    Install<VisualToolRectangle>();Drag(30,40,120,150);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());auto shape=LiveRead(*c).Events.front().Text.get();
    Install<VisualToolDrag>();
    auto save=agi::fs::path((std::filesystem::current_path()/"visual-result.ass").wstring());
    {auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile reopened;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&reopened,save,c->project->Timecodes(),"utf-8");
    ASSERT_EQ(2u,reopened.Events.size());EXPECT_EQ(shape,reopened.Events.front().Text.get());
    EXPECT_NE(nullptr,reopened.GetStyle("图形"));
    auto saved=LiveRead(*c).Events.front().Text.get();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,60,70,true);
    {auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile pending;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&pending,save,c->project->Timecodes(),"utf-8");
    EXPECT_EQ(saved,pending.Events.front().Text.get());
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(saved,LiveRead(*c).Events.front().Text.get());
}

TEST_F(visual_edit, entering_preview_pan_mode_cancels_visual_edit_and_releases_capture) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,140,150,true);
    display->SetPanMode(true);Mouse(wxEVT_LEFT_UP,140,150);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, unnotified_metadata_and_one_millisecond_changes_are_not_overwritten) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,140,150,true);
    {auto legacy=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Layer=7;LiveWriteForTest(*c).Events.front().Start=1;}
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,140,150),std::logic_error);
    EXPECT_EQ(text,Text());EXPECT_EQ(7,LiveRead(*c).Events.front().Layer);
    EXPECT_EQ(1,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(ordinary_edit, style_panel_resizes_shapes) {
    auto Text=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
     row.Text="{\\an7\\pos(30,40)\\p1}m 0 0 l 90 0 90 110 0 110{\\p0}";
     LiveWriteForTest(*c).Commit("shape",AssFile::COMMIT_DIAG_TEXT);}
    {auto id=LiveRead(*c).Events.front().Id;c->selectionController->SetSelectionAndActiveIDs({id},id);}
    auto text=Text();
    ASSERT_TRUE(CanEditDrawing(c.get()));
    auto size=ShapeSize(c.get());ASSERT_TRUE(size);
    // The panel's Shape section, only for a shape
    auto panel=new StylePanel(window,c.get());
    BOOST_SCOPE_EXIT_ALL(&){panel->Destroy();};
    panel->UpdateNow();
    EXPECT_TRUE(panel->IsSectionVisible("shape"));
    EXPECT_EQ(wxString::Format("%g",std::round(size->first*100)/100),panel->Shown()["shape_width"]);
    // A drag is one undo step; the points are scaled, no \fscx
    panel->EnterNumber("shape_width",size->first+10,false);
    panel->EnterNumber("shape_width",size->first+20,true);
    EXPECT_NEAR(size->first+20,ShapeSize(c.get())->first,0.01);
    EXPECT_NEAR(size->second,ShapeSize(c.get())->second,0.01) << "the height stays";
    EXPECT_EQ(std::string::npos,Text().find("\\fscx"));
    panel->EnterNumber("shape_height",size->second/2,true);
    EXPECT_NEAR(size->second/2,ShapeSize(c.get())->second,0.01);
    auto changed=Text();
    c->subsController->Undo();EXPECT_NEAR(size->first+20,ShapeSize(c.get())->first,0.01);EXPECT_NEAR(size->second,ShapeSize(c.get())->second,0.01);
    c->subsController->Undo();EXPECT_EQ(text,Text()) << "the whole drag in one step";
    c->subsController->Redo();c->subsController->Redo();EXPECT_EQ(changed,Text());
    // Scale tags are baked into the points
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
     row.Text="{\\an7\\pos(10,10)\\fscx200\\p1}m 0 0 l 50 0 50 20 0 20{\\p0}";
     LiveWriteForTest(*c).Commit("scaled",AssFile::COMMIT_DIAG_TEXT);}
    EXPECT_NEAR(100,ShapeSize(c.get())->first,0.01);
    EXPECT_NE(-1,ResizeShape(c.get(),60,std::nullopt));
    EXPECT_EQ(std::string::npos,Text().find("\\fscx")) << Text();
    EXPECT_NE(std::string::npos,Text().find("l 60 0 60 20 0 20")) << "the points carry the scale: " << Text();
    EXPECT_NEAR(60,ShapeSize(c.get())->first,0.01);
    EXPECT_NEAR(20,ShapeSize(c.get())->second,0.01);
    // Not while another edit holds the subtitles
    auto before=Text();
    {auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
     EXPECT_EQ(-1,ResizeShape(c.get(),40,std::nullopt));EXPECT_EQ(before,Text());}
    // Text is not a shape: no section
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="Hello";
     LiveWriteForTest(*c).Commit("text",AssFile::COMMIT_DIAG_TEXT);}
    panel->UpdateNow();
    EXPECT_FALSE(CanEditDrawing(c.get()));
    EXPECT_FALSE(panel->IsSectionVisible("shape"));
}
TEST_F(ordinary_edit, dialog_snapshot_closes_with_parent_and_rejects_duplicate_batch_before_writing) {
    SubtitleDialogEdit edit(c.get());edit.ResetActive();ASSERT_TRUE(edit.Begin());
    auto row=edit.Rows().front();row.Text="must not be written";
    EXPECT_THROW(edit.Commit({row,row},"duplicate",AssFile::COMMIT_DIAG_TEXT),std::invalid_argument);
    EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.ResetActive();delete window;window=nullptr;
    EXPECT_FALSE(edit.Alive());EXPECT_FALSE(edit.Begin());
}
TEST_F(ordinary_edit, styling_dialog_commits_owned_style_and_rejects_stale_definitions) {
    {auto write=c->subtitleAccess->TryWrite();auto style=new AssStyle();style->name="Other";LiveWriteForTest(*c).Styles.push_back(*style);LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_STYLES);}
    DialogStyling dialog(c.get());wxTextCtrl* name=nullptr;
    for(auto child:descendants(&dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child);text && (text->GetWindowStyleFlag() & wxTE_PROCESS_ENTER))name=text;
    ASSERT_NE(nullptr,name);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    name->SetValue("Other");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());dialog.Commit(false);
    EXPECT_EQ("Other",LiveRead(*c).Events.front().Style.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Undo();EXPECT_EQ("Default",LiveRead(*c).Events.front().Style.get());
    c->subsController->Redo();EXPECT_EQ("Other",LiveRead(*c).Events.front().Style.get());
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Styles.front().fontsize+=1;}
    AcceptMessages accept;name->ChangeValue("Default");dialog.Commit(false);EXPECT_EQ("Other",LiveRead(*c).Events.front().Style.get());
}
TEST_F(ordinary_edit, styling_dialog_comparison_denial_and_draft_cancel_do_not_write) {
    {DialogStyling dialog(c.get());auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);AcceptMessages accept;dialog.Commit(false);EXPECT_EQ("Default",LiveRead(*c).Events.front().Style.get());}
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(ordinary_edit, translation_dialog_preserves_tags_ids_and_per_line_undo) {
    int second;
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\i1}one";auto row=new AssDialogue();row->Text="{\\b1}two";second=row->Id;LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    auto first=LiveRead(*c).Events.front().Id;DialogTranslation dialog(c.get());
    SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;
    ASSERT_NE(nullptr,target);target->SetTextRaw("中 文");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());dialog.Commit(true);
    EXPECT_EQ(first,LiveRead(*c).Events.front().Id);EXPECT_EQ("{\\i1}中 文",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(second,c->selectionController->ReadActiveLine()->Id);
    target->SetTextRaw("译文");dialog.Commit(false);EXPECT_EQ("{\\b1}译文",LiveRead(*c).Events.back().Text.get());
    c->subsController->Undo();EXPECT_EQ("{\\b1}two",LiveRead(*c).Events.back().Text.get());EXPECT_EQ("{\\i1}中 文",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("{\\b1}译文",LiveRead(*c).Events.back().Text.get());
}
TEST_F(ordinary_edit, translation_dialog_rejects_stale_row_and_releases_draft_on_close) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="source";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    {
        DialogTranslation dialog(c.get());dialog.InsertOriginal();EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
        {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="external";LiveWriteForTest(*c).Commit("external",AssFile::COMMIT_DIAG_TEXT);}
        AcceptMessages accept;dialog.Commit(false);EXPECT_EQ("external",LiveRead(*c).Events.front().Text.get());
    }
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(ordinary_edit, kanji_dialog_validates_all_targets_and_commits_once_or_discards) {
    std::vector<int> ids;
    {auto write=c->subtitleAccess->TryWrite();auto style=new AssStyle();style->name="Target";LiveWriteForTest(*c).Styles.push_back(*style);
        LiveWriteForTest(*c).Events.front().Text=R"({\k10}a)";ids.push_back(LiveWriteForTest(*c).Events.front().Id);
        for(int i=1;i<4;++i){auto row=new AssDialogue();row->Style=i%2?"Target":"Default";row->Text=i%2?"字":R"({\k20}b)";ids.push_back(row->Id);LiveWriteForTest(*c).Events.push_back(*row);}
        LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM|AssFile::COMMIT_STYLES);
    }
    auto text=[&](int id){for(auto const& row:LiveRead(*c).Events)if(row.Id==id)return row.Text.get();return std::string();};
    auto prepare=[&](wxDialog* dialog){
        std::vector<wxComboBox*> combos;for(auto child:descendants(dialog))if(auto combo=dynamic_cast<wxComboBox*>(child))combos.push_back(combo);
        ASSERT_EQ(2u,combos.size());combos[0]->SetStringSelection("Default");combos[1]->SetStringSelection("Target");
    };
    auto press=[&](wxDialog* dialog,wxString label){for(auto child:descendants(dialog))if(auto b=dynamic_cast<wxButton*>(child);b && b->GetLabel()==label){button(b,b->GetId());return;}FAIL()<<"Button missing";};
    {RunModal accept("Kanji timer",[&](wxDialog* dialog){prepare(dialog);press(dialog,"S&tart!");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());for(int i=0;i<2;++i){press(dialog,"&Link");press(dialog,"&Accept Line");}EXPECT_EQ("字",text(ids[1]));press(dialog,"&Close");});ShowKanjiTimerDialog(c.get());}
    EXPECT_EQ(R"({\k10}字)",text(ids[1]));EXPECT_EQ(R"({\k20}字)",text(ids[3]));EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Undo();EXPECT_EQ("字",text(ids[1]));EXPECT_EQ("字",text(ids[3]));c->subsController->Redo();EXPECT_EQ(R"({\k20}字)",text(ids[3]));c->subsController->Undo();
    {RunModal cancel("Kanji timer",[&](wxDialog* dialog){prepare(dialog);press(dialog,"S&tart!");press(dialog,"&Link");press(dialog,"&Accept Line");wxCloseEvent e(wxEVT_CLOSE_WINDOW);dialog->GetEventHandler()->ProcessEvent(e);});ShowKanjiTimerDialog(c.get());}
    EXPECT_EQ("字",text(ids[1]));EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    {RunModal stale("Kanji timer",[&](wxDialog* dialog){prepare(dialog);press(dialog,"S&tart!");for(int i=0;i<2;++i){press(dialog,"&Link");press(dialog,"&Accept Line");}
        {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.back().Text="external";LiveWriteForTest(*c).Commit("external",AssFile::COMMIT_DIAG_TEXT);}
        AcceptMessages messages;press(dialog,"&Close");EXPECT_TRUE(dialog->IsShown());});ShowKanjiTimerDialog(c.get());}
    EXPECT_EQ("字",text(ids[1]));EXPECT_EQ("external",text(ids[3]));EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, video_properties_prompt_is_detached_and_preserves_resample_undo) {
    {std::ofstream video("properties.y4m",std::ios::binary);video<<"YUV4MPEG2 W32 H24 F25:1 Ip A1:1 C420jpeg\nFRAME\n";std::string pixels(32*24*3/2,'\x80');video.write(pixels.data(),pixels.size());}
    OPT_SET("Video/No YCbCr Matrix in Script")->SetInt(0);OPT_SET("Video/PlayRes Mismatch")->SetInt(0);OPT_SET("Video/No LayoutRes in Script")->SetInt(0);OPT_SET("Video/LayoutRes Mismatch")->SetInt(0);
    AcceptMessages messages;c->project->LoadVideo("properties.y4m");ASSERT_NE(nullptr,c->project->VideoProvider());FrameData();
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).SetScriptInfo("PlayResX","80");LiveWriteForTest(*c).SetScriptInfo("PlayResY","60");LiveWriteForTest(*c).Events.front().Text=R"({\pos(20,20)\bord4}中 文)";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_SCRIPTINFO|AssFile::COMMIT_DIAG_TEXT);}
    auto id=LiveRead(*c).Events.front().Id;auto pointer=&LiveRead(*c).Events.front();auto text=LiveRead(*c).Events.front().Text.get();auto size=LiveRead(*c).Styles.front().fontsize;
    OPT_SET("Video/PlayRes Mismatch")->SetInt(1);
    {RunModal choose("Resolution mismatch",[&](wxDialog* dialog){EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
        for(auto child:descendants(dialog))if(auto list=dynamic_cast<wxListBox*>(child))list->SetSelection(1);button(dialog,wxID_OK);
    });UpdateVideoProperties(c.get(),c->project->VideoProvider());}
    EXPECT_EQ(32,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());EXPECT_DOUBLE_EQ(std::round(size*0.4),LiveRead(*c).Styles.front().fontsize);EXPECT_EQ(id,LiveRead(*c).Events.front().Id);EXPECT_EQ(pointer,&LiveRead(*c).Events.front());
    auto changed=LiveRead(*c).Events.front().Text.get();c->subsController->Undo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());c->subsController->Undo();EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());EXPECT_EQ(80,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));c->subsController->Redo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
}
TEST_F(visual_edit, video_properties_denies_comparison_without_changing_script_metadata) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).SetScriptInfo("PlayResX","0");LiveWriteForTest(*c).SetScriptInfo("PlayResY","0");LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_SCRIPTINFO);}
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);AcceptMessages messages;UpdateVideoProperties(c.get(),c->project->VideoProvider());
    EXPECT_EQ(0,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));
}

TEST_F(ordinary_edit, dialog_batch_rejects_reordered_missing_or_invalid_rows_before_any_write) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=0;i<2;++i)LiveWriteForTest(*c).Events.push_back(*new AssDialogue());LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    SubtitleDialogEdit edit(c.get());edit.ResetAll();auto updates=edit.Rows();updates.front().Text="first";updates.back().Text="last";
    updates.back().Row=99;EXPECT_THROW(edit.Commit(updates,"bad order",AssFile::COMMIT_DIAG_TEXT),std::invalid_argument);EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.ResetAll();updates=edit.Rows();updates.front().Text="first";updates.back().Text="last";
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.splice(LiveWriteForTest(*c).Events.begin(),LiveWriteForTest(*c).Events,LiveWriteForTest(*c).iterator_to(LiveWriteForTest(*c).Events.back()));}
    EXPECT_FALSE(edit.Commit(updates,"reordered",AssFile::COMMIT_DIAG_TEXT));for(auto const& row:LiveRead(*c).Events)EXPECT_EQ("",row.Text.get());
    edit.ResetAll();updates=edit.Rows();updates.front().Text="first";
    {auto write=c->subtitleAccess->TryWrite();delete &LiveRead(*c).Events.back();}
    EXPECT_FALSE(edit.Commit(updates,"removed",AssFile::COMMIT_DIAG_TEXT));EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(ordinary_edit, translation_dialog_repeated_preview_does_not_reinterpret_inserted_tags_as_blocks) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text=R"({\i1}source)";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    DialogTranslation dialog(c.get());SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;
    ASSERT_NE(nullptr,target);target->SetTextRaw(R"({\b1}译文)");dialog.Commit(false);EXPECT_EQ(R"({\i1}{\b1}译文)",LiveRead(*c).Events.front().Text.get());
    target->SetTextRaw("second");dialog.Commit(false);EXPECT_EQ(R"({\i1}second)",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, dialog_noncontiguous_batch_preserves_unselected_fields_and_releases_on_notification_exception) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=0;i<2;++i)LiveWriteForTest(*c).Events.push_back(*new AssDialogue());LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    SubtitleDialogEdit edit(c.get());auto first=LiveRead(*c).Events.front().Id,last=LiveRead(*c).Events.back().Id;edit.Reset({first,last});auto updates=edit.Rows();updates.front().Text="first";updates.back().Text="last";
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([]{throw std::runtime_error("observer");});
    EXPECT_THROW(edit.Commit(updates,"complete batch",AssFile::COMMIT_DIAG_TEXT),std::runtime_error);
    EXPECT_EQ("first",LiveRead(*c).Events.front().Text.get());EXPECT_EQ("last",LiveRead(*c).Events.back().Text.get());EXPECT_EQ("",std::next(LiveRead(*c).Events.begin())->Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    throwing.Disconnect();c->subsController->Undo();for(auto const& row:LiveRead(*c).Events)EXPECT_EQ("",row.Text.get());
}

TEST_F(ordinary_edit, translation_dialog_persists_only_confirmed_text_and_keeps_metadata) {
    uint32_t extra;
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();row.Text="source";row.Comment=true;row.Layer=3;row.Start=1;row.End=2002;row.Actor="actor";row.Effect="effect";extra=LiveWriteForTest(*c).AddExtradata("unknown","payload");row.ExtradataIds=std::vector<uint32_t>{extra};LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_FULL);}
    auto id=LiveRead(*c).Events.front().Id;
    {DialogTranslation dialog(c.get());SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;ASSERT_NE(nullptr,target);target->SetTextRaw("中 文");dialog.Commit(false);target->SetTextRaw("unconfirmed");}
    auto const& row=LiveRead(*c).Events.front();EXPECT_EQ(id,row.Id);EXPECT_EQ(1,row.Start.GetMilliseconds());EXPECT_EQ(2002,row.End.GetMilliseconds());EXPECT_TRUE(row.Comment);EXPECT_EQ(3,row.Layer);EXPECT_EQ("actor",row.Actor.get());EXPECT_EQ("effect",row.Effect.get());EXPECT_EQ(std::vector<uint32_t>{extra},row.ExtradataIds.get());
    auto save=agi::fs::path((std::filesystem::current_path()/"dialog-result.ass").wstring());{auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile reopened;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&reopened,save,c->project->Timecodes(),"utf-8");EXPECT_EQ("中 文",reopened.Events.front().Text.get());ASSERT_EQ(1u,reopened.Extradata.size());EXPECT_EQ("payload",reopened.Extradata.front().value);
}
TEST_F(ordinary_edit, dialog_dependency_changes_and_explicit_hide_release_pending_work) {
    SubtitleDialogEdit edit(c.get());edit.ResetActive();
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Start=1;}
    EXPECT_FALSE(edit.Begin());edit.ResetActive();
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Styles.front().fontsize+=1;}
    EXPECT_FALSE(edit.Begin());
    DialogStyling dialog(c.get());dialog.Show();wxTextCtrl* name=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child);text && (text->GetWindowStyleFlag()&wxTE_PROCESS_ENTER))name=text;
    ASSERT_NE(nullptr,name);name->SetValue("Default");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());dialog.Hide();EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(ordinary_edit, styling_dialog_notification_exception_releases_interaction_without_partial_state) {
    DialogStyling dialog(c.get());agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([]{throw std::runtime_error("observer");});
    EXPECT_THROW(dialog.Commit(false),std::runtime_error);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ("Default",LiveRead(*c).Events.front().Style.get());
}

TEST_F(ordinary_edit, video_layout_writeback_rejects_bad_structure_and_keeps_live_nodes_and_other_state) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Properties.video_zoom=2;LiveWriteForTest(*c).AddExtradata("unknown","keep");}
    SubtitleDialogEdit edit(c.get());edit.ResetAll();AssFile calculated(c->subtitleAccess->Read());calculated.Events.front().Id=LiveRead(*c).Events.front().Id;
    calculated.Events.front().Text="layout";calculated.Styles.front().name="renamed";
    EXPECT_THROW(edit.CommitLayout(calculated,"invalid layout",AssFile::COMMIT_DIAG_FULL),std::invalid_argument);EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    calculated.Styles.front().name="Default";calculated.Styles.front().fontsize=24;calculated.Styles.front().UpdateData();calculated.SetScriptInfo("PlayResX","123");
    edit.ResetAll();auto row=&LiveRead(*c).Events.front();auto style=&LiveRead(*c).Styles.front();
    ASSERT_TRUE(edit.CommitLayout(calculated,"layout",AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_STYLES|AssFile::COMMIT_SCRIPTINFO));
    EXPECT_EQ(row,&LiveRead(*c).Events.front());EXPECT_EQ(style,&LiveRead(*c).Styles.front());EXPECT_EQ(row,c->selectionController->ReadActiveLine());EXPECT_EQ("layout",row->Text.get());EXPECT_DOUBLE_EQ(24,style->fontsize);EXPECT_DOUBLE_EQ(2,LiveRead(*c).Properties.video_zoom);EXPECT_EQ("keep",LiveRead(*c).Extradata.front().value);
}
TEST_F(ordinary_edit, video_layout_access_requires_a_current_interaction) {
    auto id=LiveRead(*c).Events.front().Id;AssFile calculated(c->subtitleAccess->Read());calculated.Events.front().Id=id;calculated.SetScriptInfo("Title","blocked");
    auto interaction=c->subtitleAccess->TryBeginInteraction({id});ASSERT_TRUE(interaction);interaction->End();
    EXPECT_THROW(interaction->UpdateLayout(calculated),std::logic_error);EXPECT_NE("blocked",LiveRead(*c).GetScriptInfo("Title"));
}

}

TEST_F(ordinary_edit, controlled_tag_commands_preserve_multiline_data_and_single_undo) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    std::vector<int> ids;for(auto row:c->selectionController->ReadSelectedLines())ids.push_back(row->Id);
    ASSERT_EQ(2u,ids.size());
    cmd::call("edit/style/bold",c.get());
    for(auto const& row:LiveRead(*c).Events)if(std::find(ids.begin(),ids.end(),row.Id)!=ids.end())EXPECT_NE(std::string::npos,row.Text.get().find("\\b1"));
    c->subsController->Undo();
    for(auto const& row:LiveRead(*c).Events)EXPECT_EQ(std::string::npos,row.Text.get().find("\\b1"));
    c->subsController->Redo();
    for(auto const& row:LiveRead(*c).Events)if(std::find(ids.begin(),ids.end(),row.Id)!=ids.end())EXPECT_NE(std::string::npos,row.Text.get().find("\\b1"));
}
TEST_F(ordinary_edit, controlled_adjoin_and_replace_all_use_production_entries_and_preserve_ids) {
    cmd::PasteSubtitleLines(c.get(),"Dialogue: 0,0:00:00.000,0:00:01.000,Default,A,0,0,0,,one\nDialogue: 0,0:00:02.000,0:00:03.000,Default,B,0,0,0,,one");
    auto selected=c->selectionController->ReadSortedSelection();ASSERT_EQ(2u,selected.size());
    auto second=selected[1]->Id;
    cmd::call("time/continuous/start",c.get());
    EXPECT_EQ(1000,LiveRead(*c).Events.front().End.GetMilliseconds());
    for(auto const& row:LiveRead(*c).Events)if(row.Id==second)EXPECT_EQ(1000,row.Start.GetMilliseconds());
    c->subsController->Undo();
    for(auto const& row:LiveRead(*c).Events)if(row.Id==second)EXPECT_EQ(2000,row.Start.GetMilliseconds());
    SearchReplaceSettings settings{};settings.find="one";settings.replace_with="translated";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::SELECTED;
    c->search->Configure(settings);AcceptMessages messages;EXPECT_TRUE(c->search->ReplaceAll());
    for(auto row:c->selectionController->ReadSelectedLines())EXPECT_EQ("translated",row->Text.get());
    c->subsController->Undo();for(auto row:c->selectionController->ReadSelectedLines())EXPECT_EQ("one",row->Text.get());
}

TEST_F(ordinary_edit, controlled_duplicate_move_join_and_split_preserve_live_identity_and_history) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    auto selected=c->selectionController->ReadSortedSelection();ASSERT_EQ(2u,selected.size());
    auto original=selected.front();auto id=original->Id;
    cmd::call("edit/line/duplicate",c.get());EXPECT_EQ(5u,LiveRead(*c).Events.size());
    bool preserved=false;for(auto const& row:LiveRead(*c).Events)if(row.Id==id)preserved=&row==original;EXPECT_TRUE(preserved);
    cmd::call("grid/move/up",c.get());EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
    cmd::call("edit/line/join/concatenate",c.get());EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->subsController->Undo();EXPECT_EQ(5u,LiveRead(*c).Events.size());
    c->subsController->Redo();EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->textSelectionController->SetSelection(1,1);
    cmd::call("edit/line/split/preserve",c.get());EXPECT_EQ(5u,LiveRead(*c).Events.size());
    c->subsController->Undo();EXPECT_EQ(4u,LiveRead(*c).Events.size());
}

TEST_F(ordinary_edit, controlled_real_style_rename_updates_references_and_repeated_apply_keeps_node_identity) {
    class ConfirmRename : public wxModalDialogHook {
    public: ConfirmRename(){Register();}
        int Enter(wxDialog* dialog) override {return dynamic_cast<wxMessageDialog*>(dialog)?wxID_YES:wxID_NONE;}
    } confirm;
    Input("{\\rDefault}text");
    auto live=LiveRead(*c).GetStyle("Default");wxArrayString fonts;fonts.Add("Arial");
    DialogStyleEditor dialog(window,live,c.get(),nullptr,"",fonts);
    wxTextCtrl* name=nullptr;
    for(auto child:descendants(&dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child);text && text->GetValue()=="Default"){name=text;break;}
    ASSERT_NE(nullptr,name);name->ChangeValue("Renamed");button(&dialog,wxID_APPLY);
    EXPECT_EQ(live,LiveRead(*c).GetStyle("Renamed"));EXPECT_EQ("Renamed",LiveRead(*c).Events.front().Style.get());EXPECT_EQ("{\\rRenamed}text",LiveRead(*c).Events.front().Text.get());
    button(&dialog,wxID_APPLY);EXPECT_EQ(live,LiveRead(*c).GetStyle("Renamed"));
    c->subsController->Undo();c->subsController->Undo();EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Default"));EXPECT_EQ("{\\rDefault}text",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("{\\rRenamed}text",LiveRead(*c).Events.front().Text.get());
}

TEST_F(ordinary_edit, controlled_shift_and_timing_processor_use_real_dialog_confirm_events) {
    OPT_SET("Tool/Shift Times/ByTime")->SetBool(true);OPT_SET("Tool/Shift Times/Direction")->SetBool(false);
    OPT_SET("Tool/Shift Times/Type")->SetInt(0);OPT_SET("Tool/Shift Times/Affect")->SetInt(0);
    ShowShiftTimesDialog(c.get());
    wxDialog* shift=nullptr;
    for(auto child:window->GetChildren())if(auto dialog=dynamic_cast<wxDialog*>(child);dialog && dialog->GetTitle()=="Shift Times")shift=dialog;
    ASSERT_NE(nullptr,shift);
    TimeEdit* value=nullptr;for(auto child:descendants(shift))if(auto time=dynamic_cast<TimeEdit*>(child))value=time;
    ASSERT_NE(nullptr,value);value->SetTime(1000);button(shift,wxID_OK);
    EXPECT_EQ(1000,LiveRead(*c).Events.front().Start.GetMilliseconds());
    OPT_SET("Tool/Timing Post Processor/Lead/IN")->SetInt(100);
    OPT_SET("Tool/Timing Post Processor/Enable/Lead/IN")->SetBool(true);
    OPT_SET("Tool/Timing Post Processor/Enable/Lead/OUT")->SetBool(false);
    OPT_SET("Tool/Timing Post Processor/Enable/Adjacent")->SetBool(false);
    OPT_SET("Tool/Timing Post Processor/Enable/Keyframe")->SetBool(false);
    {
        RunModal hook("Timing Post-Processor",[&](wxDialog* dialog){
            for(auto child:descendants(dialog))if(auto list=dynamic_cast<wxCheckListBox*>(child))for(unsigned i=0;i<list->GetCount();++i)list->Check(i,true);
            button(dialog,wxID_OK);
        });
        ShowTimingProcessorDialog(c.get());EXPECT_EQ(900,LiveRead(*c).Events.front().Start.GetMilliseconds());
    }
    c->subsController->Undo();EXPECT_EQ(1000,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(0,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(ordinary_edit, controlled_resample_uses_real_settings_confirmation_and_one_history_step) {
    Input("{\\pos(10,20)}sample");
    int width,height;LiveRead(*c).GetResolution(width,height);auto fontsize=LiveRead(*c).GetStyle("Default")->fontsize;
    RunModal hook("Resample Resolution",[&](wxDialog* dialog){
        int changed=0;
        for(auto child:descendants(dialog))if(auto spin=dynamic_cast<wxSpinCtrl*>(child);spin && spin->GetParent()->GetLabel()=="Destination Resolution") {spin->SetValue(spin->GetValue()*2);++changed;}
        ASSERT_EQ(2,changed);button(dialog,wxID_OK);
    });
    cmd::call("tool/resampleres",c.get());
    EXPECT_EQ(width*2,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));EXPECT_EQ(height*2,LiveRead(*c).GetScriptInfoAsInt("PlayResY"));
    EXPECT_DOUBLE_EQ(fontsize*2,LiveRead(*c).GetStyle("Default")->fontsize);
    c->subsController->Undo();int restored_width,restored_height;LiveRead(*c).GetResolution(restored_width,restored_height);
    EXPECT_EQ(width,restored_width);EXPECT_EQ(height,restored_height);EXPECT_DOUBLE_EQ(fontsize,LiveRead(*c).GetStyle("Default")->fontsize);
    EXPECT_EQ("{\\pos(10,20)}sample",LiveRead(*c).Events.front().Text.get());
}

TEST_F(ordinary_edit, selection_notifications_refresh_real_editor_without_model_authority) {
    int first=c->selectionController->ActiveID(), second=0;
    {
        auto write=c->subtitleAccess->TryWrite();
        auto row=new AssDialogue;row->Text="second selection";second=row->Id;
        LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("selection fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    auto undo=c->subsController->GetUndoDescription();auto revision=c->subtitleEditGate.Revision();
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    std::vector<char> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&]{order.push_back('S');});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&] {
        order.push_back('A');EXPECT_FALSE(c->subtitleAccess->TryWrite());
        c->subsController->Undo(); // History is also read-only during delivery.
    });
    c->selectionController->SetSelectionAndActiveIDs({second},second,false);
    EXPECT_EQ("second selection",std::string(editor->GetTextRaw().data()));
    c->selectionController->SetSelectionAndActiveIDs({second},second,false);
    EXPECT_EQ((std::vector<char>{'S','A','S'}),order);
    EXPECT_EQ(second,c->selectionController->ActiveID());
    EXPECT_EQ(undo,c->subsController->GetUndoDescription());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    c->selectionController->SetActiveID(first);
    EXPECT_EQ("",std::string(editor->GetTextRaw().data()));
    EXPECT_EQ((std::set<int>{second}),c->selectionController->SelectedIDs());
}

namespace {
class selection_ui : public ordinary_edit {
protected:
    BaseGrid* grid=nullptr;
    int height=0;
    std::vector<int> ids;
    void SetUp() override {
        ordinary_edit::SetUp();
        grid=new BaseGrid(window,c.get());c->subsGrid=grid;
        grid->SetSize(900,300);window->SetSize(1000,600);window->Show();
        {
            auto write=c->subtitleAccess->TryWrite();
            auto& first=LiveWriteForTest(*c).Events.front();first.Text="first";first.Start=1000;
            ids.push_back(first.Id);
            for(int i=1;i<4;++i) {
                auto row=new AssDialogue;row->Text=std::to_string(i);row->Start=(i+1)*1000;
                LiveWriteForTest(*c).Events.push_back(*row);ids.push_back(row->Id);
            }
            LiveWriteForTest(*c).Commit("selection UI fixture",AssFile::COMMIT_DIAG_ADDREM|AssFile::COMMIT_DIAG_FULL);
        }
        c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0],false);
        height=grid->LineHeight();
    }
    void TearDown() override {
        if(grid->HasCapture())grid->ReleaseMouse();
        c->subsGrid=nullptr;ordinary_edit::TearDown();
    }
    void Mouse(int row,bool ctrl=false,bool shift=false,bool alt=false) {
        wxMouseEvent down(wxEVT_LEFT_DOWN);down.SetPosition({300,(row+1)*height+2});
        down.SetControlDown(ctrl);down.SetShiftDown(shift);down.SetAltDown(alt);
        down.SetEventObject(grid);grid->GetEventHandler()->ProcessEvent(down);
        wxMouseEvent up(wxEVT_LEFT_UP);up.SetPosition(down.GetPosition());up.SetEventObject(grid);
        grid->GetEventHandler()->ProcessEvent(up);
    }
    void Key(int key,bool shift=false,bool alt=false) {
        wxKeyEvent event(wxEVT_KEY_DOWN);event.m_keyCode=key;event.SetShiftDown(shift);event.SetAltDown(alt);
        event.SetEventObject(grid);grid->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(selection_ui, mouse_keyboard_and_select_all_are_pure_ui_during_comparison) {
    auto before=c->subtitleEditGate.Revision();auto undo=c->subsController->GetUndoDescription();
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    Mouse(1);EXPECT_EQ(ids[1],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[1]}),c->selectionController->SelectedIDs());
    EXPECT_EQ("1",std::string(editor->GetTextRaw().data()));
    Mouse(3,true);EXPECT_EQ((std::set<int>{ids[1],ids[3]}),c->selectionController->SelectedIDs());
    Key(WXK_UP,false,true);EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[1],ids[3]}),c->selectionController->SelectedIDs());
    Key(WXK_UP);EXPECT_EQ((std::set<int>{ids[1]}),c->selectionController->SelectedIDs());
    cmd::call("subtitle/select/all",c.get());EXPECT_EQ(4u,c->selectionController->SelectedIDs().size());
    EXPECT_EQ(before,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
}
TEST_F(selection_ui, bound_row_modifiers_keep_single_member_and_repeat_notifications) {
    {
        auto write=c->subtitleAccess->TryWrite();
        auto group=LiveWriteForTest(*c).AddExtradata("aegisub.enhanced.subtitle-binding","0:test");
        LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{group};
        std::next(LiveWriteForTest(*c).Events.begin())->ExtradataIds=std::vector<uint32_t>{group};
        LiveWriteForTest(*c).Commit("binding fixture",AssFile::COMMIT_EXTRADATA);
    }
    auto revision=c->subtitleEditGate.Revision();auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    Mouse(0);EXPECT_EQ((std::set<int>{ids[0],ids[1]}),c->selectionController->SelectedIDs());
    Mouse(1,true);EXPECT_EQ((std::set<int>{ids[1]}),c->selectionController->SelectedIDs());
    Mouse(0,false,false,true);EXPECT_EQ((std::set<int>{ids[0]}),c->selectionController->SelectedIDs());
    int notices=0;agi::signal::Connection selected=c->selectionController->AddSelectionListener([&]{++notices;});
    Mouse(0);Mouse(0);EXPECT_EQ(2,notices);
    EXPECT_EQ((std::set<int>{ids[0],ids[1]}),c->selectionController->SelectedIDs());
    EXPECT_EQ(revision,c->subtitleEditGate.Revision());
}
TEST_F(selection_ui, range_anchor_survives_sort_by_identity_and_missing_anchor_is_safe) {
    Mouse(1);Key(WXK_DOWN,true);
    EXPECT_EQ((std::set<int>{ids[1],ids[2]}),c->selectionController->SelectedIDs());
    {
        auto write=c->subtitleAccess->TryWrite();
        SubtitleRowListEdit staged(c.get());
        staged.file.Events.reverse();staged.Commit("reverse fixture",AssFile::COMMIT_ORDER);
    }
    // Current row 2 moved to position 1; anchor row 1 moved to position 2.
    Key(WXK_UP,true);
    EXPECT_EQ(ids[3],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[3],ids[2],ids[1]}),c->selectionController->SelectedIDs());
    {
        auto write=c->subtitleAccess->TryWrite();SubtitleRowListEdit staged(c.get());
        for(auto it=staged.file.Events.begin();it!=staged.file.Events.end();++it)if(it->Id==ids[1]) {
            staged.file.Events.erase_and_dispose(it,[](auto row){delete row;});break;
        }
        staged.Commit("delete anchor fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    Key(WXK_DOWN,true);
    EXPECT_EQ((std::set<int>{ids[3],ids[2]}),c->selectionController->SelectedIDs());
}
TEST_F(selection_ui, undo_redo_rebind_editor_grid_and_bound_selection_with_same_ids) {
    c->selectionController->SetSelectionAndActiveIDs({ids[1],ids[2]},ids[2],false);
    c->textSelectionController->SetSelection(0,1);
    Input("both edited");
    auto selected=c->selectionController->SelectedIDs();
    std::vector<char> order;
    agi::signal::Connection selection=c->selectionController->AddSelectionListener([&]{order.push_back('S');});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&]{order.push_back('A');});
    c->subsController->Undo();
    EXPECT_EQ((std::vector<char>{'S','A'}),order);EXPECT_EQ(selected,c->selectionController->SelectedIDs());
    EXPECT_EQ(ids[2],c->selectionController->ActiveID());EXPECT_EQ("2",std::string(editor->GetTextRaw().data()));
    order.clear();c->subsController->Redo();
    EXPECT_EQ((std::vector<char>{'S','A'}),order);EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ("both edited",std::string(editor->GetTextRaw().data()));
    EXPECT_EQ(selected,c->selectionController->SelectedIDs());
    Key(WXK_UP);EXPECT_EQ(ids[1],c->selectionController->ActiveID());
}
TEST_F(selection_ui, selection_listener_cannot_undo_or_commit_with_a_preexisting_scope) {
    Input("history entry");auto before=c->subsController->GetUndoDescription();
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto edit=c->subtitleAccess->Edit(scope->Lease());
    auto revision=c->subtitleEditGate.Revision();int calls=0;
    agi::signal::Connection observer=c->selectionController->AddSelectionListener([&]{
        ++calls;c->subsController->Undo();c->subsController->Redo();
        EXPECT_FALSE(c->subtitleAccess->TryWrite());
        EXPECT_THROW(edit.Commit("reentry",AssFile::COMMIT_DIAG_META),std::logic_error);
    });
    c->selectionController->SetSelectedIDs({ids[0]});
    EXPECT_EQ(1,calls);EXPECT_EQ(before,c->subsController->GetUndoDescription());
    EXPECT_EQ("history entry",LiveRead(*c).Events.front().Text.get());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
}
TEST_F(selection_ui, find_navigation_uses_ids_without_a_write_lease) {
    SearchReplaceSettings settings{};settings.find="2";settings.field=SearchReplaceSettings::Field::TEXT;
    settings.limit_to=SearchReplaceSettings::Limit::ALL;c->search->Configure(settings);
    auto before=c->subtitleEditGate.Revision();auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    EXPECT_TRUE(c->search->FindNext());EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[2]}),c->selectionController->SelectedIDs());
    EXPECT_EQ(before,c->subtitleEditGate.Revision());
}
}

TEST_F(ordinary_edit, save_cleans_only_independent_copy_and_keeps_ass_format_precision) {
    {
        auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
        row.Text=" 中 文 {\\i1} ";row.Comment=true;row.Start=1001;row.End=2345;
        auto binding=LiveWriteForTest(*c).AddExtradata("aegisub.enhanced.subtitle-binding","0:save-test");
        LiveWriteForTest(*c).AddExtradata("orphan","keep in memory");
        row.ExtradataIds=std::vector<uint32_t>{binding,999999};
        LiveWriteForTest(*c).Properties.video_zoom=1.25;LiveWriteForTest(*c).Properties.automation_settings["test"]="saved";
        LiveWriteForTest(*c).Commit("save fixture",AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_EXTRADATA);
    }
    auto id=LiveRead(*c).Events.front().Id;auto refs=LiveRead(*c).Events.front().ExtradataIds.get();
    auto revision=c->subtitleEditGate.Revision();auto undo=c->subsController->GetUndoDescription();
    auto path=agi::fs::path((std::filesystem::current_path()/"snapshot-save.ass").wstring());
    {
        // C1-C1c-12: saving during a comparison is refused before any output.
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_THROW(c->subsController->Save(path,"utf-8"),SubtitleProjectBusy);
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path.wstring())));
    }
    c->subsController->Save(path,"utf-8");
    // Saving works on a snapshot: no row revision, history or live extradata change.
    EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    EXPECT_EQ(refs,LiveRead(*c).Events.front().ExtradataIds.get());EXPECT_EQ(2u,LiveRead(*c).Extradata.size());
    EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    AssFile reopened;SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    std::ifstream saved(path.string(),std::ios::binary);
    std::string output((std::istreambuf_iterator<char>(saved)),{});
    EXPECT_NE(std::string::npos,output.find(" 中 文 {\\i1} "));
    // The existing ASS reader trims line-end space; ASS time serialization rounds to centiseconds.
    EXPECT_EQ(" 中 文 {\\i1}",reopened.Events.front().Text.get());EXPECT_TRUE(reopened.Events.front().Comment);
    EXPECT_EQ(1000,reopened.Events.front().Start.GetMilliseconds());EXPECT_EQ(2350,reopened.Events.front().End.GetMilliseconds());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());EXPECT_EQ(2345,LiveRead(*c).Events.front().End.GetMilliseconds());
    ASSERT_EQ(1u,reopened.Extradata.size());EXPECT_EQ("aegisub.enhanced.subtitle-binding",reopened.Extradata.front().key);
    EXPECT_EQ(1u,reopened.Events.front().ExtradataIds.get().size());EXPECT_NE(nullptr,reopened.GetStyle("Default"));
    EXPECT_DOUBLE_EQ(1.25,reopened.Properties.video_zoom);
}
TEST_F(ordinary_edit, failed_save_leaves_filename_paths_dirty_state_model_and_history_intact) {
    Input("unsaved");auto old=c->subsController->Filename();auto revision=c->subtitleEditGate.Revision();
    auto undo=c->subsController->GetUndoDescription();auto script=c->path->Decode("?script");
    auto path=agi::fs::path((std::filesystem::current_path()/"missing-parent"/"failed.ass").wstring());
    EXPECT_ANY_THROW(c->subsController->Save(path,"utf-8"));
    EXPECT_EQ(old,c->subsController->Filename());EXPECT_EQ(script,c->path->Decode("?script"));EXPECT_TRUE(c->subsController->IsModified());
    EXPECT_EQ("unsaved",LiveRead(*c).Events.front().Text.get());EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
}

TEST_F(ordinary_edit, autosave_owns_complete_input_and_queued_completion_survives_controller_destruction) {
    auto directory=std::filesystem::current_path()/"autosave-snapshot";
    auto old_path=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString(directory.string());
    Input("automatic snapshot 中 文");
    {
        auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Properties.video_zoom=1.75;
        LiveWriteForTest(*c).Properties.export_encoding="utf-8";
        auto id=LiveWriteForTest(*c).AddExtradata("aegisub.enhanced.subtitle-binding","0:auto");
        LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{id};LiveWriteForTest(*c).AddExtradata("orphan","not saved");
        LiveWriteForTest(*c).Commit("auto fixture",AssFile::COMMIT_EXTRADATA);
    }
    c->subsController->RequestAutoSave();Input("later edit");
    // The production destructor drains its serial worker and invalidates queued UI results.
    c->subsController.reset();wxTheApp->ProcessPendingEvents();
    std::vector<std::filesystem::path> outputs;
    for(auto const& item:std::filesystem::directory_iterator(directory))outputs.push_back(item.path());
    ASSERT_EQ(1u,outputs.size());AssFile reopened;agi::fs::path path(outputs.front().wstring());
    SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("automatic snapshot 中 文",reopened.Events.front().Text.get());EXPECT_EQ("later edit",LiveRead(*c).Events.front().Text.get());
    EXPECT_DOUBLE_EQ(1.75,reopened.Properties.video_zoom);EXPECT_EQ("utf-8",reopened.Properties.export_encoding);
    EXPECT_EQ(1u,reopened.Extradata.size());EXPECT_EQ(2u,LiveRead(*c).Extradata.size());
    OPT_SET("Path/Auto/Save")->SetString(old_path);
}

TEST_F(ordinary_edit, metadata_only_save_close_reopen_has_independent_dirty_state_and_compatible_ass_fields) {
    using Field=SubtitleProjectAccess::MetadataField;
    auto path=agi::fs::path((std::filesystem::current_path()/"metadata-only.ass").wstring());
    c->subsController->Save(path,"utf-8");ASSERT_FALSE(c->subsController->IsModified());
    auto revision=c->subtitleEditGate.Revision();auto undo=c->subsController->GetUndoDescription();
    auto ui=c->subtitleAccess->UIState();ui.video_zoom=1.5;c->subtitleAccess->UpdateUIState(ui);
    EXPECT_FALSE(c->subsController->IsModified());
    c->subtitleAccess->SetMetadata(Field::ExportEncoding,"utf-8");c->subtitleAccess->SetAutomationSetting("test","some settings");
    EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    auto bad=agi::fs::path((std::filesystem::current_path()/"no-parent-metadata"/"failed.ass").wstring());
    EXPECT_ANY_THROW(c->subsController->Save(bad,"utf-8"));EXPECT_TRUE(c->subtitleAccess->MetadataDirty());
    c->subsController->Save(path,"utf-8");EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Close();c->subsController->Load(path,"utf-8");
    auto id=LiveRead(*c).Events.front().Id;c->selectionController->SetSelectionAndActiveIDs({id},id);
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_EQ("utf-8",LiveRead(*c).Properties.export_encoding);
    EXPECT_EQ("some settings",c->subtitleAccess->AutomationSetting("test"));EXPECT_DOUBLE_EQ(1.5,LiveRead(*c).Properties.video_zoom);
}
TEST_F(ordinary_edit, metadata_only_change_triggers_autosave_after_a_clean_manual_save) {
    auto root=std::filesystem::current_path()/"metadata-autosave";
    auto old=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString(root.string());
    c->subsController->Save(agi::fs::path((std::filesystem::current_path()/"clean-metadata.ass").wstring()),"utf-8");
    auto revision=c->subtitleEditGate.Revision();
    c->subtitleAccess->SetAutomationSetting("test","metadata only");c->subsController->RequestAutoSave();
    EXPECT_EQ(revision,c->subtitleEditGate.Revision());c->subsController.reset();wxTheApp->ProcessPendingEvents();
    std::vector<std::filesystem::path> files;for(auto const& item:std::filesystem::directory_iterator(root))files.push_back(item.path());
    ASSERT_EQ(1u,files.size());AssFile reopened;agi::fs::path path(files.front().wstring());
    SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("metadata only",reopened.Properties.automation_settings.at("test"));
    OPT_SET("Path/Auto/Save")->SetString(old);
}

template<class T> concept PublicHistoryRestore = requires { typename T::HistoryRestore; };
static_assert(!PublicHistoryRestore<SubsController>);
TEST_F(ordinary_edit, ordinary_scope_cannot_register_a_live_history_writer_or_change_undo_granularity) {
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);
    EXPECT_THROW(LiveWriteForTest(*c).AddUndoManager([](AssFileCommit const&){}),std::logic_error);
    scope.reset();Input("first history edit");Input("same undo group");
    auto description=c->subsController->GetUndoDescription();
    // Input emits STARTACTION, so each call retains its own existing Undo group.
    c->subsController->Undo();EXPECT_EQ("first history edit",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo();EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("first history edit",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("same undo group",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
}

TEST_F(ordinary_edit, font_collection_uses_snapshot_and_can_be_destroyed_with_worker_pending) {
    auto action=OPT_GET("Tool/Fonts Collector/Action")->GetInt();OPT_SET("Tool/Fonts Collector/Action")->SetInt(0);
    Input("font snapshot");auto revision=c->subtitleEditGate.Revision();
    ShowFontsCollectorDialog(c.get());wxDialog* dialog=nullptr;wxButton* start=nullptr;
    for(auto child:descendants(window))if(auto candidate=dynamic_cast<wxDialog*>(child);candidate && candidate->GetTitle()=="Fonts Collector")dialog=candidate;
    ASSERT_NE(nullptr,dialog);
    for(auto child:descendants(dialog))if(auto candidate=dynamic_cast<wxButton*>(child);candidate && candidate->GetLabel()=="&Start!")start=candidate;
    ASSERT_NE(nullptr,start);
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    button(start,start->GetId());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    comparison.reset();Input("edited while worker owns snapshot");
    button(dialog,wxID_CANCEL); // unregisters; parent destruction drains the private worker queue
    EXPECT_EQ("edited while worker owns snapshot",LiveRead(*c).Events.front().Text.get());
    OPT_SET("Tool/Fonts Collector/Action")->SetInt(action);
}

namespace {
class model_installation : public ordinary_edit {
protected:
    using State=SubtitleModelInstallation::State;
    using Ticket=SubtitleModelRebindTicket;
    agi::fs::path Candidate(std::string const& name,bool bad_extra=false) {
        AssFile source;source.LoadDefault();source.Events.front().Text="new 模型";source.Events.front().Comment=true;
        source.Properties.export_encoding="utf-8";source.Properties.video_zoom=1.5;
        auto id=source.AddExtradata("aegisub.enhanced.subtitle-binding","0:installed");
        source.Events.front().ExtradataIds=std::vector<uint32_t>{bad_extra?999999:id};
        auto path=agi::fs::path((std::filesystem::current_path()/(name+".ass")).wstring());
        SubtitleFormat::GetWriter(path)->WriteFile(&source,path,0,"utf-8");return path;
    }
    std::vector<std::string> Contents() {
        auto const& file=c->subtitleAccess->Read();std::vector<std::string> values;
        for(auto const& info:file.Info)values.push_back(std::string(info.Key())+"="+std::string(info.Value()));
        for(auto const& style:file.Styles)values.push_back(style.GetEntryData());
        for(auto const& row:file.Events)values.push_back(std::to_string(row.Id)+":"+std::to_string(row.Row)+":"+
            std::to_string(row.Start.GetMilliseconds())+":"+std::to_string(row.End.GetMilliseconds())+":"+row.GetEntryData());
        for(auto const& extra:file.Extradata)values.push_back(std::to_string(extra.id)+":"+extra.key+":"+extra.value);
        for(auto const& attachment:file.Attachments)values.push_back(attachment.GetEntryData());
        values.push_back(std::to_string(file.next_extradata_id));return values;
    }
    struct Envelope {
        std::vector<std::string> content;ProjectProperties properties;
        agi::SubtitleRevision revision;uint64_t metadata;
        std::set<int> selected;int active;
        wxString undo,redo;agi::fs::path filename,script;
        bool modified,metadata_dirty,undo_empty,redo_empty;
    };
    Envelope Capture() {
        return {Contents(),LiveRead(*c).Properties,c->subtitleEditGate.Revision(),c->subtitleAccess->MetadataRevision(),
            c->selectionController->SelectedIDs(),c->selectionController->ActiveID(),c->subsController->GetUndoDescription(),
            c->subsController->GetRedoDescription(),c->subsController->Filename(),c->path->Decode("?script"),
            c->subsController->IsModified(),c->subtitleAccess->MetadataDirty(),c->subsController->IsUndoStackEmpty(),c->subsController->IsRedoStackEmpty()};
    }
    void Unchanged(Envelope const& before) {
        EXPECT_EQ(before.content,Contents());EXPECT_EQ(static_cast<ProjectMetadata const&>(before.properties),static_cast<ProjectMetadata const&>(LiveRead(*c).Properties));
        auto const& ui=LiveRead(*c).Properties;EXPECT_EQ(before.properties.active_row,ui.active_row);EXPECT_EQ(before.properties.scroll_position,ui.scroll_position);
        EXPECT_EQ(before.properties.video_position,ui.video_position);EXPECT_EQ(before.properties.video_zoom,ui.video_zoom);
        EXPECT_EQ(before.properties.ar_mode,ui.ar_mode);EXPECT_EQ(before.properties.ar_value,ui.ar_value);
        EXPECT_EQ(before.revision,c->subtitleEditGate.Revision());EXPECT_EQ(before.metadata,c->subtitleAccess->MetadataRevision());
        EXPECT_EQ(before.selected,c->selectionController->SelectedIDs());EXPECT_EQ(before.active,c->selectionController->ActiveID());
        EXPECT_EQ(before.undo,c->subsController->GetUndoDescription());EXPECT_EQ(before.redo,c->subsController->GetRedoDescription());
        EXPECT_EQ(before.filename,c->subsController->Filename());EXPECT_EQ(before.script,c->path->Decode("?script"));
        EXPECT_EQ(before.modified,c->subsController->IsModified());EXPECT_EQ(before.metadata_dirty,c->subtitleAccess->MetadataDirty());
        EXPECT_EQ(before.undo_empty,c->subsController->IsUndoStackEmpty());EXPECT_EQ(before.redo_empty,c->subsController->IsRedoStackEmpty());
    }
};
TEST_F(model_installation, production_load_denied_by_comparison_preserves_complete_host_state) {
    Input("one");Input("two");c->subsController->Undo();
    c->subtitleAccess->SetAutomationSetting("keep","metadata");
    auto path=Candidate("denied-load");auto before=Capture();auto old=c->selectionController->ReadActiveLine();int notices=0;
    agi::signal::Connection listener=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++notices;if(t.Current())t.Acknowledge(t.Revision());});
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);Unchanged(before);
    EXPECT_EQ(old,c->selectionController->ReadActiveLine());EXPECT_EQ(0,notices);EXPECT_EQ("one",std::string(editor->GetTextRaw().data()));
    comparison.reset();c->subsController->Redo();EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
}
TEST_F(model_installation, production_observer_failure_is_recovery_blocked_and_retries_explicitly) {
    Input("retained old row");auto old=c->selectionController->ReadActiveLine();auto path=Candidate("observer-failure");
    bool fail=true;std::vector<int> order;
    agi::signal::Connection reader=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){order.push_back(1);EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());t.Acknowledge(t.Revision());});
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){order.push_back(2);if(fail)throw std::runtime_error("observer failure");old=nullptr;t.Acknowledge(t.Revision());});
    agi::signal::Connection normal=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){order.push_back(3);t.Acknowledge(t.Revision());});
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::runtime_error);
    EXPECT_EQ((std::vector<int>{3,2,1}),order);EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    ASSERT_NE(nullptr,old);EXPECT_EQ("retained old row",old->Text.get());EXPECT_EQ(1u,c->subsController->PendingInstallationObservers().size());
    EXPECT_FALSE(c->subtitleAccess->TryWrite());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());EXPECT_FALSE(c->subsController->CanSave());
    EXPECT_THROW(c->subsController->Save(path,"utf-8"),std::logic_error);
    EXPECT_THROW(c->subsController->RequestAutoSave(),std::logic_error);
    EXPECT_THROW(c->subsController->Close(),std::logic_error);
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);
    EXPECT_FALSE(c->subsController->RetryModelInstallation());EXPECT_EQ("retained old row",old->Text.get());
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());EXPECT_EQ(nullptr,old);
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    EXPECT_TRUE(c->subsController->PendingInstallationObservers().empty());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    reader.Disconnect();throwing.Disconnect();normal.Disconnect();
    Input("new edit");c->subsController->Undo();EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("new edit",LiveRead(*c).Events.front().Text.get());
}
TEST_F(model_installation, successful_install_invalidates_old_document_and_saves_reopened_data) {
    Input("old");auto old=c->subtitleEditGate.Capture();auto path=Candidate("successful-install");
    c->subsController->Load(path,"utf-8");auto next=c->subtitleEditGate.Revision();
    EXPECT_EQ(old.Revision().document,next.document);EXPECT_EQ(old.Revision().document_epoch+1,next.document_epoch);
    EXPECT_EQ(old.Revision().generation+1,next.generation);EXPECT_FALSE(old.Current());
    EXPECT_THROW(c->subtitleAccess->Read(old),std::logic_error);
    EXPECT_THROW(c->selectionController->ReadActiveLine(old),std::logic_error);
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
    EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());EXPECT_EQ("new 模型",std::string(editor->GetTextRaw().data()));
    c->subsController->Save(path,"utf-8");AssFile reopened;SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("new 模型",reopened.Events.front().Text.get());EXPECT_TRUE(reopened.Events.front().Comment);
    EXPECT_EQ("aegisub.enhanced.subtitle-binding",reopened.Extradata.front().key);EXPECT_NE(nullptr,reopened.GetStyle("Default"));
}
TEST_F(model_installation, active_writer_and_prepare_failure_or_cancel_preserve_original_state) {
    Input("protected");auto path=Candidate("prepare-failure");
    {
        auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto before=Capture();
        EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);Unchanged(before);
    }
    for(bool cancel:{false,true}) {
        auto before=Capture();
        agi::signal::Connection observer=LiveRead(*c).AddInstallationObserver("prepare-stop",[&]{
            EXPECT_FALSE(c->subtitleAccess->TryWrite());
            EXPECT_THROW(c->subtitleAccess->SetAutomationSetting("keep","blocked"),std::logic_error);
            auto ui=c->subtitleAccess->UIState();ui.video_zoom=9;
            EXPECT_THROW(c->subtitleAccess->UpdateUIState(ui),std::logic_error);
            if(cancel)c->subsController->CancelModelInstallation();else throw std::runtime_error("prepare failure");
        },[](int,AssDialogue const*,Ticket t){t.Acknowledge(t.Revision());});
        EXPECT_ANY_THROW(c->subsController->Load(path,"utf-8"));Unchanged(before);
    }
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(model_installation, invalid_candidate_and_missing_input_do_not_publish_or_change_revisions) {
    Input("untouched");auto before=Capture();auto bad=Candidate("invalid-extra",true);
    EXPECT_THROW(c->subsController->Load(bad,"utf-8"),std::invalid_argument);Unchanged(before);
    auto missing=agi::fs::path((std::filesystem::current_path()/"missing-install-input.ass").wstring());
    EXPECT_ANY_THROW(c->subsController->Load(missing,"utf-8"));Unchanged(before);
}
TEST_F(model_installation, observer_reentry_and_real_host_close_event_cannot_destroy_publication_stack) {
    auto path=Candidate("reentrant-load");bool closed=false,visited=false;
    window->Bind(wxEVT_CLOSE_WINDOW,[&](wxCloseEvent& event){if(c->subsController->DeferHostClose(event)){closed=true;return;}event.Skip();});
    agi::signal::Connection observer=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){
        visited=true;EXPECT_FALSE(window->Close());EXPECT_TRUE(closed);
        EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);
        EXPECT_THROW(c->subsController->RetryModelInstallation(),std::logic_error);
        EXPECT_THROW(c->selectionController->SetActiveID(0),std::logic_error);
        EXPECT_FALSE(c->subtitleAccess->TryWrite());t.Acknowledge(t.Revision());
    });
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(visited);EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    wxCloseEvent normal(wxEVT_CLOSE_WINDOW);EXPECT_FALSE(c->subsController->DeferHostClose(normal));
}
TEST_F(model_installation, observer_self_destruction_withdraws_only_after_reference_release) {
    auto path=Candidate("destroyed-observer");bool destroyed=false;
    struct Borrower {
        std::shared_ptr<int> lifetime=std::make_shared<int>(0); // Declared first, released after every borrowed field.
        AssDialogue const* row;bool& destroyed;agi::signal::Connection connection;
        Borrower(AssDialogue const* row,bool& destroyed):row(row),destroyed(destroyed){}
        ~Borrower(){row=nullptr;destroyed=true;}
    };
    auto borrower=std::make_unique<Borrower>(c->selectionController->ReadActiveLine(),destroyed);
    borrower->connection=LiveRead(*c).AddInstallationObserver("owned borrower",[]{},[&](int,AssDialogue const*,Ticket){borrower.reset();},std::weak_ptr<void const>(borrower->lifetime));
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(destroyed);EXPECT_FALSE(borrower);EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
}
TEST_F(model_installation, background_installation_is_rejected_before_any_host_access) {
    auto path=Candidate("background-install");auto before=Capture();
    std::thread worker([&]{EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);});worker.join();Unchanged(before);
}
TEST_F(model_installation, missing_ack_and_disconnection_cannot_release_an_owners_old_reference) {
    Input("borrowed until owner releases");auto old=c->selectionController->ReadActiveLine();auto path=Candidate("missing-ack");
    auto lifetime=std::make_shared<int>(0);int called=0;
    agi::signal::Connection observer=LiveRead(*c).AddInstallationObserver("no acknowledgement",[]{},
        [&](int,AssDialogue const*,Ticket){++called;},std::weak_ptr<void const>(lifetime));
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::runtime_error);
    EXPECT_EQ(1,called);EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    observer.Disconnect();EXPECT_FALSE(c->subsController->RetryModelInstallation());
    EXPECT_EQ("borrowed until owner releases",old->Text.get());EXPECT_EQ(1,called);
    old=nullptr;lifetime.reset();EXPECT_TRUE(c->subsController->RetryModelInstallation());
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
}
TEST_F(model_installation, owner_destroyed_during_prepare_releases_its_old_reference_before_withdrawal) {
    auto path=Candidate("prepare-destruction");bool destroyed=false,called=false;
    struct Borrower {
        std::shared_ptr<int> lifetime=std::make_shared<int>(0);
        AssDialogue const* row;bool& destroyed;agi::signal::Connection connection;
        Borrower(AssDialogue const* row,bool& destroyed):row(row),destroyed(destroyed){}
        ~Borrower(){row=nullptr;destroyed=true;}
    };
    auto borrower=std::make_unique<Borrower>(c->selectionController->ReadActiveLine(),destroyed);
    borrower->connection=LiveRead(*c).AddInstallationObserver("prepare destroys owner",[&]{borrower.reset();},
        [&](int,AssDialogue const*,Ticket){called=true;},std::weak_ptr<void const>(borrower->lifetime));
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(destroyed);EXPECT_FALSE(called);
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
}
TEST_F(model_installation, repeated_load_keeps_selection_before_active_and_rejects_old_task_writeback) {
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto task=c->subtitleAccess->Edit(scope->Lease());scope.reset();
    auto path=Candidate("repeat-install");std::vector<int> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);if(t.Current())t.Acknowledge(t.Revision());});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);if(t.Current())t.Acknowledge(t.Revision());});
    auto revision=c->subtitleEditGate.Revision();
    for(int i=1;i<=2;++i) {
        order.clear();c->subsController->Load(path,"utf-8");
        EXPECT_EQ((std::vector<int>{1,2}),order);EXPECT_EQ(revision.document_epoch+i,c->subtitleEditGate.Revision().document_epoch);
        EXPECT_THROW(task.SetDialogueField(LiveRead(*c).Events.front().Id,&AssDialogueBase::Text,std::string("old task")),std::logic_error);
        EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    }
    order.clear();auto ids=c->selectionController->SelectedIDs();c->selectionController->SetSelectedIDs(ids);
    c->selectionController->SetSelectedIDs(ids);EXPECT_EQ((std::vector<int>{1,1}),order);
}
TEST_F(model_installation, real_grid_audio_and_long_lived_dialogs_rebind_and_edit_the_installed_document) {
    Input("old dialog input");auto path=Candidate("ui-observers");
    c->subsGrid=new BaseGrid(window,c.get());c->audioBox=new AudioBox(window,c.get());
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    {
        DialogStyling styling(c.get());DialogTranslation translation(c.get());
        c->subsController->Load(path,"utf-8");
        EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
        EXPECT_TRUE(c->subsController->PendingInstallationObservers().empty());
        EXPECT_EQ("new 模型",std::string(editor->GetTextRaw().data()));
        EXPECT_NE(nullptr,c->audioController->GetTimingController());
        Input("edited new UI model");c->subsController->Undo();EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
        c->subsController->Redo();EXPECT_EQ("edited new UI model",LiveRead(*c).Events.front().Text.get());
    }
    c->project->CloseAudio();c->subsGrid=nullptr;c->audioBox=nullptr;
}

// C1-C1c-8: Load/Close lifecycle business migration.
class RecordMessages : public wxModalDialogHook {
public:
    std::vector<wxString> messages;
    RecordMessages() { Register(); }
    int Enter(wxDialog* dialog) override {
        if(auto message=dynamic_cast<wxMessageDialog*>(dialog)){messages.push_back(message->GetMessage());return wxID_OK;}
        return wxID_NONE;
    }
    void Exit(wxDialog*) override {}
};
class document_lifecycle : public model_installation {
protected:
    std::vector<agi::fs::path> Recent() { auto list=config::mru->Get("Subtitle");return list?*list:std::vector<agi::fs::path>{}; }
    static void Ack(Ticket& t) { if(t.Current())t.Acknowledge(t.Revision()); }
    agi::fs::path Local(std::string const& name) { return agi::fs::path((std::filesystem::current_path()/name).wstring()); }
};
TEST_F(document_lifecycle, production_close_installs_blank_document_with_new_identity_and_clean_history) {
    auto path=Candidate("close-source");c->subsController->Load(path,"utf-8");
    Input("edited before close");c->subtitleAccess->SetAutomationSetting("keep","metadata");
    ASSERT_TRUE(c->subsController->IsModified());
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto task=c->subtitleAccess->Edit(scope->Lease());scope.reset();
    auto old=c->subtitleEditGate.Capture();auto recent=Recent();
    int opened=0;agi::fs::path opened_path="unset";std::vector<int> order;
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const& p){++opened;opened_path=p;});
    agi::signal::Connection commit=LiveRead(*c).AddCommitListener([&](int type,AssDialogue const*,Ticket t){order.push_back(type==AssFile::COMMIT_NEW?0:-1);Ack(t);});
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);Ack(t);});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);Ack(t);});
    c->subsController->Close();
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty());
    EXPECT_EQ((std::vector<int>{0,1,2}),order); // one COMMIT_NEW, then Selection before Active, each exactly once
    auto next=c->subtitleEditGate.Revision();
    EXPECT_EQ(old.Revision().document,next.document);EXPECT_EQ(old.Revision().document_epoch+1,next.document_epoch);
    EXPECT_EQ(old.Revision().generation+1,next.generation);EXPECT_FALSE(old.Current());
    EXPECT_THROW(c->subtitleAccess->Read(old),std::logic_error);
    EXPECT_THROW(task.SetDialogueField(LiveRead(*c).Events.front().Id,&AssDialogueBase::Text,std::string("old task")),std::logic_error);
    ASSERT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Default"));
    EXPECT_TRUE(LiveRead(*c).Extradata.empty());EXPECT_EQ("",std::string(editor->GetTextRaw().data()));
    AssFile blank;blank.LoadDefault(true,OPT_GET("Subtitle Format/ASS/Default Style Catalog")->GetString());
    EXPECT_EQ(static_cast<ProjectMetadata const&>(blank.Properties),static_cast<ProjectMetadata const&>(LiveRead(*c).Properties));
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
    EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_FALSE(c->subtitleAccess->MetadataDirty());
    EXPECT_TRUE(c->subsController->IsUndoStackEmpty());EXPECT_TRUE(c->subsController->IsRedoStackEmpty());
    EXPECT_NE(path,c->subsController->Filename());EXPECT_NE(path.parent_path(),c->path->Decode("?script"));
    EXPECT_EQ(1,opened);EXPECT_TRUE(opened_path.empty());EXPECT_EQ(recent,Recent()); // closing never adds an empty MRU entry
    Input("after close");EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo();EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    c->subsController->Redo();EXPECT_EQ("after close",LiveRead(*c).Events.front().Text.get());
}
TEST_F(document_lifecycle, production_close_denied_by_comparison_or_writer_preserves_complete_host_state) {
    auto path=Candidate("close-denied");c->subsController->Load(path,"utf-8");
    Input("one");Input("two");c->subsController->Undo();c->subtitleAccess->SetAutomationSetting("keep","metadata");
    int opened=0,notices=0;auto old=c->selectionController->ReadActiveLine();
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    agi::signal::Connection commit=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++notices;Ack(t);});
    auto before=Capture();
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_THROW(c->subsController->Close(),std::logic_error);Unchanged(before);
    }
    {
        auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto held=Capture(); // acquiring a writer advances generation
        EXPECT_THROW(c->subsController->Close(),std::logic_error);Unchanged(held);
    }
    EXPECT_EQ(old,c->selectionController->ReadActiveLine());EXPECT_EQ(0,opened);EXPECT_EQ(0,notices);
    EXPECT_EQ(State::Aborting,c->subsController->InstallationStatus());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Redo();EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());EXPECT_EQ(path,c->subsController->Filename());
}
TEST_F(document_lifecycle, close_recovery_blocked_runs_follow_up_work_exactly_once_after_explicit_retry) {
    auto path=Candidate("close-recovery");c->subsController->Load(path,"utf-8");Input("kept by retired model");
    auto old=c->selectionController->ReadActiveLine();auto recent=Recent();bool fail=true;int opened=0;
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");old=nullptr;Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());EXPECT_EQ(0,opened);
    ASSERT_NE(nullptr,old);EXPECT_EQ("kept by retired model",old->Text.get());
    EXPECT_FALSE(c->subtitleAccess->TryWrite());EXPECT_FALSE(c->subsController->CanSave());
    EXPECT_THROW(c->subsController->Close(),std::logic_error);
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);
    EXPECT_FALSE(c->subsController->RetryModelInstallation());EXPECT_EQ(0,opened);
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());EXPECT_EQ(nullptr,old);
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_EQ(1,opened);
    EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty());EXPECT_EQ(recent,Recent());
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    EXPECT_THROW(c->subsController->RetryModelInstallation(),std::logic_error);EXPECT_EQ(1,opened); // plan consumed once
    c->subsController->Load(path,"utf-8");EXPECT_EQ(2,opened);ASSERT_FALSE(Recent().empty());EXPECT_EQ(path,Recent().front());
}
TEST_F(document_lifecycle, load_follow_up_failures_are_reported_after_commit_and_do_not_suppress_later_steps) {
    auto backup=OPT_GET("App/Auto/Backup")->GetBool();auto backup_path=OPT_GET("Path/Auto/Backup")->GetString();
    BOOST_SCOPE_EXIT_ALL(&) { OPT_SET("App/Auto/Backup")->SetBool(backup);OPT_SET("Path/Auto/Backup")->SetString(backup_path); };
    auto blocker=std::filesystem::current_path()/"backup-blocker";std::ofstream(blocker)<<"file, not a directory";
    OPT_SET("App/Auto/Backup")->SetBool(true);OPT_SET("Path/Auto/Backup")->SetString((blocker/"nested").string());
    int opened=0;agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    auto path=Candidate("follow-up");auto epoch=c->subtitleEditGate.Revision().document_epoch;
    EXPECT_NO_THROW(c->subsController->Load(path,"utf-8"));
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_EQ(epoch+1,c->subtitleEditGate.Revision().document_epoch);
    ASSERT_EQ(1u,c->subsController->LastPeripheralErrors().size());
    EXPECT_NE(std::string::npos,c->subsController->LastPeripheralErrors().front().find("Backup"));
    EXPECT_EQ(1,opened);EXPECT_EQ(path,c->subsController->Filename());ASSERT_FALSE(Recent().empty());EXPECT_EQ(path,Recent().front());
    EXPECT_EQ(path.parent_path().string(),OPT_GET("Path/Last/Subtitles")->GetString());
    EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->CanSave());
    agi::signal::Connection failing=c->subsController->AddFileOpenListener([&](agi::fs::path const&){throw std::runtime_error("listener failure");});
    EXPECT_NO_THROW(c->subsController->Load(path,"utf-8"));EXPECT_EQ(epoch+2,c->subtitleEditGate.Revision().document_epoch);
    EXPECT_EQ(2u,c->subsController->LastPeripheralErrors().size());EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());EXPECT_TRUE(c->subtitleAccess->TryWrite());
    failing.Disconnect();OPT_SET("App/Auto/Backup")->SetBool(false);
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty()); // cleared per installation

    // A real MRU persistence failure must not suppress backup or FileOpen, or
    // turn an already committed installation into a failed load.
    agi::MRUManager blocked_mru(Local("backup-blocker/mru.json"), "{}", config::opt);
    auto original_mru=config::mru;config::mru=&blocked_mru;
    BOOST_SCOPE_EXIT_ALL(&) { config::mru=original_mru; };
    auto good_backup=Local("mru-failure-backup");
    OPT_SET("App/Auto/Backup")->SetBool(true);
    OPT_SET("Path/Auto/Backup")->SetString(good_backup.string());
    auto previous_opened=opened;
    EXPECT_NO_THROW(c->subsController->Load(path,"utf-8"));
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    ASSERT_EQ(1u,c->subsController->LastPeripheralErrors().size());
    EXPECT_NE(std::string::npos,c->subsController->LastPeripheralErrors().front().find("Recent files"));
    EXPECT_EQ(previous_opened+1,opened);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path((good_backup/(path.stem().string()+".ORIGINAL"+path.extension().string())).wstring())));
    EXPECT_EQ(path,c->subsController->Filename());EXPECT_TRUE(c->subsController->CanSave());
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
}
TEST_F(document_lifecycle, project_entries_distinguish_parse_failure_recovery_blocked_and_follow_up_warnings) {
    RecordMessages messages;Input("kept");auto before=Capture();
    auto unsupported=Local("unsupported.qqq");std::ofstream(std::filesystem::path(unsupported.wstring()))<<"not subtitles";
    c->project->LoadSubtitles(unsupported,"utf-8");ASSERT_EQ(1u,messages.messages.size());Unchanged(before);
    c->project->LoadSubtitles(Local("missing-project-input.ass"),"utf-8");ASSERT_EQ(2u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().Contains("not found"));Unchanged(before);
    auto path=Candidate("project-recovery");bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    c->project->LoadSubtitles(path,"utf-8");ASSERT_EQ(3u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().Contains("Close Aegisub to recover"));EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    EXPECT_NO_THROW(c->project->CloseSubtitles());ASSERT_EQ(4u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().Contains("Close Aegisub to recover"));EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());throwing.Disconnect();
    agi::signal::Connection failing=c->subsController->AddFileOpenListener([&](agi::fs::path const&){throw std::runtime_error("listener failure");});
    auto second=Candidate("project-warning");c->project->LoadSubtitles(second,"utf-8");ASSERT_EQ(5u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().StartsWith("The subtitles were opened"));EXPECT_TRUE(messages.messages.back().Contains("listener failure"));
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_EQ(second,c->subsController->Filename());
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
}
TEST_F(document_lifecycle, project_close_reports_rejection_without_throwing_then_closes_once) {
    RecordMessages messages;auto path=Candidate("project-close");c->subsController->Load(path,"utf-8");Input("kept");
    int opened=0;agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    auto before=Capture();
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_NO_THROW(c->project->CloseSubtitles());ASSERT_EQ(1u,messages.messages.size());Unchanged(before);EXPECT_EQ(0,opened);
    }
    std::vector<int> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);Ack(t);});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);Ack(t);});
    c->project->CloseSubtitles();EXPECT_EQ(1u,messages.messages.size());EXPECT_EQ(1,opened);
    EXPECT_EQ((std::vector<int>{1,2}),order); // no second selection announcement after the installation
    EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());
}
TEST_F(document_lifecycle, project_load_announces_installed_selection_once) {
    RecordMessages messages;auto path=Candidate("project-selection");std::vector<int> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);Ack(t);});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);Ack(t);});
    c->project->LoadSubtitles(path,"utf-8");EXPECT_TRUE(messages.messages.empty());
    EXPECT_EQ((std::vector<int>{1,2}),order);EXPECT_EQ(path,c->subsController->Filename());
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
}
TEST_F(document_lifecycle, open_close_reload_save_and_reopen_keep_path_mru_and_metadata_consistent) {
    auto a=Candidate("cycle-a");c->subsController->Load(a,"utf-8");
    EXPECT_EQ(a,Recent().front());EXPECT_EQ(a.parent_path(),c->path->Decode("?script"));
    EXPECT_EQ(a.parent_path().string(),OPT_GET("Path/Last/Subtitles")->GetString());
    Input("edited a");c->subtitleAccess->SetAutomationSetting("cycle","kept");c->subsController->Save(a,"utf-8");
    EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Close();EXPECT_NE(a,c->subsController->Filename());EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ(a,Recent().front());
    c->subsController->Load(a,"utf-8");EXPECT_EQ("edited a",LiveRead(*c).Events.front().Text.get());
    EXPECT_TRUE(c->subsController->IsUndoStackEmpty());EXPECT_FALSE(c->subsController->IsModified());EXPECT_FALSE(c->subtitleAccess->MetadataDirty());
    Input("second edit");auto b=Local("cycle-b.ass");c->subsController->Save(b,"utf-8");
    EXPECT_EQ(b,c->subsController->Filename());EXPECT_EQ(b,Recent().front());EXPECT_EQ(b.parent_path(),c->path->Decode("?script"));
    AssFile reopened;SubtitleFormat::GetReader(b,"utf-8")->ReadFile(&reopened,b,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("second edit",reopened.Events.front().Text.get());EXPECT_TRUE(reopened.Events.front().Comment);
    ASSERT_EQ(1u,reopened.Extradata.size());EXPECT_EQ("aegisub.enhanced.subtitle-binding",reopened.Extradata.front().key);
    c->subsController->Close();c->subsController->Load(b,"utf-8");EXPECT_EQ("second edit",LiveRead(*c).Events.front().Text.get());
}
TEST_F(document_lifecycle, explicit_retry_with_real_grid_and_dialogs_repeats_rebinding_without_duplicate_side_effects) {
    Input("old dialog input");auto path=Candidate("retry-ui");c->subsGrid=new BaseGrid(window,c.get());
    {
        DialogStyling styling(c.get());DialogTranslation translation(c.get());
        bool fail=true;int opened=0;auto recent=Recent();
        agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
        agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
        auto metadata=c->subtitleAccess->MetadataRevision();
        EXPECT_THROW(c->subsController->Load(path,"utf-8"),SubtitleModelRecoveryBlocked);
        auto published=c->subtitleAccess->MetadataRevision();auto revision=c->subtitleEditGate.Revision();
        EXPECT_EQ(metadata+1,published);EXPECT_EQ(0,opened);EXPECT_EQ(recent,Recent());
        EXPECT_FALSE(c->subsController->RetryModelInstallation()); // every fixed observer dispatched again
        EXPECT_EQ(published,c->subtitleAccess->MetadataRevision());EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(0,opened);
        fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
        EXPECT_EQ(published,c->subtitleAccess->MetadataRevision());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
        EXPECT_EQ(1,opened);EXPECT_EQ(path,Recent().front());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
        EXPECT_FALSE(c->subsController->IsModified());EXPECT_FALSE(c->subtitleAccess->MetadataDirty());
        EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());EXPECT_EQ("new 模型",std::string(editor->GetTextRaw().data()));
        Input("edited after retry");c->subsController->Undo();EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
        c->subsController->Redo();EXPECT_EQ("edited after retry",LiveRead(*c).Events.front().Text.get());
    }
    c->subsGrid=nullptr;
}
TEST_F(document_lifecycle, autosave_result_completing_after_close_cannot_touch_the_new_document) {
    auto save=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString((std::filesystem::current_path()/"autosave").string());
    BOOST_SCOPE_EXIT_ALL(&) { OPT_SET("Path/Auto/Save")->SetString(save); };
    auto path=Candidate("autosave-close");c->subsController->Load(path,"utf-8");Input("pending autosave");
    c->subsController->RequestAutoSave();c->subsController->Close();Input("new document edit");
    EXPECT_TRUE(c->subsController->IsModified());
    for(int i=0;i<200 && wxTheApp->HasPendingEvents();++i)wxTheApp->ProcessPendingEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));wxTheApp->ProcessPendingEvents();
    EXPECT_EQ("new document edit",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo();EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_FALSE(c->subsController->IsModified());
}
TEST_F(document_lifecycle, lua_local_scripts_reload_is_post_commit_follow_up_not_isolated_work) {
    // Boundary record: automation still reads the live document from its FileOpen listener.
    // It runs only after the installation is Committed and is not migrated by C1-C1c-8.
    auto path=Candidate("lua-boundary");int opened=0;
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){
        ++opened;EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    });
    c->subsController->Load(path,"utf-8");EXPECT_EQ(1,opened);EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty());
}
}
TEST_F(visual_edit, production_load_rebinds_real_visual_cache_and_renderer_before_next_gesture) {
    Install<VisualToolDrag>();auto old_id=LiveRead(*c).Events.front().Id;
    AssFile source;source.LoadDefault();source.Events.front().Start=0;source.Events.front().End=5000;
    source.SetScriptInfo("PlayResX","640");source.SetScriptInfo("PlayResY","480");
    source.Events.front().Text="{\\pos(100,100)\\bord7}new visual model";
    auto path=agi::fs::path((std::filesystem::current_path()/"visual-install.ass").wstring());
    SubtitleFormat::GetWriter(path)->WriteFile(&source,path,0,"utf-8");
    c->subsController->Load(path,"utf-8");EXPECT_EQ(SubtitleModelInstallation::State::Committed,c->subsController->InstallationStatus());
    EXPECT_NE(old_id,LiveRead(*c).Events.front().Id);EXPECT_EQ(source.Events.front().Text.get(),Text());EXPECT_FALSE(FrameData().empty());
    auto before=Text();Mouse(wxEVT_LEFT_DOWN,100,100,true);ASSERT_TRUE(display->HasCapture());
    Mouse(wxEVT_MOTION,150,160,true);Mouse(wxEVT_LEFT_UP,150,160);EXPECT_NE(before,Text());
    c->subsController->Undo();EXPECT_EQ(before,Text());c->subsController->Redo();EXPECT_NE(before,Text());
}
static_assert(!std::is_constructible_v<SubtitleModelInstallation,SubsController&>);

// C1-C1c-10: the privatized owner still holds exactly one live document; the
// production read path is that document, copies are detached, and runtime
// authority (revision tokens, history restore, document replacement) is unchanged.
TEST_F(ordinary_edit, owner_privatization_keeps_one_live_document_read_only_path_and_detached_copies) {
    AssFile const* live=&LiveRead(*c);
    EXPECT_EQ(live,&LiveSubtitleModelTestAccess::Model(*c));
    Input("owner edit");
    ASSERT_EQ("owner edit",LiveRead(*c).Events.front().Text.get());
    auto id=LiveRead(*c).Events.front().Id;
    auto undo=c->subsController->GetUndoDescription();
    auto revision=c->subtitleEditGate.Capture();

    auto snapshot=c->subtitleAccess->Snapshot();
    snapshot->Events.front().Text="detached";snapshot->AddExtradata("k","v");snapshot->Events.clear_and_dispose([](AssDialogue* row){delete row;});
    AssFile copy(LiveRead(*c));copy.Events.front().Text="copy";copy.Commit("copy",AssFile::COMMIT_DIAG_TEXT);
    EXPECT_EQ("owner edit",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.Authorizes(revision));

    std::thread worker([&]{ EXPECT_THROW((void)LiveRead(*c),std::logic_error); });
    worker.join();

    c->subsController->Undo();EXPECT_NE("owner edit",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("owner edit",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(live,&LiveRead(*c));
    EXPECT_FALSE(c->subtitleEditGate.Authorizes(revision));

    auto before_close=c->subtitleEditGate.Capture();
    c->subsController->Close();
    EXPECT_EQ(live,&LiveRead(*c));
    EXPECT_FALSE(c->subtitleEditGate.Authorizes(before_close));
    EXPECT_NE("owner edit",LiveRead(*c).Events.front().Text.get());
}

// C1-C1c-11 A1 (audit probe): a throwing newer commit observer used to stop
// delivery, leaving older observers (BaseGrid/SubsEditBox) on freed rows.
TEST_F(ordinary_edit, throwing_newer_commit_observer_still_reaches_older_observers_and_keeps_deleted_rows_alive) {
    cmd::call("edit/line/duplicate",c.get());
    ASSERT_EQ(2u,LiveRead(*c).Events.size());
    auto doomed=&LiveRead(*c).Events.back();auto doomed_id=doomed->Id;
    c->selectionController->SetSelectionAndActiveIDs({doomed_id},doomed_id);
    int older_calls=0;
    agi::signal::Connection older=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*){++older_calls;});
    agi::signal::Connection newer=LiveRead(*c).AddCommitListener([](int,AssDialogue const*){throw std::runtime_error("newer observer");});
    auto before=c->subtitleAccess->RetainedRetiredNodes();
    EXPECT_THROW(cmd::call("edit/line/delete",c.get()),std::runtime_error);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());
    EXPECT_EQ(1,older_calls);
    EXPECT_EQ(before+1,c->subtitleAccess->RetainedRetiredNodes());
    EXPECT_EQ(doomed_id,doomed->Id) << "retired row must remain valid for observers that may still reference it";
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    newer.Disconnect();
    EXPECT_TRUE(c->subtitleAccess->TryWrite().has_value());
}

// C1-C1c-11 B1/B2 (audit probe): Undo/Redo restoration prepares the target state
// before touching the model or the history stacks; a failed announcement keeps
// the replaced rows alive and the model and history consistent with each other.
TEST_F(ordinary_edit, undo_with_throwing_commit_observer_keeps_model_history_and_replaced_rows_consistent) {
    auto commit=[&](char const* text,char const* message){ auto w=c->subtitleAccess->TryWrite();ASSERT_TRUE(w);
        LiveWriteForTest(*c).Events.front().Text=text;LiveWriteForTest(*c).Commit(message,AssFile::COMMIT_DIAG_TEXT); };
    commit("one","step one"); commit("two","step two");
    auto replaced=&LiveRead(*c).Events.front();auto replaced_id=replaced->Id;
    auto replaced_style=&LiveRead(*c).Styles.front();auto style_name=replaced_style->name;
    int older_calls=0;
    agi::signal::Connection older=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*){++older_calls;});
    agi::signal::Connection thrower=LiveRead(*c).AddCommitListener([](int,AssDialogue const*){throw std::runtime_error("observer");});
    EXPECT_THROW(c->subsController->Undo(),std::runtime_error);
    EXPECT_EQ(1,older_calls);
    // Model and history agree: the undo took effect and can be redone.
    EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("step two",c->subsController->GetRedoDescription());
    // The replaced nodes were parked, not freed.
    EXPECT_EQ(replaced_id,replaced->Id);EXPECT_EQ(style_name,replaced_style->name);
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    thrower.Disconnect();
    EXPECT_TRUE(c->subtitleAccess->TryWrite().has_value());
    c->subsController->Redo();
    EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo();c->subsController->Redo();c->subsController->Undo();
    EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(1u,LiveRead(*c).Styles.size());
}

// C1-C1c-11 A2 (audit probe): media changes in RecoveryBlocked used to replace
// the provider, then fail in SetMetadata before announcing it, leaving the
// audio controller on a freed provider. They are now refused before any effect.
TEST_F(document_lifecycle, media_changes_are_refused_without_side_effects_while_recovery_is_blocked) {
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    auto first=c->project->AudioProvider();ASSERT_TRUE(first);int announced=0;
    agi::signal::Connection l=c->project->AddAudioProviderListener([&](agi::AudioProvider*){++announced;});
    auto path=Candidate("media-recovery");c->subsController->Load(path,"utf-8");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    RecordMessages messages;
    auto metadata=c->subtitleAccess->MetadataRevision();
    EXPECT_NO_THROW(c->project->LoadAudio("dummy-audio:silence?sr=22050&bd=16&ch=1&ln=220500"));
    EXPECT_EQ(first,c->project->AudioProvider());
    EXPECT_NO_THROW(c->project->CloseAudio());
    EXPECT_EQ(first,c->project->AudioProvider());
    EXPECT_NO_THROW(c->project->CloseVideo());
    EXPECT_NO_THROW(c->project->CloseTimecodes());
    EXPECT_NO_THROW(c->project->CloseKeyframes());
    EXPECT_NO_THROW(c->project->LoadList({}));
    EXPECT_EQ(0,announced);EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
    ASSERT_EQ(6u,messages.messages.size());
    for(auto const& m:messages.messages)EXPECT_TRUE(m.Contains("Close Aegisub to recover"));
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());throwing.Disconnect();
    c->project->CloseAudio();EXPECT_EQ(1,announced);EXPECT_FALSE(c->project->AudioProvider());
}

// C1-C1c-11 A3 (audit finding): ~DialogExport stored the filter selection with
// SetMetadata, which throws while recovery is blocked -> std::terminate. The
// destructor must not throw, whether the metadata is sealed or an observer throws.
TEST_F(document_lifecycle, export_dialog_closes_without_terminating_when_metadata_cannot_be_stored) {
    {
        bool fail=true;
        agi::signal::Connection throwing=c->subtitleAccess->AddMetadataListener([&]{if(fail)throw std::runtime_error("metadata observer");});
        RunModal close("Export",[&](wxDialog*){});
        EXPECT_NO_THROW(ShowExportDialog(c.get()));
        fail=false;
    }
    auto path=Candidate("export-recovery");c->subsController->Load(path,"utf-8");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    auto metadata=c->subtitleAccess->MetadataRevision();
    {RunModal close("Export",[&](wxDialog*){});EXPECT_NO_THROW(ShowExportDialog(c.get()));}
    EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
}

// C1-C1c-11 A4 (audit finding): the crash handler saved with Save(), which
// throws for an installing or RecoveryBlocked model, inside the fatal-exception
// handler. The recovery copy works in every state and never throws.
TEST_F(document_lifecycle, recovery_copy_is_written_in_recovery_blocked_without_touching_document_state) {
    auto path=Candidate("crash-recovery");c->subsController->Load(path,"utf-8");Input("unsaved work");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    auto filename=c->subsController->Filename();auto modified=c->subsController->IsModified();
    auto metadata=c->subtitleAccess->MetadataRevision();
    EXPECT_THROW(c->subsController->Save(Local("recovered-save.ass")),std::logic_error);
    auto target=Local("recovered-copy.ass");
    EXPECT_TRUE(c->subsController->WriteRecoveryCopy(target));
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(target.wstring())));
    AssFile reread;SubtitleFormat::GetReader(target,"utf-8")->ReadFile(&reread,target,{},"utf-8");
    EXPECT_EQ(LiveRead(*c).Events.size(),reread.Events.size());
    EXPECT_EQ(filename,c->subsController->Filename());EXPECT_EQ(modified,c->subsController->IsModified());
    EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
    EXPECT_FALSE(c->subsController->WriteRecoveryCopy(Local("missing-dir/sub/recovered.ass")));
    EXPECT_FALSE(c->subsController->WriteRecoveryCopy(Local("recovered.unknown-format")));
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
}

// C1-C1c-11 B5 (audit finding): GetSubtitles read the worker-owned render copy
// and subtitle provider from the UI thread without synchronizing with the
// worker. It now runs on the worker after every queued subtitle update.
TEST_F(visual_edit, subtitle_overlay_is_rendered_on_the_worker_after_queued_updates) {
    auto set=[&](char const* text){auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);
        LiveWriteForTest(*c).Events.front().Text=text;LiveWriteForTest(*c).Commit("overlay",AssFile::COMMIT_DIAG_TEXT);};
    auto alpha_at=[&](int x,int y){
        auto frame=c->project->VideoProvider()->GetSubtitles(1.0);
        return frame.data[(y*frame.width+x)*4+3];
    };
    char const* square="{\\an7\\pos(0,0)\\bord0\\shad0\\1c&HFFFFFF&\\p1}m 0 0 l 200 0 200 200 0 200";
    for(int i=0;i<20;++i) {
        set(square);EXPECT_EQ(255,alpha_at(100,100)) << "iteration " << i;
        set("");EXPECT_EQ(0,alpha_at(100,100)) << "iteration " << i;
    }
}

// C1-C1c-12 (audit C1): during a comparison session, Save cannot move the file
// name, ?script, relative paths or the saved baseline; AutoSave is deferred;
// media changes are refused; Export still works. All without side effects.
TEST_F(document_lifecycle, comparison_session_freezes_save_target_metadata_and_media) {
    auto saved=Local("compare-saved.ass");c->subsController->Save(saved,"utf-8");
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    auto provider=c->project->AudioProvider();ASSERT_TRUE(provider);
    Input("compared edit");ASSERT_TRUE(c->subsController->IsModified());
    auto autosave_root=std::filesystem::current_path()/"compare-autosave";
    auto old_autosave=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString(autosave_root.string());
    auto filename=c->subsController->Filename();auto script=c->path->Decode("?script");
    auto properties=LiveRead(*c).Properties;auto metadata=c->subtitleAccess->MetadataRevision();
    auto revision=c->subtitleEditGate.Revision();
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        RecordMessages messages;
        auto elsewhere=Local("compare-elsewhere.ass");
        EXPECT_THROW(c->subsController->Save(elsewhere,"utf-8"),SubtitleProjectBusy);
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(elsewhere.wstring())));
        // The production command reports the refusal instead of failing silently.
        EXPECT_NO_THROW(cmd::call("subtitle/save",c.get()));
        ASSERT_EQ(1u,messages.messages.size());EXPECT_TRUE(messages.messages.back().Contains("being compared"));
        c->subsController->RequestAutoSave();
        EXPECT_FALSE(std::filesystem::exists(autosave_root));
        EXPECT_NO_THROW(c->project->LoadAudio("dummy-audio:silence?sr=22050&bd=16&ch=1&ln=220500"));
        EXPECT_NO_THROW(c->project->CloseAudio());
        EXPECT_EQ(provider,c->project->AudioProvider());
        ASSERT_EQ(3u,messages.messages.size());EXPECT_TRUE(messages.messages.back().Contains("being compared"));
        // Read-only output stays available.
        auto exported=Local("compare-export.srt");AssExporter exporter(c.get());
        EXPECT_NO_THROW(exporter.Export(exported,"utf-8",nullptr));
        EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(exported.wstring())));
        EXPECT_EQ(filename,c->subsController->Filename());EXPECT_EQ(script,c->path->Decode("?script"));
        EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
        EXPECT_TRUE(static_cast<ProjectMetadata const&>(properties)==static_cast<ProjectMetadata const&>(LiveRead(*c).Properties));
        EXPECT_EQ(revision,c->subtitleEditGate.Revision());
        EXPECT_TRUE(comparison->Valid());
    }
    // After the comparison everything works again, including the deferred autosave.
    c->subsController->RequestAutoSave();c->subsController.reset();wxTheApp->ProcessPendingEvents();
    EXPECT_TRUE(std::filesystem::exists(autosave_root));
    OPT_SET("Path/Auto/Save")->SetString(old_autosave);
}
TEST_F(document_lifecycle, comparison_cannot_begin_while_a_save_or_media_change_is_in_progress) {
    bool began=true;int saves=0;
    agi::signal::Connection on_save=c->subsController->AddFileSaveListener([&]{++saves;began=c->subtitleEditGate.TryBeginComparison().has_value();});
    c->subsController->Save(Local("in-progress.ass"),"utf-8");
    EXPECT_EQ(1,saves);EXPECT_FALSE(began);
    began=true;
    agi::signal::Connection on_audio=c->project->AddAudioProviderListener([&](agi::AudioProvider*){began=c->subtitleEditGate.TryBeginComparison().has_value();});
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    EXPECT_FALSE(began);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

// C1-C1c-13: modal dialogs without write leases (B6), save failures (C2),
// opening inside notifications (C4) and atomic multi-step writes (C6).
namespace {
class ProbeModal : public wxModalDialogHook {
    std::function<int(wxDialog*)> enter;
public:
    explicit ProbeModal(std::function<int(wxDialog*)> enter) : enter(std::move(enter)) { Register(); }
    int Enter(wxDialog* dialog) override { return enter(dialog); }
    void Exit(wxDialog*) override {}
};
wxTextCtrl* AssColorInput(wxDialog* dialog) {
    for(auto child:descendants(dialog))
        if(auto text=dynamic_cast<wxTextCtrl*>(child);text && text->GetValue().StartsWith("&H"))return text;
    return nullptr;
}
}
TEST_F(ordinary_edit, color_picker_preview_is_an_interaction_and_cancel_undoes_it) {
    Input("sample");
    auto before=c->subsController->GetUndoDescription();
    bool previewed=false,comparison_refused=false;
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            previewed=LiveRead(*c).Events.front().Text.get().find("0000FF")!=std::string::npos;
            comparison_refused=!c->subtitleEditGate.TryBeginComparison().has_value();
        });
        cmd::call("edit/color/primary",c.get());
    }
    EXPECT_TRUE(previewed);EXPECT_TRUE(comparison_refused);
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(before,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison().has_value());
}
TEST_F(ordinary_edit, color_picker_cancel_never_undoes_a_commit_made_while_it_was_open) {
    Input("sample");
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            // Another handler commits through the shared write scope meanwhile.
            auto write=c->subtitleAccess->TryWrite();ASSERT_TRUE(write);
            AssDialogueBase row=LiveRead(*c).Events.front();row.Actor="other handler";
            auto edit=c->subtitleAccess->Edit(write->Lease());edit.UpdateRows({row});
            edit.Commit("other handler",AssFile::COMMIT_DIAG_META);
            input->SetValue("&H00FF00&"); // the preview session is stale: ignored
        });
        cmd::call("edit/color/primary",c.get());
    }
    EXPECT_EQ("other handler",LiveRead(*c).Events.front().Actor.get());
    EXPECT_EQ(wxString("other handler"),c->subsController->GetUndoDescription());
    EXPECT_EQ(std::string::npos,LiveRead(*c).Events.front().Text.get().find("00FF00"));
    EXPECT_NE(std::string::npos,LiveRead(*c).Events.front().Text.get().find("0000FF"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison().has_value());
}
TEST_F(ordinary_edit, style_panel_colour_picker_previews_as_an_interaction) {
    Input("sample");
    auto panel=new StylePanel(window,c.get());
    panel->UpdateNow();
    auto before=c->subsController->GetUndoDescription();
    bool previewed=false,comparison_refused=false;
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            previewed=LiveRead(*c).Events.front().Text.get()=="{\\c&H0000FF&}sample";
            comparison_refused=!c->subtitleEditGate.TryBeginComparison().has_value();
        });
        panel->PickColour("fill");
    }
    EXPECT_TRUE(previewed);EXPECT_TRUE(comparison_refused);
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get()) << "cancel undoes the preview";
    EXPECT_EQ(before,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison().has_value());
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            auto write=c->subtitleAccess->TryWrite();ASSERT_TRUE(write);
            AssDialogueBase row=LiveRead(*c).Events.front();row.Actor="other handler";
            auto edit=c->subtitleAccess->Edit(write->Lease());edit.UpdateRows({row});
            edit.Commit("other handler",AssFile::COMMIT_DIAG_META);
            input->SetValue("&H00FF00&");
        });
        panel->PickColour("outline");
    }
    EXPECT_EQ(wxString("other handler"),c->subsController->GetUndoDescription()) << "never undone";
    EXPECT_EQ("{\\3c&H0000FF&}sample",LiveRead(*c).Events.front().Text.get()) << "and the stale preview is ignored";
    panel->Destroy();
}
TEST_F(ordinary_edit, font_resample_and_style_manager_questions_hold_no_write_lease) {
    Input("sample");
    std::vector<std::pair<wxString,bool>> seen;
    auto description=c->subsController->GetUndoDescription();auto styles=LiveRead(*c).Styles.size();
    ShowStyleManagerDialog(c.get());
    wxDialog* manager=nullptr;
    for(auto top:wxTopLevelWindows) if(auto d=dynamic_cast<wxDialog*>(top);d && d->GetTitle()=="Styles Manager")manager=d;
    ASSERT_NE(nullptr,manager); manager->Hide();
    auto cleanup=std::unique_ptr<wxDialog,std::function<void(wxDialog*)>>(manager,[](wxDialog* d){d->Close();});
    wxButton *import=nullptr,*remove=nullptr;wxListBox *current=nullptr;
    for(auto child:descendants(manager)) {
        if(auto b=dynamic_cast<wxButton*>(child); b && b->GetLabel()=="&Import from script...")import=b;
        if(child->GetParent()->GetLabel()=="Current script") {
            if(auto b=dynamic_cast<wxButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
            if(auto list=dynamic_cast<wxListBox*>(child))current=list;
        }
    }
    ASSERT_NE(nullptr,import);ASSERT_NE(nullptr,remove);ASSERT_NE(nullptr,current);
    ProbeModal probe([&](wxDialog* dialog){
        seen.emplace_back(dialog->GetTitle(),c->subtitleEditGate.TryBeginComparison().has_value());
        return dynamic_cast<wxMessageDialog*>(dialog)?wxID_NO:wxID_CANCEL;
    });
    cmd::call("edit/font",c.get());
    cmd::call("tool/resampleres",c.get());
    current->DeselectAll();current->SetStringSelection("Default");button(remove,remove->GetId());
    button(import,import->GetId());
    ASSERT_EQ(4u,seen.size());
    for(auto const& [title,free]:seen) EXPECT_TRUE(free) << title;
    EXPECT_EQ(description,c->subsController->GetUndoDescription());EXPECT_EQ(styles,LiveRead(*c).Styles.size());
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, joined_lines_are_committed_before_selection_observers_see_them) {
    int first=LiveRead(*c).Events.front().Id,second=0;
    {
        auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;row->Text="second";second=row->Id;
        LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("join fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    std::vector<wxString> seen;std::vector<size_t> rows;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&]{
        seen.push_back(c->subsController->GetUndoDescription());rows.push_back(LiveRead(*c).Events.size());});
    cmd::call("edit/line/join/concatenate",c.get());
    ASSERT_FALSE(seen.empty());
    for(size_t i=0;i<seen.size();++i){EXPECT_EQ(wxString("join lines"),seen[i]);EXPECT_EQ(1u,rows[i]);}
    ASSERT_EQ(1u,LiveRead(*c).Events.size());
    c->subsController->Undo();EXPECT_EQ(2u,LiveRead(*c).Events.size());
}
TEST_F(document_lifecycle, failed_save_restores_metadata_through_its_listeners) {
    c->subsController->Save(Local("rollback.ass"),"utf-8");
    auto filename=c->subsController->Filename();
    auto original=LiveRead(*c).Properties.export_encoding;
    std::string seen="unset";int notices=0;
    agi::signal::Connection metadata=c->subtitleAccess->AddMetadataListener([&]{++notices;seen=LiveRead(*c).Properties.export_encoding;});
    agi::signal::Connection on_save=c->subsController->AddFileSaveListener([&]{
        c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ExportEncoding,"changed during save");
        throw std::runtime_error("save listener");
    });
    auto revision=c->subtitleAccess->MetadataRevision();
    EXPECT_THROW(c->subsController->Save(Local("rollback-2.ass"),"utf-8"),std::runtime_error);
    EXPECT_EQ(original,LiveRead(*c).Properties.export_encoding);
    EXPECT_EQ(original,seen); // the listeners were told about the restored value
    EXPECT_EQ(2,notices);EXPECT_EQ(revision+2,c->subtitleAccess->MetadataRevision());
    EXPECT_EQ(filename,c->subsController->Filename());EXPECT_FALSE(c->subsController->IsModified());
}
TEST_F(document_lifecycle, failed_save_on_every_change_still_announces_the_recorded_commit) {
    c->subsController->Save(Local("every-change.ass"),"utf-8");
    auto old=OPT_GET("App/Auto/Save on Every Change")->GetBool();
    OPT_SET("App/Auto/Save on Every Change")->SetBool(true);
    BOOST_SCOPE_EXIT_ALL(old) { OPT_SET("App/Auto/Save on Every Change")->SetBool(old); };
    bool fail=true;int saves=0,notices=0;
    agi::signal::Connection on_save=c->subsController->AddFileSaveListener([&]{++saves;if(fail)throw std::runtime_error("disk full");});
    agi::signal::Connection commit=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++notices;Ack(t);});
    auto edit_row=[&](std::string const& text,wxString const& message) {
        auto write=c->subtitleAccess->TryWrite();ASSERT_TRUE(write);
        AssDialogueBase row=LiveRead(*c).Events.front();row.Text=text;
        auto edit=c->subtitleAccess->Edit(write->Lease());edit.UpdateRows({row});
        EXPECT_NO_THROW(edit.Commit(message,AssFile::COMMIT_DIAG_TEXT));
    };
    edit_row("changed while the disk is full","disk full edit");
    EXPECT_EQ(1,saves);EXPECT_EQ(1,notices);
    EXPECT_EQ(wxString("disk full edit"),c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subsController->IsModified());
    fail=false;edit_row("saved again","second edit");
    EXPECT_EQ(2,saves);EXPECT_EQ(2,notices);EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Undo();EXPECT_EQ("changed while the disk is full",LiveRead(*c).Events.front().Text.get());
}
TEST_F(document_lifecycle, opening_or_closing_inside_an_observer_notification_is_refused_without_side_effects) {
    auto path=Candidate("notify-source");
    int first=LiveRead(*c).Events.front().Id,second=0;
    {
        auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;row->Text="second";second=row->Id;
        LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("notify fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    auto before=Contents();auto filename=c->subsController->Filename();
    RecordMessages messages;
    bool ran=false,notifying=false;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){
        if(!ran) {
            ran=true;notifying=LiveRead(*c).IsNotifying();
            EXPECT_THROW(c->subsController->Load(path,"utf-8"),agi::InvalidInputException);
            EXPECT_THROW(c->subsController->Close(),agi::InvalidInputException);
            c->project->LoadSubtitles(path);
            c->project->LoadList({path});
            c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
        }
        Ack(t);
    });
    c->selectionController->SetSelectionAndActiveIDs({second},second);
    EXPECT_TRUE(ran);EXPECT_TRUE(notifying);
    EXPECT_EQ(before,Contents());EXPECT_EQ(filename,c->subsController->Filename());
    EXPECT_EQ(nullptr,c->project->AudioProvider());
    EXPECT_EQ(3u,messages.messages.size());
    // Outside the notification the same open works.
    c->subsController->Load(path,"utf-8");EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
    (void)first;
}
// C1-C1c-14 (audit C5): once the new model is published, tools and dialogs
// opened while recovery is blocked can subscribe; they read the new model and
// are not part of the retried delivery.
TEST_F(document_lifecycle, subscriptions_are_allowed_while_recovery_is_blocked) {
    auto path=Candidate("subscribe-recovery");c->subsController->Load(path,"utf-8");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    int late_commits=0,late_selections=0;std::string seen_text="unset";
    std::optional<agi::signal::Connection> commit,selected;
    EXPECT_NO_THROW(commit.emplace(LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++late_commits;Ack(t);})));
    EXPECT_NO_THROW(selected.emplace(c->selectionController->AddSelectionListener([&](Ticket t){++late_selections;Ack(t);})));
    EXPECT_NO_THROW({ DialogStyling styling(c.get()); seen_text=LiveRead(*c).Events.front().Text.get(); });
    EXPECT_EQ("",seen_text); // the published (blank) model
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    EXPECT_EQ(0,late_commits);EXPECT_EQ(0,late_selections); // not part of the retried delivery
    Input("after recovery");EXPECT_EQ(1,late_commits);
}
// C1-C1c-14 (audit C7): commands that edit the active line do nothing when
// there is none, instead of dereferencing a null line.
TEST_F(ordinary_edit, active_line_commands_do_nothing_without_an_active_line) {
    Input("sample");
    c->selectionController->SetSelectionAndActiveIDs({},0);
    ASSERT_EQ(nullptr,c->selectionController->ReadActiveLine());
    auto description=c->subsController->GetUndoDescription();auto revision=c->subtitleEditGate.Revision();
    EXPECT_FALSE(cmd::get("edit/line/reverse_move")->Validate(c.get()));
    for(auto name:{"edit/line/reverse_move","subtitle/bind","subtitle/unbind","edit/revert","edit/clear","edit/clear/text",
                   "edit/insert_original","edit/font","edit/color/primary","edit/style/bold"})
        EXPECT_NO_THROW(cmd::call(name,c.get())) << name;
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get());
    (void)revision;
}
// C1-C1c-14 (audit C3): pasted lines keep references that resolve in this
// document and drop the rest.
TEST_F(ordinary_edit, pasted_lines_drop_extradata_references_that_do_not_resolve) {
    uint32_t known=0;
    {auto write=c->subtitleAccess->TryWrite();known=LiveWriteForTest(*c).AddExtradata("opaque","here");
     LiveWriteForTest(*c).Commit("extradata fixture",AssFile::COMMIT_EXTRADATA);}
    auto data="Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{=" + std::to_string(known) + "=9999}pasted";
    cmd::PasteSubtitleLines(c.get(),data);
    AssDialogue const* pasted=nullptr;
    for(auto const& row:LiveRead(*c).Events)if(row.Text.get()=="pasted")pasted=&row;
    ASSERT_NE(nullptr,pasted);
    EXPECT_EQ((std::vector<uint32_t>{known}),pasted->ExtradataIds.get());
}
// C1-C1c-14 (audit C8): an autosave that fails after a manual save already
// covered its version must not move the autosave baseline back.
TEST_F(document_lifecycle, failed_autosave_does_not_undo_a_later_manual_save_baseline) {
    c->subsController->Save(Local("autosave-baseline.ass"),"utf-8");
    auto old_dir=OPT_GET("Path/Auto/Save")->GetString();
    BOOST_SCOPE_EXIT_ALL(old_dir) { OPT_SET("Path/Auto/Save")->SetString(old_dir); };
    auto blocker=std::filesystem::current_path()/"autosave-blocker";
    std::ofstream(blocker) << "a file, so the autosave directory cannot be created";
    OPT_SET("Path/Auto/Save")->SetString((blocker/"dir").string());
    Input("autosaved and saved");
    c->subsController->RequestAutoSave();               // fails on the worker
    c->subsController->Save(Local("autosave-baseline.ass"),"utf-8"); // covers the same version
    for(int i=0;i<300;++i){wxTheApp->ProcessPendingEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    auto good=std::filesystem::current_path()/"autosave-good";
    OPT_SET("Path/Auto/Save")->SetString(good.string());
    c->subsController->RequestAutoSave();               // nothing new since the save
    for(int i=0;i<100;++i){wxTheApp->ProcessPendingEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    EXPECT_FALSE(std::filesystem::exists(good));
}

// ---- C2: Apply Engine ------------------------------------------------------
namespace {
std::vector<std::string> LiveTexts(agi::Context& c) {
    std::vector<std::string> out;
    for (auto const& line : LiveRead(c).Events) out.push_back(line.Text.get());
    return out;
}
std::vector<int> LiveIds(agi::Context& c) {
    std::vector<int> out;
    for (auto const& line : LiveRead(c).Events) out.push_back(line.Id);
    return out;
}
std::vector<subtitle_apply::Choice> AllChanges(SubtitleComparison const& comparison) {
    std::vector<subtitle_apply::Choice> out;
    for (size_t i = 0; i < comparison.Items().size(); ++i)
        if (comparison.Items()[i].kind != agi::ass::compare::Kind::Unchanged) out.push_back({i});
    return out;
}
}
TEST_F(ordinary_edit, comparison_freezes_writers_and_apply_is_one_undoable_step) {
    Input("current");
    auto id = LiveRead(*c).Events.front().Id;
    auto baseline = c->subtitleAccess->Snapshot();
    baseline->Events.front().Text = "from baseline";
    auto added = new AssDialogue; added->Text = "added"; added->Start = 9000; added->End = 9500;
    baseline->Events.push_back(*added);
    auto undo_before = c->subsController->GetUndoDescription();

    auto comparison = c->subsController->BeginComparison(std::move(baseline), agi::ass::compare::All);
    ASSERT_TRUE(comparison.Valid());
    EXPECT_FALSE(c->subtitleAccess->TryWrite());                 // ordinary edits are frozen
    EXPECT_FALSE(c->subtitleEditGate.TryBeginProjectWrite());    // so is project state
    EXPECT_THROW(c->subsController->BeginComparison(c->subtitleAccess->Snapshot(), agi::ass::compare::All), SubtitleApplyRejected);
    Input("ignored while comparing");
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));

    int commits = 0, type = -1;
    agi::signal::Connection listener = LiveRead(*c).AddCommitListener([&](int t, AssDialogue const*) { ++commits; type = t; });
    auto choices = AllChanges(comparison);
    ASSERT_EQ(2u, choices.size());
    EXPECT_TRUE(c->subsController->CheckApply(comparison, choices).Ok());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c)); // dry run changes nothing
    auto report = c->subsController->ApplyComparison(comparison, choices);
    ASSERT_TRUE(report.Ok());
    EXPECT_EQ(1u, report.modified); EXPECT_EQ(1u, report.added);
    EXPECT_FALSE(comparison.Valid());
    EXPECT_EQ(1, commits); EXPECT_EQ(AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL, type);
    EXPECT_EQ((std::vector<std::string>{"from baseline", "added"}), LiveTexts(*c));
    auto applied = LiveIds(*c);
    EXPECT_EQ(id, applied[0]);                                   // the modified row keeps its identity
    EXPECT_EQ(std::set<int>(applied.begin(), applied.end()), c->selectionController->SelectedIDs());
    EXPECT_EQ(id, c->selectionController->ActiveID());
    EXPECT_EQ("apply comparison", c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subsController->IsModified());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());                  // the session is released

    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
    EXPECT_EQ((std::vector<int>{id}), LiveIds(*c));
    EXPECT_EQ(undo_before, c->subsController->GetUndoDescription());
    c->subsController->Redo();
    EXPECT_EQ((std::vector<std::string>{"from baseline", "added"}), LiveTexts(*c));
    EXPECT_EQ(applied, LiveIds(*c));
}
TEST_F(ordinary_edit, refused_apply_changes_nothing_and_keeps_the_comparison) {
    auto baseline = c->subtitleAccess->Snapshot();
    for (auto text : {"g1", "g2"}) {
        auto line = new AssDialogue; line->Text = text; line->Start = 5000; line->End = 6000;
        baseline->Events.push_back(*line);
    }
    auto token = baseline->AddExtradata("aegisub.enhanced.subtitle-binding", "group");
    for (auto& line : baseline->Events) if (line.Text.get() != "") line.ExtradataIds = std::vector<uint32_t>{token};
    auto texts = LiveTexts(*c); auto ids = LiveIds(*c);
    auto undo = c->subsController->GetUndoDescription(); auto revision = c->subtitleEditGate.Revision();

    auto comparison = c->subsController->BeginComparison(std::move(baseline), agi::ass::compare::All);
    auto choices = AllChanges(comparison);
    ASSERT_EQ(2u, choices.size());
    auto report = c->subsController->ApplyComparison(comparison, {choices[0]});
    ASSERT_FALSE(report.Ok());
    ASSERT_EQ(1u, report.refusals.size());
    EXPECT_EQ(subtitle_apply::Problem::PartialBinding, report.refusals[0].problem);
    EXPECT_EQ("group", report.refusals[0].detail);
    EXPECT_EQ(texts, LiveTexts(*c)); EXPECT_EQ(ids, LiveIds(*c));
    EXPECT_EQ(undo, c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.IsCurrent(revision));
    EXPECT_TRUE(LiveRead(*c).Extradata.empty());
    EXPECT_TRUE(comparison.Valid());                             // the user can choose again
    EXPECT_FALSE(c->subtitleAccess->TryWrite());

    // Choosing the whole group applies it, with its binding.
    ASSERT_TRUE(c->subsController->ApplyComparison(comparison, choices).Ok());
    ASSERT_EQ(3u, LiveRead(*c).Events.size());
    ASSERT_EQ(1u, LiveRead(*c).Extradata.size());
    EXPECT_EQ("group", LiveRead(*c).Extradata.front().value);
}
TEST_F(ordinary_edit, ending_a_comparison_releases_it_and_stale_or_foreign_use_is_rejected) {
    {
        auto scope = c->subtitleAccess->TryWrite(); ASSERT_TRUE(scope);
        EXPECT_THROW(c->subsController->BeginComparison(c->subtitleAccess->Snapshot(), agi::ass::compare::All), SubtitleApplyRejected);
    }
    auto baseline = c->subtitleAccess->Snapshot(); baseline->Events.front().Text = "b";
    auto comparison = c->subsController->BeginComparison(std::move(baseline), agi::ass::compare::Text | agi::ass::compare::Start | agi::ass::compare::End);
    ASSERT_EQ(1u, AllChanges(comparison).size());
    comparison.End();
    EXPECT_FALSE(comparison.Valid());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    EXPECT_THROW(c->subsController->ApplyComparison(comparison, {{0}}), SubtitleApplyRejected);
    EXPECT_EQ((std::vector<std::string>{""}), LiveTexts(*c));
    // Choosing only Unchanged items ends nothing and changes nothing.
    auto same = c->subsController->BeginComparison(c->subtitleAccess->Snapshot(), agi::ass::compare::All);
    auto report = c->subsController->ApplyComparison(same, {{0}});
    EXPECT_FALSE(report.Ok()); EXPECT_TRUE(same.Valid());
    auto empty = c->subsController->ApplyComparison(same, {});
    EXPECT_TRUE(empty.Ok()); EXPECT_FALSE(same.Valid());         // nothing to apply: ended unchanged
    EXPECT_EQ((std::vector<std::string>{""}), LiveTexts(*c));
}

// ---- C2-B: comparison window ------------------------------------------------
namespace {
// Records message boxes and answers them; Settle() runs @p action on the
// comparison tab once it is open, then ends the comparison if still open.
class CompareHook : public wxModalDialogHook {
    std::function<void(wxWindow*)> action;
public:
    static inline CompareHook* active = nullptr;
    std::vector<wxString> messages;
    int shown = 0;
    int drop_prompts = 0, save_prompts = 0;
    int drop_choice = wxID_CANCEL, save_choice = wxID_CANCEL;
    explicit CompareHook(std::function<void(wxWindow*)> action) : action(std::move(action)) { Register(); active = this; }
    ~CompareHook() { if (active == this) active = nullptr; }
    void Settle(agi::Context& c) {
        auto page = c.workspaceTabs ? c.workspaceTabs->Comparison() : nullptr;
        if (!page) return;
        ++shown;
        if (action) action(page);
        if (c.workspaceTabs->Comparison()) c.workspaceTabs->CloseComparison(false);
    }
    int Enter(wxDialog* dialog) override {
        if (auto message = dynamic_cast<wxMessageDialog*>(dialog)) {
            if (dialog->GetTitle() == "Compare Dropped Subtitles") { ++drop_prompts; return drop_choice; }
            if (dialog->GetTitle() == "Unsaved changes") { ++save_prompts; return save_choice; }
            messages.push_back(message->GetMessage()); return wxID_OK;
        }
        return wxID_NONE;
    }
    void Exit(wxDialog*) override { }
};
template<class T = wxWindow> T* Named(wxWindow* dialog, char const* name) {
    auto window = dynamic_cast<T*>(dialog->FindWindow(name));
    EXPECT_NE(nullptr, window) << name;
    return window;
}
void Press(wxWindow* dialog, char const* name) {
    auto b = Named<wxButton>(dialog, name); ASSERT_NE(nullptr, b);
    ASSERT_TRUE(b->IsEnabled()) << name;
    button(b, b->GetId());
}
wxString Label(wxWindow* dialog, char const* name) {
    auto w = Named(dialog, name);
    return w ? w->GetLabel() : wxString();
}
wxString Report(wxWindow* dialog) {
    auto t = Named<wxTextCtrl>(dialog, "compare-report");
    return t ? t->GetValue() : wxString();
}
// Open the comparison tab for @p path and settle it with the active hook.
// True if the comparison ended by writing to the subtitles.
bool RunCompare(agi::Context* c, agi::fs::path const& path) {
    if (!OpenCompareTab(c, path)) return false;
    if (CompareHook::active) CompareHook::active->Settle(*c);
    return !c->workspaceTabs->Comparison() && c->workspaceTabs->LastApplied();
}
agi::fs::path WriteBaseline(agi::Context& c, std::string const& name, std::function<void(AssFile&)> edit) {
    auto file = c.subtitleAccess->Snapshot();
    edit(*file);
    agi::fs::path path((std::filesystem::current_path() / name).wstring());
    SubtitleFormat::GetWriter(path)->WriteFile(file.get(), path, agi::vfr::Framerate(), "UTF-8");
    return path;
}
}
TEST_F(ordinary_edit, compare_window_writes_decisions_as_one_undo_step) {
    Input("current");
    auto id = LiveRead(*c).Events.front().Id;
    auto path = WriteBaseline(*c, "compare-baseline.ass", [](AssFile& f) {
        f.Events.front().Text = "from baseline";
        auto line = new AssDialogue; line->Text = "added"; line->Start = 9000; line->End = 9500; f.Events.push_back(*line);
    });
    bool writer_blocked = false, unchanged_while_deciding = false;
    wxString counter, finish_first, report, stats;
    CompareHook hook([&](wxWindow* dialog) {
        writer_blocked = !c->subtitleAccess->TryWrite();
        counter = Label(dialog, "compare-counter");
        finish_first = dialog->FindWindow(wxID_OK)->GetLabel();
        EXPECT_NE(wxNOT_FOUND, Label(dialog, "compare-right-title").Find("target project"));
        Press(dialog, "compare-reject-current");          // restore the file's text
        Press(dialog, "compare-next");
        EXPECT_EQ("Difference 2 of 2", Label(dialog, "compare-counter"));
        Press(dialog, "compare-reject-current");          // restore the file's line
        unchanged_while_deciding = LiveTexts(*c) == std::vector<std::string>{"current"};
        report = Report(dialog);
        stats = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_EQ(1, hook.shown); EXPECT_TRUE(hook.messages.empty());
    EXPECT_TRUE(writer_blocked); EXPECT_TRUE(unchanged_while_deciding);
    EXPECT_EQ("Difference 1 of 2", counter);
    EXPECT_EQ("Done", wxStripMenuCodes(finish_first));
    EXPECT_NE(wxNOT_FOUND, report.Find("1 lines modified, 1 added, 0 removed")) << report;
    EXPECT_EQ("0 pending, 0 accepted, 2 rejected, 0 partial", stats);
    EXPECT_EQ((std::vector<std::string>{"from baseline", "added"}), LiveTexts(*c));
    EXPECT_EQ(id, LiveRead(*c).Events.front().Id);
    EXPECT_EQ("apply comparison", c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, compare_window_accepting_keeps_the_project_unchanged) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-fields.ass", [](AssFile& f) {
        f.Events.front().Text = "baseline text"; f.Events.front().Actor = "baseline actor";
    });
    auto texts = LiveTexts(*c); auto undo = c->subsController->GetUndoDescription();
    // Accepting keeps the open subtitles as they are: Done ends the comparison unchanged.
    CompareHook done([&](wxWindow* dialog) {
        Press(dialog, "compare-accept-current");
        EXPECT_EQ("Done", wxStripMenuCodes(dialog->FindWindow(wxID_OK)->GetLabel()));
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    EXPECT_EQ(texts, LiveTexts(*c)); EXPECT_EQ(undo, c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
}
TEST_F(ordinary_edit, compare_window_cancel_with_decisions_confirms_and_changes_nothing) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-cancel.ass", [](AssFile& f) { f.Events.front().Text = "baseline text"; });
    auto texts = LiveTexts(*c); auto undo = c->subsController->GetUndoDescription();
    bool closed = false;
    CompareHook hook([&](wxWindow* dialog) {
        Press(dialog, "compare-reject-current");
        button(dialog->FindWindow(wxID_CANCEL), wxID_CANCEL); // confirmation answered OK by the hook
        closed = !tabs->Comparison();
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("discard 1 decisions")) << hook.messages[0];
    EXPECT_TRUE(closed);
    EXPECT_EQ(texts, LiveTexts(*c)); EXPECT_EQ(undo, c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
}
TEST_F(ordinary_edit, compare_window_takes_only_the_chosen_fields) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-fields2.ass", [](AssFile& f) {
        f.Events.front().Text = "baseline text"; f.Events.front().Actor = "baseline actor";
    });
    wxString stats, actor_tip;
    int actor_y = -1, text_y = -1;
    CompareHook hook([&](wxWindow* dialog) {
        // The current difference changed two fields: each gets a detail row
        // under it, with its own arrows in the gutter.
        auto gutter = dialog->FindWindow("compare-gutter"); ASSERT_NE(nullptr, gutter);
        auto hint = Named<wxStaticText>(dialog, "compare-hint"); ASSERT_NE(nullptr, hint);
        auto mouse = [&](wxEventType type, int x, int y) {
            wxMouseEvent event(type); event.SetPosition({x, y}); event.SetEventObject(gutter);
            gutter->GetEventHandler()->ProcessEvent(event);
        };
        for (int y = 0; y < gutter->GetClientSize().GetHeight(); y += 2) {
            mouse(wxEVT_MOTION, 4, y);
            auto h = hint->GetLabelText();
            if (actor_y < 0 && h.Contains("Actor: take the left side")) { actor_y = y; actor_tip = h; }
            if (text_y < 0 && h.Contains("Text: take the left side")) text_y = y;
        }
        ASSERT_GE(actor_y, 0); ASSERT_GE(text_y, 0);
        // » on the Actor row: keep the left (file) side of that field only.
        mouse(wxEVT_LEFT_DOWN, 4, actor_y);
        stats = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_LT(text_y, actor_y); // in field order, text first
    EXPECT_EQ("0 pending, 0 accepted, 0 rejected, 1 partial", stats);
    EXPECT_EQ("current", LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("baseline actor", LiveRead(*c).Events.front().Actor.get());
}
TEST_F(ordinary_edit, compare_window_decides_detail_fields_by_keyboard) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-keys.ass", [](AssFile& f) {
        f.Events.front().Text = "baseline text"; f.Events.front().Actor = "baseline actor";
    });
    wxString resting, first, second, clamped, after_left, after_escape, whole, undone;
    bool still_open = false;
    CompareHook hook([&](wxWindow* dialog) {
        auto pane = dialog->FindWindow("compare-right"); ASSERT_NE(nullptr, pane);
        auto hint = Named<wxStaticText>(dialog, "compare-hint"); ASSERT_NE(nullptr, hint);
        auto key = [&](wxWindow* target, wxEventType type, int code, bool ctrl) {
            wxKeyEvent event(type); event.m_keyCode = code; event.SetControlDown(ctrl); event.SetEventObject(target);
            target->GetEventHandler()->ProcessEvent(event);
        };
        resting = hint->GetLabelText();
        key(pane, wxEVT_KEY_DOWN, WXK_DOWN, true); first = hint->GetLabelText();
        key(pane, wxEVT_KEY_DOWN, WXK_DOWN, true); second = hint->GetLabelText();
        key(pane, wxEVT_KEY_DOWN, WXK_DOWN, true); clamped = hint->GetLabelText(); // stays on the last field
        key(pane, wxEVT_KEY_DOWN, WXK_LEFT, true);                                 // Actor: the left side
        after_left = Label(dialog, "compare-stats");
        key(dialog, wxEVT_CHAR_HOOK, WXK_ESCAPE, false);                           // leaves the field only
        after_escape = hint->GetLabelText();
        still_open = tabs->Comparison() != nullptr;
        key(pane, wxEVT_KEY_DOWN, WXK_RIGHT, true);                                // no field: the whole line
        whole = Label(dialog, "compare-stats");
        Press(dialog, "compare-undo");
        undone = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_NE(wxNOT_FOUND, resting.Find("Ctrl+Up/Down")) << resting;
    EXPECT_TRUE(first.StartsWith("Text: Ctrl+Left")) << first;
    EXPECT_TRUE(second.StartsWith("Actor: Ctrl+Left")) << second;
    EXPECT_EQ(second, clamped);
    EXPECT_EQ("0 pending, 0 accepted, 0 rejected, 1 partial", after_left);
    EXPECT_EQ(resting, after_escape);
    EXPECT_TRUE(still_open);
    EXPECT_EQ("0 pending, 1 accepted, 0 rejected, 0 partial", whole);
    EXPECT_EQ(after_left, undone);
    EXPECT_EQ("current", LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("baseline actor", LiveRead(*c).Events.front().Actor.get());
}
TEST_F(ordinary_edit, compare_window_filters_by_category_and_leaves_ignored_fields_alone) {
    Input("current");
    auto start = LiveRead(*c).Events.front().Start, end = LiveRead(*c).Events.front().End;
    auto path = WriteBaseline(*c, "compare-filter.ass", [](AssFile& f) {
        f.Events.front().End = f.Events.front().End + 500;      // a time-only change
        auto line = new AssDialogue; line->Text = "added"; line->Start = 9000; line->End = 9500; f.Events.push_back(*line);
    });
    wxString all_counter, time_counter, time_stats, ignored_counter, ignored_stats;
    bool time_hidden = false;
    CompareHook hook([&](wxWindow* dialog) {
        auto toggle = [&](char const* name) {
            auto b = Named<wxToggleButton>(dialog, name); ASSERT_NE(nullptr, b);
            b->SetValue(true);
            wxCommandEvent event(wxEVT_TOGGLEBUTTON, b->GetId()); event.SetEventObject(b); event.SetInt(1);
            b->GetEventHandler()->ProcessEvent(event);
        };
        auto check = [&](char const* name, bool value) {
            auto b = Named<wxCheckBox>(dialog, name); ASSERT_NE(nullptr, b);
            b->SetValue(value);
            wxCommandEvent event(wxEVT_CHECKBOX, b->GetId()); event.SetEventObject(b); event.SetInt(value);
            b->GetEventHandler()->ProcessEvent(event);
        };
        all_counter = Label(dialog, "compare-counter");
        EXPECT_EQ("Time only 1", Named<wxToggleButton>(dialog, "compare-category-Time")->GetLabel());
        EXPECT_FALSE(Named<wxToggleButton>(dialog, "compare-category-Text")->IsShown()); // nothing of that kind
        // Only the time change: Reject All covers just what is shown.
        toggle("compare-category-Time");
        time_counter = Label(dialog, "compare-counter");
        Press(dialog, "compare-reject-all");
        time_stats = Label(dialog, "compare-stats");
        Press(dialog, "compare-undo");
        // Ignoring times instead: the time change is no longer a difference.
        toggle("compare-category-all");
        check("compare-ignore-Time", true);
        ignored_counter = Label(dialog, "compare-counter");
        time_hidden = !Named<wxToggleButton>(dialog, "compare-category-Time")->IsShown();
        Press(dialog, "compare-reject-all");
        ignored_stats = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_EQ("Difference 1 of 2", all_counter);
    EXPECT_EQ("Difference 1 of 1 (2 in total)", time_counter);
    EXPECT_EQ("1 pending, 0 accepted, 1 rejected, 0 partial", time_stats);
    EXPECT_EQ("Difference 1 of 1 (2 in total)", ignored_counter);
    EXPECT_TRUE(time_hidden);
    EXPECT_EQ("1 pending, 0 accepted, 1 rejected, 0 partial", ignored_stats);
    ASSERT_EQ(2u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("the 1 differences shown")) << hook.messages[0];
    EXPECT_NE(wxNOT_FOUND, hook.messages[1].Find("Ignored fields keep their current choice: Start, End")) << hook.messages[1];
    // Only the added line was written; the ignored time stayed as it was.
    ASSERT_EQ(2u, LiveRead(*c).Events.size());
    EXPECT_EQ(start, LiveRead(*c).Events.front().Start);
    EXPECT_EQ(end, LiveRead(*c).Events.front().End);
    EXPECT_EQ("added", std::next(LiveRead(*c).Events.begin())->Text.get());
}
TEST_F(ordinary_edit, compare_window_shows_refusals_and_batch_actions_confirm_their_scope) {
    auto path = WriteBaseline(*c, "compare-bound.ass", [](AssFile& f) {
        auto token = f.AddExtradata("aegisub.enhanced.subtitle-binding", "group");
        for (auto text : {"g1", "g2"}) {
            auto line = new AssDialogue; line->Text = text; line->Start = 5000; line->End = 6000;
            line->ExtradataIds = std::vector<uint32_t>{token}; f.Events.push_back(*line);
        }
    });
    auto texts = LiveTexts(*c);
    bool disabled = false, enabled_after_all = false, undone = false;
    wxString report;
    CompareHook hook([&](wxWindow* dialog) {
        Press(dialog, "compare-reject-current");          // only one member of the group
        report = Report(dialog);
        disabled = !dialog->FindWindow(wxID_OK)->IsEnabled();
        Press(dialog, "compare-reject-all");              // asks, the hook answers OK
        enabled_after_all = dialog->FindWindow(wxID_OK)->IsEnabled();
        Press(dialog, "compare-undo");
        undone = !dialog->FindWindow(wxID_OK)->IsEnabled();
        Press(dialog, "compare-redo");
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    EXPECT_TRUE(disabled); EXPECT_TRUE(enabled_after_all); EXPECT_TRUE(undone);
    EXPECT_NE(wxNOT_FOUND, report.Find("Bound lines must be applied together")) << report;
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("all 2 differences")) << hook.messages[0];
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("1 of them will change")) << hook.messages[0];
    EXPECT_EQ(texts, LiveTexts(*c));                       // closed by the hook: nothing written
}
TEST_F(ordinary_edit, compare_window_with_the_project_on_the_left_accepts_the_file) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-swap.ass", [](AssFile& f) {
        f.Events.front().Text = "revised";
        auto line = new AssDialogue; line->Text = "new line"; line->Start = 9000; line->End = 9500; f.Events.push_back(*line);
    });
    CompareHook hook([&](wxWindow* dialog) {
        Press(dialog, "compare-swap");
        EXPECT_NE(wxNOT_FOUND, Label(dialog, "compare-left-title").Find("target project"));
        auto all = Named<wxCheckBox>(dialog, "compare-check-all"); ASSERT_NE(nullptr, all);
        all->SetValue(true);
        wxCommandEvent event(wxEVT_CHECKBOX, all->GetId()); event.SetEventObject(all);
        all->GetEventHandler()->ProcessEvent(event);
        EXPECT_EQ("2 selected", Label(dialog, "compare-checked-count"));
        Press(dialog, "compare-accept-checked");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("the 2 selected differences")) << hook.messages[0];
    EXPECT_EQ((std::vector<std::string>{"revised", "new line"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, compare_command_refuses_while_editing_and_reports_unreadable_files) {
    CompareHook hook(nullptr);
    {
        auto scope = c->subtitleAccess->TryWrite(); ASSERT_TRUE(scope);
        cmd::call("subtitle/compare", c.get());               // refused before any file dialog
    }
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("being edited"));
    agi::fs::path bad((std::filesystem::current_path() / "compare-bad.xyz").wstring());
    { std::ofstream out(bad); out << "not subtitles"; }
    EXPECT_FALSE(RunCompare(c.get(), bad));
    ASSERT_EQ(2u, hook.messages.size());
    EXPECT_EQ(0, hook.shown);
    // Identical file: told there is nothing to apply, no window.
    auto same = WriteBaseline(*c, "compare-same.ass", [](AssFile&) {});
    EXPECT_FALSE(RunCompare(c.get(), same));
    ASSERT_EQ(3u, hook.messages.size());
    EXPECT_EQ(0, hook.shown);
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
}

TEST_F(ordinary_edit, compare_tab_locks_the_editor_until_the_comparison_ends) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-tab.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    CompareHook hook(nullptr);
    int frame_keys = 0;
    window->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& e) { ++frame_keys; e.Skip(); });
    auto key = [&](wxWindow* target, int code, int mods) {
        wxKeyEvent event(wxEVT_CHAR_HOOK); event.m_keyCode = code;
        event.SetControlDown(mods & wxMOD_CONTROL); event.SetShiftDown(mods & wxMOD_SHIFT);
        event.SetEventObject(target); target->GetEventHandler()->ProcessEvent(event);
    };
    // Keys in the editor reach the main window as before.
    key(tabs->Book()->GetChildren().front(), 'S', wxMOD_CONTROL);
    EXPECT_EQ(1, frame_keys);
    EXPECT_FALSE(Named(tabs, "workspace-strip")->IsShown());

    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    auto page = tabs->Comparison(); ASSERT_NE(nullptr, page);
    EXPECT_TRUE(tabs->ComparisonInFront());
    EXPECT_TRUE(Named(tabs, "workspace-strip")->IsShown());
    EXPECT_FALSE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_EQ("Compare: compare-tab.ass", Label(tabs, "workspace-tab-compare"));
    EXPECT_FALSE(c->subtitleAccess->TryWrite());
    // In front, the tab keeps menu and hotkey commands off the locked subtitles.
    EXPECT_FALSE(tabs->AllowsCommand("edit/undo"));
    EXPECT_NE("", Label(tabs, "workspace-notice"));
    EXPECT_TRUE(tabs->AllowsCommand("app/exit"));
    // Its own shortcuts work there, and no key goes on to the main window.
    auto pane = page->FindWindow("compare-right"); ASSERT_NE(nullptr, pane);
    Press(page, "compare-reject-current");
    EXPECT_EQ("0 pending, 0 accepted, 1 rejected, 0 partial", Label(page, "compare-stats"));
    key(pane, 'Z', wxMOD_CONTROL);
    EXPECT_EQ("1 pending, 0 accepted, 0 rejected, 0 partial", Label(page, "compare-stats"));
    key(pane, 'Y', wxMOD_CONTROL);
    EXPECT_EQ("0 pending, 0 accepted, 1 rejected, 0 partial", Label(page, "compare-stats"));
    key(pane, 'S', wxMOD_CONTROL);
    EXPECT_EQ(1, frame_keys);

    // The editor tab says it is locked, and leads back.
    auto edit = Named<WorkspaceTab>(tabs, "workspace-tab-edit"); ASSERT_NE(nullptr, edit);
    edit->Click();
    EXPECT_GT(Named(tabs, "workspace-strip")->GetPosition().y, tabs->Book()->GetPosition().y) << "the strip is below the page";
    EXPECT_FALSE(tabs->ComparisonInFront());
    EXPECT_TRUE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_NE(wxNOT_FOUND, Label(tabs, "workspace-banner-text").Find("compare-tab.ass"));
    EXPECT_TRUE(tabs->AllowsCommand("edit/undo"));     // runs, and finds the subtitles locked
    EXPECT_FALSE(c->subtitleAccess->TryWrite());
    Press(tabs, "workspace-banner-back");
    EXPECT_TRUE(tabs->ComparisonInFront());

    // Ending it from the tab strip confirms the decision it discards.
    Named<WorkspaceTab>(tabs, "workspace-tab-close")->Click();
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("discard 1 decisions")) << hook.messages[0];
    EXPECT_EQ(nullptr, tabs->Comparison());
    EXPECT_FALSE(tabs->LastApplied());
    EXPECT_FALSE(Named(tabs, "workspace-strip")->IsShown());
    EXPECT_FALSE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, compare_tab_is_one_at_a_time_and_refuses_drops_while_open) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-one.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    auto other = WriteBaseline(*c, "compare-two.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    CompareHook hook(nullptr);
    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    auto page = tabs->Comparison();
    tabs->ShowEditor();
    // A second comparison brings the open one to the front instead.
    EXPECT_FALSE(OpenCompareTab(c.get(), other));
    EXPECT_EQ(page, tabs->Comparison()); EXPECT_TRUE(tabs->ComparisonInFront());
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("already open")) << hook.messages[0];
    tabs->ShowEditor();
    cmd::call("subtitle/compare", c.get());               // no file dialog: the open tab
    EXPECT_TRUE(tabs->ComparisonInFront()); EXPECT_EQ(1u, hook.messages.size());
    // Dropped files cannot replace the subtitles while they are compared.
    tabs->ShowEditor();
    c->project->LoadList({other}, true);
    EXPECT_TRUE(tabs->ComparisonInFront());
    EXPECT_EQ(0, hook.drop_prompts);
    ASSERT_EQ(2u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[1].Find("being compared")) << hook.messages[1];
    // Closing the window without a veto ends it without asking.
    Press(page, "compare-reject-current");
    EXPECT_TRUE(tabs->CloseComparison(false));
    EXPECT_EQ(2u, hook.messages.size());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
}

TEST_F(ordinary_edit, dark_appearance_uses_dark_defaults_only_for_unchanged_colours) {
    auto restore = OPT_GET("App/Theme")->GetInt();
    theme::Init();
    int notified = 0;
    agi::signal::Connection listener = theme::AddChangeListener([&] { ++notified; });
    std::string const background = "Colour/Subtitle Grid/Background/Background";
    ASSERT_TRUE(theme::HasDarkDefault(background));
    EXPECT_FALSE(theme::HasDarkDefault("Colour/Audio Display/Play Cursor")); // drawn on the dark waveform

    OPT_SET("App/Theme")->SetInt(int(theme::Mode::Light));
    EXPECT_FALSE(theme::IsDark());
    EXPECT_EQ(wxColour(255, 255, 255), theme::Colour(background));
    OPT_SET("App/Theme")->SetInt(int(theme::Mode::Dark));
    EXPECT_TRUE(theme::IsDark());
    EXPECT_EQ(wxColour(0x1e, 0x1f, 0x22), theme::Colour(background));
    EXPECT_EQ(2, notified);                                  // each change of the setting
    // A colour the user chose is kept in either appearance.
    OPT_SET(background)->SetColor(agi::Color(10, 20, 30));
    EXPECT_EQ(wxColour(10, 20, 30), theme::Colour(background));
    OPT_SET(background)->Reset();
    EXPECT_EQ(wxColour(0x1e, 0x1f, 0x22), theme::Colour(background));
    // Options without a dark default read as they are.
    EXPECT_EQ(to_wx(OPT_GET("Colour/Audio Display/Play Cursor")->GetColor()), theme::Colour("Colour/Audio Display/Play Cursor"));
    // The tab strip and banner follow the change.
    auto path = WriteBaseline(*c, "theme-tab.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    CompareHook hook(nullptr);
    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    tabs->ShowEditor();
    EXPECT_EQ(theme::UiColours().notice_back, Named(tabs, "workspace-banner")->GetBackgroundColour());
    OPT_SET("App/Theme")->SetInt(int(theme::Mode::Light));
    EXPECT_EQ(theme::UiColours().notice_back, Named(tabs, "workspace-banner")->GetBackgroundColour());
    EXPECT_EQ(wxColour(0xff, 0xf4, 0xce), Named(tabs, "workspace-banner")->GetBackgroundColour());
    tabs->CloseComparison(false);
    OPT_SET("App/Theme")->SetInt(restore);
}

TEST_F(ordinary_edit, drawn_suggestions_offer_values_starting_with_the_typed_text) {
    auto frame=new wxPanel(window);
    auto field=new wxTextCtrl(frame,-1,"");
    std::vector<wxString> values{wxString::FromUTF8("待"),wxString::FromUTF8("待校"),wxString::FromUTF8("待定"),"Alice","alex"};
    auto suggest=new FlatSuggest(field,frame,[&]{ return values; });
    auto key=[&](int code){ wxKeyEvent e(wxEVT_CHAR_HOOK); e.m_keyCode=code; return suggest->HandleKey(e); };
    field->ChangeValue(wxString::FromUTF8("待")); suggest->Suggest();
    // Longer values only: the one already typed is not offered
    EXPECT_EQ((std::vector<wxString>{wxString::FromUTF8("待校"),wxString::FromUTF8("待定")}),suggest->Shown());
    EXPECT_EQ(0,suggest->Selected());
    EXPECT_TRUE(key(WXK_DOWN)); EXPECT_EQ(1,suggest->Selected());
    EXPECT_TRUE(key(WXK_DOWN)); EXPECT_EQ(0,suggest->Selected());   // round to the first
    EXPECT_TRUE(key(WXK_UP)); EXPECT_EQ(1,suggest->Selected());
    EXPECT_TRUE(key(WXK_RETURN));
    EXPECT_EQ(wxString::FromUTF8("待定"),field->GetValue());
    EXPECT_TRUE(suggest->Shown().empty());
    EXPECT_FALSE(key(WXK_RETURN)) << "closed: Enter goes on to the field";
    // Any case; Escape closes
    field->ChangeValue("al"); suggest->Suggest();
    EXPECT_EQ((std::vector<wxString>{"Alice","alex"}),suggest->Shown());
    EXPECT_TRUE(key(WXK_ESCAPE)); EXPECT_TRUE(suggest->Shown().empty());
    field->ChangeValue("zz"); suggest->Suggest(); EXPECT_TRUE(suggest->Shown().empty());
    frame->Destroy();
}

TEST_F(ordinary_edit, grid_colours_saved_at_a_former_default_follow_the_appearance_again) {
    std::string const header = "Colour/Subtitle Grid/Header";
    OPT_SET(header)->SetColor(agi::Color(165, 207, 231));        // the original light default, as saved in a config file
    OPT_SET("Colour/Subtitle Grid/Lines")->SetColor(agi::Color(1, 2, 3)); // the user's own
    theme::Init();
    EXPECT_TRUE(OPT_GET(header)->IsDefault());
    EXPECT_EQ(agi::Color(1, 2, 3), OPT_GET("Colour/Subtitle Grid/Lines")->GetColor());
    OPT_SET("Colour/Subtitle Grid/Lines")->Reset();
}

TEST_F(ordinary_edit, dropped_subtitles_compare_with_a_saved_document_without_replacing_it) {
    Input("current");
    auto original = WriteBaseline(*c, "drop-current.ass", [](AssFile&) {});
    c->subsController->Save(original, "utf-8");
    ASSERT_TRUE(c->subsController->HasFile()); ASSERT_FALSE(c->subsController->IsModified());
    auto path = WriteBaseline(*c, "drop-baseline.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    auto revision = c->subtitleEditGate.Revision(); auto undo = c->subsController->GetUndoDescription();
    CompareHook hook(nullptr); hook.drop_choice = wxID_YES;
    c->project->LoadList({path}, true);
    hook.Settle(*c);
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(1, hook.shown); EXPECT_EQ(0, hook.save_prompts);
    EXPECT_EQ(original, c->subsController->Filename()); EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
    EXPECT_EQ(revision, c->subtitleEditGate.Revision()); EXPECT_EQ(undo, c->subsController->GetUndoDescription());
}
TEST_F(ordinary_edit, dropped_subtitles_compare_unsaved_work_and_apply_as_one_undo_step) {
    Input("unsaved"); ASSERT_FALSE(c->subsController->HasFile());
    auto path = WriteBaseline(*c, "drop-unsaved.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    CompareHook hook([&](wxWindow* dialog) {
        EXPECT_EQ((std::vector<std::string>{"unsaved"}), LiveTexts(*c));
        Press(dialog, "compare-reject-current");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    hook.drop_choice = wxID_YES;
    c->project->LoadList({path}, true);
    hook.Settle(*c);
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(1, hook.shown); EXPECT_EQ(0, hook.save_prompts);
    EXPECT_FALSE(c->subsController->HasFile());
    EXPECT_EQ((std::vector<std::string>{"baseline"}), LiveTexts(*c));
    c->subsController->Undo(); EXPECT_EQ((std::vector<std::string>{"unsaved"}), LiveTexts(*c));
    c->subsController->Redo(); EXPECT_EQ((std::vector<std::string>{"baseline"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, cancelled_subtitle_drop_changes_neither_document_nor_other_dropped_media) {
    Input("keep"); auto revision = c->subtitleEditGate.Revision();
    auto path = WriteBaseline(*c, "drop-cancel.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    agi::fs::path timecodes((std::filesystem::current_path() / "drop-timecodes.txt").wstring());
    { std::ofstream out(timecodes); out << "# timecode format v2\n0\n40\n80\n"; }
    CompareHook hook(nullptr);
    c->project->LoadList({timecodes, path}, true);
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(0, hook.shown); EXPECT_EQ(0, hook.save_prompts);
    EXPECT_FALSE(c->project->CanCloseTimecodes()); EXPECT_FALSE(c->subsController->HasFile());
    EXPECT_EQ(revision, c->subtitleEditGate.Revision());
    EXPECT_EQ((std::vector<std::string>{"keep"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, normal_subtitle_drop_preserves_the_unsaved_changes_confirmation) {
    Input("keep");
    auto path = WriteBaseline(*c, "drop-normal.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    CompareHook hook(nullptr); hook.drop_choice = wxID_NO;
    c->project->LoadList({path}, true); // Cancel the save/discard prompt.
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(1, hook.save_prompts); EXPECT_EQ(0, hook.shown);
    EXPECT_EQ((std::vector<std::string>{"keep"}), LiveTexts(*c));
    hook.save_choice = wxID_NO;
    c->project->LoadList({path}, true); // Explicitly discard and open normally.
    EXPECT_EQ(2, hook.drop_prompts); EXPECT_EQ(2, hook.save_prompts);
    EXPECT_EQ(path, c->subsController->Filename());
    EXPECT_EQ((std::vector<std::string>{"other"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, empty_workspace_and_non_drop_opens_do_not_prompt_for_comparison) {
    c->project->CloseSubtitles();
    ASSERT_FALSE(c->subsController->HasFile()); ASSERT_FALSE(c->subsController->IsModified());
    auto path = WriteBaseline(*c, "drop-first.ass", [](AssFile& f) { f.Events.front().Text = "first"; });
    CompareHook hook(nullptr);
    c->project->LoadList({path}, true);
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_EQ(path, c->subsController->Filename());
    EXPECT_EQ((std::vector<std::string>{"first"}), LiveTexts(*c));
    auto other = WriteBaseline(*c, "drop-next.ass", [](AssFile& f) { f.Events.front().Text = "next"; });
    c->project->LoadList({other});
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_EQ(other, c->subsController->Filename());
}
TEST_F(ordinary_edit, timecode_only_and_busy_subtitle_drops_do_not_offer_comparison) {
    Input("keep");
    agi::fs::path timecodes((std::filesystem::current_path() / "drop-only-timecodes.txt").wstring());
    { std::ofstream out(timecodes); out << "# timecode format v2\n0\n40\n80\n"; }
    CompareHook hook(nullptr);
    c->project->LoadList({timecodes}, true);
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_TRUE(c->project->CanCloseTimecodes());
    auto path = WriteBaseline(*c, "drop-busy.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    auto comparison = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    c->project->LoadList({path}, true);
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_EQ(0, hook.shown);
    EXPECT_EQ((std::vector<std::string>{"keep"}), LiveTexts(*c));
    EXPECT_FALSE(hook.messages.empty());
}
TEST_F(audio_edit, main_window_blocks_sit_on_a_backdrop_without_borders) {
    // The main window's arrangement: the video and visual tools blocks, then
    // the audio block, the same gap between all of them.
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Theme")->SetInt(1); };
    OPT_SET("App/Theme")->SetInt(1);
    theme::Init();
    auto root=new wxPanel(window);root->SetSize(0,0,1400,700);
    theme::UseBackdrop(root);
    const int gap=theme::BlockGap(root);
    auto video_box=new VideoBox(root,false,c.get());
    auto audio_box=c->audioBox=new AudioBox(root,c.get());
    auto edit_box=new SubsEditBox(root,c.get());
    auto grid=new BaseGrid(root,c.get());
    auto tools=new wxBoxSizer(wxVERTICAL);tools->Add(audio_box,0,wxEXPAND);tools->Add(edit_box,1,wxEXPAND);
    auto top=new wxBoxSizer(wxHORIZONTAL);top->Add(video_box,0,wxEXPAND);top->Add(tools,1,wxEXPAND|wxLEFT,gap);
    root->SetSizer(top);
    video_box->SetToolbarGap(gap);
    VideoDisplay *display=nullptr;std::vector<wxToolBar*> bars;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto child:w->GetChildren()){
        if(auto d=dynamic_cast<VideoDisplay*>(child))display=d;
        if(auto b=dynamic_cast<wxToolBar*>(child);b&&b->HasFlag(wxTB_VERTICAL))bars.push_back(b);find(child);}};
    find(video_box);
    ASSERT_NE(nullptr,display);ASSERT_EQ(2u,bars.size());
    display->SetMinSize(wxSize(640,360));
    root->Layout();
    auto x_in_root=[&](wxWindow*w){int x=0;for(;w&&w!=root;w=w->GetParent())x+=w->GetPosition().x;return x;};
    wxWindow *video_block=display->GetParent(), *tools_block=bars[0]->GetParent();
    ASSERT_NE(video_block,tools_block);
    EXPECT_EQ(video_box,video_block->GetParent());EXPECT_EQ(video_box,tools_block->GetParent());
    int video_right=x_in_root(video_block)+video_block->GetSize().x;
    int tools_left=x_in_root(tools_block),tools_right=tools_left+tools_block->GetSize().x;
    EXPECT_EQ(gap,tools_left-video_right) << "between the video and the tools";
    EXPECT_EQ(gap,x_in_root(audio_box)-tools_right) << "between the tools and the audio";
    EXPECT_EQ(video_box->GetSize().y,tools_block->GetSize().y) << "the tools block runs the full height";

    for (int mode : {1, 2}) {
        OPT_SET("App/Theme")->SetInt(mode);
        // Blocks in the toolkit's panel colour, darker between them.
        for (wxWindow *block : {video_block, tools_block, static_cast<wxWindow*>(edit_box)})
            EXPECT_EQ(theme::Surface(), block->GetBackgroundColour()) << "mode " << mode;
        for (wxWindow *backdrop : {static_cast<wxWindow*>(root), static_cast<wxWindow*>(video_box), static_cast<wxWindow*>(audio_box)})
            EXPECT_EQ(theme::Backdrop(), backdrop->GetBackgroundColour()) << "mode " << mode;
        auto lightness=[](wxColour c){return c.Red()+c.Green()+c.Blue();};
        EXPECT_LT(lightness(theme::Backdrop()),lightness(theme::Surface()));
    }

    // No border on any block, nor on what fills them.
    AudioDisplay *waveform=nullptr;
    std::function<void(wxWindow*)> find_audio=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(auto d=dynamic_cast<AudioDisplay*>(ch))waveform=d;find_audio(ch);}};
    find_audio(audio_box);ASSERT_NE(nullptr,waveform);
    for (wxWindow *w : {static_cast<wxWindow*>(edit_box), static_cast<wxWindow*>(grid), static_cast<wxWindow*>(waveform), waveform->GetParent(), static_cast<wxWindow*>(c->karaoke)})
        EXPECT_EQ(wxBORDER_NONE, w->GetBorder()) << w->GetClassInfo()->GetClassName();
    EXPECT_FALSE(audio_box->HasFlag(wxSW_3D)) << "no raised resize strip";
    EXPECT_EQ(gap,audio_box->GetDefaultBorderSize()) << "the gap below the audio is its resize strip";
    int stc=0;
    std::function<void(wxWindow*)> editors=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(dynamic_cast<SubsTextEditCtrl*>(ch)){++stc;EXPECT_EQ(wxBORDER_NONE,ch->GetBorder());}editors(ch);}};
    editors(edit_box);EXPECT_EQ(1,stc);
}

TEST_F(audio_edit, scrollbar_thumb_edges_zoom_and_alt_wheel_zooms) {
    window->SetSize(1100,400);window->Show(); // the scrollbar captures the mouse while dragging
    auto box=c->audioBox=new AudioBox(window,c.get());
    box->SetSize(0,0,1000,260);box->Layout();wxYield();
    int sliders=0;
    std::function<void(wxWindow*)> count=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(dynamic_cast<wxSlider*>(ch))++sliders;count(ch);}};
    count(box);
    EXPECT_EQ(0,sliders) << "zoom and volume are drawn tracks; horizontal zoom lives on the scrollbar";
    AudioDisplay* display=nullptr;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(auto d=dynamic_cast<AudioDisplay*>(ch))display=d;find(ch);}};
    find(box);ASSERT_NE(nullptr,display);
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    display->SetZoomLevel(-50); // a wide thumb
    const int width=display->GetClientSize().x, bar_y=display->GetClientSize().y-5;
    const double duration=display->GetDuration();
    ASSERT_GT(width,500);ASSERT_GT(duration,display->MaxVisibleDuration()*2);
    auto mouse=[&](wxEventType type,int x,bool down) {
        wxMouseEvent event(type);event.SetLeftDown(down);event.SetPosition({x,bar_y});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    };
    auto visible=[&]{double ms=display->GetMillisecondsPerPixel();double left=display->GetScrollLeft()*ms;return std::pair{left,left+width*ms};};
    auto bar_x=[&](double t){return int(t*width/duration);};
    auto drag=[&](int from,int to){mouse(wxEVT_LEFT_DOWN,from,true);mouse(wxEVT_MOTION,(from+to)/2,true);mouse(wxEVT_MOTION,to,true);mouse(wxEVT_LEFT_UP,to,false);};
    const double bar_ms=duration/width; // time one pixel of the bar stands for

    // Scroll to the middle by dragging the thumb's middle.
    auto [l0,r0]=visible();
    ASSERT_GT(bar_x(r0)-bar_x(l0),60);
    drag((bar_x(l0)+bar_x(r0))/2,width/2);
    auto [l1,r1]=visible();
    EXPECT_GT(l1,l0);EXPECT_NEAR(r0-l0,r1-l1,1) << "moving does not zoom";
    EXPECT_NEAR(width/2,(bar_x(l1)+bar_x(r1))/2,2);

    // Right edge inwards: the left edge stays, less time is shown.
    drag(bar_x(r1),bar_x(r1)-40);
    auto [l2,r2]=visible();
    EXPECT_NEAR(l1,l2,display->GetMillisecondsPerPixel()+1);
    EXPECT_NEAR(r2,(bar_x(r1)-40)*bar_ms,bar_ms+display->GetMillisecondsPerPixel());
    EXPECT_GT(display->GetZoomLevel(),-50);
    EXPECT_EQ(AudioDisplay::NearestZoomLevel(display->GetMillisecondsPerPixel()),display->GetZoomLevel());
    EXPECT_EQ(display->GetZoomLevel(),OPT_GET("Audio/Zoom/Horizontal")->GetInt()) << "the zoom is kept";

    // Left edge outwards: the right edge stays, more time is shown.
    drag(bar_x(l2),bar_x(l2)-20);
    auto [l3,r3]=visible();
    EXPECT_NEAR(r2,r3,2*display->GetMillisecondsPerPixel()+1);
    EXPECT_GT(r3-l3,r2-l2);
    EXPECT_LT(l3,l2);

    // The span stops at the deepest zoom instead of crossing over...
    drag(bar_x(l3),bar_x(r3)+30);
    auto [l4,r4]=visible();
    EXPECT_NEAR(display->MinVisibleDuration(),r4-l4,display->GetMillisecondsPerPixel()+1);
    EXPECT_NEAR(r3,r4,2*display->GetMillisecondsPerPixel()+1);
    EXPECT_EQ(30,display->GetZoomLevel());
    // ...and at the widest.
    display->SetZoomLevel(-30);
    auto [l5,r5]=visible();
    drag(bar_x(r5),width-1);
    auto [l6,r6]=visible();
    EXPECT_NEAR(display->MaxVisibleDuration(),r6-l6,display->GetMillisecondsPerPixel()+1);
    EXPECT_NEAR(l5,l6,display->GetMillisecondsPerPixel()+1);

    // Alt + wheel zooms whatever the wheel's default is.
    for(bool default_zoom:{false,true}){
        OPT_SET("Audio/Wheel Default to Zoom")->SetBool(default_zoom);
        display->SetZoomLevel(0);
        wxMouseEvent wheel(wxEVT_MOUSEWHEEL);wheel.SetAltDown(true);wheel.m_wheelRotation=120;wheel.m_wheelDelta=120;
        wheel.SetPosition({width/2,80});wheel.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(wheel);
        EXPECT_EQ(1,display->GetZoomLevel()) << "wheel defaults to zoom: " << default_zoom;
        EXPECT_EQ(1,OPT_GET("Audio/Zoom/Horizontal")->GetInt());
    }
}
TEST_F(audio_edit, alt_wheel_zoom_does_not_leave_alt_to_the_menu_bar) {
    // After Alt + wheel, releasing Alt must not select the menu bar (Windows),
    // which would keep later turns counting as Alt + wheel; Alt alone later
    // still does.
    ResetAltUsedForWheel();
    EXPECT_FALSE(TakeAltUsedForWheel());
    NoteAltUsedForWheel();
    EXPECT_TRUE(TakeAltUsedForWheel()) << "the release right after zooming";
    EXPECT_FALSE(TakeAltUsedForWheel()) << "only once";
    NoteAltUsedForWheel();ResetAltUsedForWheel();
    EXPECT_FALSE(TakeAltUsedForWheel()) << "a turn without Alt forgets it";

    // The wheel itself: Alt zooms, a plain turn right after scrolls.
    auto box=c->audioBox=new AudioBox(window,c.get());
    box->SetSize(0,0,1000,260);box->Layout();
    AudioDisplay* display=nullptr;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(auto d=dynamic_cast<AudioDisplay*>(ch))display=d;find(ch);}};
    find(box);ASSERT_NE(nullptr,display);
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    OPT_SET("Audio/Wheel Default to Zoom")->SetBool(false);
    display->SetZoomLevel(0);display->ScrollPixelToLeft(2000);
    auto wheel=[&](bool alt){
        wxMouseEvent event(wxEVT_MOUSEWHEEL);event.SetAltDown(alt);event.m_wheelRotation=-120;event.m_wheelDelta=120;
        event.SetPosition({400,80});event.SetEventObject(display);display->GetEventHandler()->ProcessEvent(event);
    };
    wheel(true);
    EXPECT_EQ(-1,display->GetZoomLevel());
    int scroll=display->GetScrollLeft();
    wheel(false);
    EXPECT_EQ(-1,display->GetZoomLevel()) << "no more zooming once Alt is up";
    EXPECT_NE(scroll,display->GetScrollLeft()) << "it scrolls";
    EXPECT_FALSE(TakeAltUsedForWheel());
}

TEST_F(audio_edit, level_tracks_set_zoom_and_volume_with_link_and_reset) {
    window->SetSize(1100,400);window->Show(); // the tracks capture the mouse while dragging
    auto box=c->audioBox=new AudioBox(window,c.get());
    box->SetSize(0,0,1000,300);box->Layout();wxYield();
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    OPT_SET("Audio/Link")->SetBool(false);
    box->ResetLevels();
    AudioLevels *levels=box->Levels();
    ASSERT_NE(nullptr,levels);
    ASSERT_GT(levels->GetClientSize().y,120);
    auto mouse=[&](wxEventType type,wxPoint p,bool down,bool shift=false){
        wxMouseEvent e(type);e.SetLeftDown(down);e.SetShiftDown(shift);e.SetPosition(p);e.SetEventObject(levels);
        levels->GetEventHandler()->ProcessEvent(e);
    };
    using Part=AudioLevels::Part;
    auto zoom_handle=[&]{return levels->HandleAt(Part::Zoom,levels->GetZoom());};
    auto volume_handle=[&]{return levels->HandleAt(Part::Volume,levels->GetVolume());};
    EXPECT_EQ(Part::Zoom,levels->HitTest(zoom_handle()));
    EXPECT_EQ(Part::Volume,levels->HitTest(volume_handle()));
    EXPECT_LT(zoom_handle().x,volume_handle().x) << "zoom on the left, volume on the right";

    // Drag the zoom handle up: zoom grows, the option and the waveform follow.
    wxPoint start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);
    mouse(wxEVT_MOTION,start-wxPoint(0,30),true);
    mouse(wxEVT_LEFT_UP,start-wxPoint(0,30),false);
    int zoom=levels->GetZoom();
    EXPECT_GT(zoom,AudioLevels::neutral_position);
    EXPECT_EQ(zoom,OPT_GET("Audio/Zoom/Vertical")->GetInt());
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetVolume()) << "not linked: the volume stays";

    // Shift drags a quarter as fast.
    start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);
    mouse(wxEVT_MOTION,start+wxPoint(0,40),true,true);
    mouse(wxEVT_LEFT_UP,start+wxPoint(0,40),false,true);
    int fine=zoom-levels->GetZoom();
    start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);
    mouse(wxEVT_MOTION,start+wxPoint(0,40),true);
    mouse(wxEVT_LEFT_UP,start+wxPoint(0,40),false);
    int coarse=zoom-fine-levels->GetZoom();
    EXPECT_GT(fine,0);EXPECT_NEAR(coarse,fine*4,4);

    // Volume: a click on its track jumps there; double-click back to neutral.
    wxRect column(levels->HandleAt(Part::Volume,AudioLevels::max_position),levels->HandleAt(Part::Volume,AudioLevels::min_position));
    mouse(wxEVT_LEFT_DOWN,wxPoint(column.x,column.y+5),true);
    mouse(wxEVT_LEFT_UP,wxPoint(column.x,column.y+5),false);
    EXPECT_GT(levels->GetVolume(),90);
    EXPECT_EQ(levels->GetVolume(),OPT_GET("Audio/Volume")->GetInt());
    mouse(wxEVT_LEFT_DCLICK,volume_handle(),false);
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetVolume());

    // Wheel: one step per notch.
    int before=levels->GetVolume();
    wxMouseEvent wheel(wxEVT_MOUSEWHEEL);wheel.m_wheelRotation=240;wheel.m_wheelDelta=120;wheel.SetPosition(volume_handle());wheel.SetEventObject(levels);
    levels->GetEventHandler()->ProcessEvent(wheel);
    EXPECT_EQ(before+2,levels->GetVolume());

    // The link button, left in the bottom row: one track sets both.
    wxSize size=levels->GetClientSize();
    wxPoint link(size.x/4,size.y-5), reset(size.x*3/4,size.y-5);
    EXPECT_EQ(Part::Link,levels->HitTest(link));
    EXPECT_EQ(Part::Reset,levels->HitTest(reset));
    EXPECT_LT(link.x,reset.x) << "reset to the right of link";
    mouse(wxEVT_LEFT_DOWN,link,true);mouse(wxEVT_LEFT_UP,link,false);
    EXPECT_TRUE(OPT_GET("Audio/Link")->GetBool());
    EXPECT_TRUE(levels->IsLinked());
    EXPECT_EQ(levels->GetZoom(),levels->GetVolume()) << "linking takes the zoom's value";
    EXPECT_EQ(Part::Zoom,levels->HitTest(wxPoint(size.x*3/4,zoom_handle().y))) << "one track across";
    start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);mouse(wxEVT_MOTION,start-wxPoint(0,10),true);mouse(wxEVT_LEFT_UP,start-wxPoint(0,10),false);
    EXPECT_EQ(levels->GetZoom(),levels->GetVolume());
    EXPECT_EQ(levels->GetZoom(),OPT_GET("Audio/Volume")->GetInt());

    // Reset, right of link: both neutral, still linked.
    mouse(wxEVT_LEFT_DOWN,reset,true);mouse(wxEVT_LEFT_UP,reset,false);
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetZoom());
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetVolume());
    EXPECT_EQ(AudioLevels::neutral_position,OPT_GET("Audio/Zoom/Vertical")->GetInt());
    EXPECT_EQ(AudioLevels::neutral_position,OPT_GET("Audio/Volume")->GetInt());
    EXPECT_TRUE(OPT_GET("Audio/Link")->GetBool());
    ASSERT_NE(nullptr,cmd::get("audio/opt/reset_levels")) << "also in the command palette";
    OPT_SET("Audio/Link")->SetBool(false);
    EXPECT_FALSE(levels->IsLinked());

    // Texts below the tracks.
    EXPECT_EQ(wxString::FromUTF8("1.0\u00d7"),AudioLevels::ZoomText(50));
    EXPECT_EQ(wxString::FromUTF8("8.0\u00d7"),AudioLevels::ZoomText(100));
    EXPECT_EQ("0dB",AudioLevels::VolumeText(50));
    EXPECT_EQ("+18dB",AudioLevels::VolumeText(100));
    EXPECT_EQ("-18dB",AudioLevels::VolumeText(25));
}
