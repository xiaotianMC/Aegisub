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
#include "subtitle_row_list_edit.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "timeedit_ctrl.h"
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
#include "workspace_tabs.h"
#include "dialog_manager.h"
#include "compat.h"
#include "utils.h"
#include "audio_controller.h"
#include "audio_timing.h"
#include "audio_display.h"
#include "audio_box.h"
#include "audio_karaoke.h"
#include "audio_edit_session.h"
#include "ass_karaoke.h"
#include "project.h"
#include "video_display.h"
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
#include <wx/panel.h>
#include <fstream>
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
    for(auto const& row:LiveRead(*c).Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    start->SetTime(1002); send(start,0); send(layer,8); send(actor,0,"blocked");
    for(auto const& row:LiveRead(*c).Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
}
TEST_F(ordinary_edit, grid_number_cell_direct_event_is_gated) {
    auto grid=new BaseGrid(window,c.get()); c->subsGrid=grid;
    { auto write=c->subtitleAccess->TryWrite(); LiveWriteForTest(*c).Commit("",AssFile::COMMIT_NEW); }
    wxFont font=*wxNORMAL_FONT; font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());
    auto face=FontFace("Subtitle/Grid"); if(!face.empty())font.SetFaceName(face);
    wxClientDC dc(grid); dc.SetFont(font);
    auto click=[&] {
        wxMouseEvent event(wxEVT_LEFT_DOWN); event.SetPosition({1,dc.GetCharHeight()+6});
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
        auto zoom=new wxComboBox(window,wxID_ANY);
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

TEST_F(visual_edit, drawing_dialog_preview_confirm_cancel_and_undo_use_independent_data) {
    Install<VisualToolRectangle>();Drag(30,40,120,150);auto text=Text();
    {RunModal edit("Edit Shape",[&](wxDialog* dialog){
        EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
        wxSpinCtrlDouble* width=nullptr;for(auto child:descendants(dialog))if((width=dynamic_cast<wxSpinCtrlDouble*>(child)))break;
        ASSERT_NE(nullptr,width);width->SetValue(width->GetValue()+20);
        wxSpinDoubleEvent event(wxEVT_SPINCTRLDOUBLE,width->GetId());event.SetEventObject(width);width->GetEventHandler()->ProcessEvent(event);
        EXPECT_EQ(text,Text());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
        button(dialog,wxID_OK);
    });
    EditDrawing(c.get());}EXPECT_NE(text,Text());auto changed=Text();
    c->subsController->Undo();EXPECT_EQ(text,Text());c->subsController->Redo();EXPECT_EQ(changed,Text());
    RunModal cancel("Edit Shape",[&](wxDialog* dialog){
        wxSpinCtrlDouble* width=nullptr;for(auto child:descendants(dialog))if((width=dynamic_cast<wxSpinCtrlDouble*>(child)))break;
        ASSERT_NE(nullptr,width);width->SetValue(width->GetValue()+30);
        wxCommandEvent event(wxEVT_TEXT,width->GetId());width->GetEventHandler()->ProcessEvent(event);
        EXPECT_EQ(changed,Text());dialog->EndModal(wxID_CANCEL);
    });
    EditDrawing(c.get());EXPECT_EQ(changed,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, drawing_dialog_rejects_changed_target_and_comparison_authorization) {
    Install<VisualToolRectangle>();Drag(30,40,120,150);auto text=Text();AcceptMessages accept;
    {RunModal changed("Edit Shape",[&](wxDialog* dialog){
        {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Layer=9;}
        button(dialog,wxID_OK);EXPECT_EQ(text,Text());EXPECT_TRUE(dialog->IsModal());
    });
    EditDrawing(c.get());}EXPECT_EQ(9,LiveRead(*c).Events.front().Layer);EXPECT_EQ(text,Text());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    RunModal blocked("Edit Shape",[&](wxDialog* dialog){button(dialog,wxID_OK);EXPECT_TRUE(dialog->IsModal());});
    EditDrawing(c.get());EXPECT_EQ(text,Text());
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
        wxFont font=*wxNORMAL_FONT;font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());
        auto face=FontFace("Subtitle/Grid");if(!face.empty())font.SetFaceName(face);
        wxClientDC dc(grid);dc.SetFont(font);height=dc.GetCharHeight()+4;
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
    auto edit = Named<wxToggleButton>(tabs, "workspace-tab-edit"); ASSERT_NE(nullptr, edit);
    wxCommandEvent toggle(wxEVT_TOGGLEBUTTON, edit->GetId()); toggle.SetEventObject(edit);
    edit->GetEventHandler()->ProcessEvent(toggle);
    EXPECT_FALSE(tabs->ComparisonInFront());
    EXPECT_TRUE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_NE(wxNOT_FOUND, Label(tabs, "workspace-banner-text").Find("compare-tab.ass"));
    EXPECT_TRUE(tabs->AllowsCommand("edit/undo"));     // runs, and finds the subtitles locked
    EXPECT_FALSE(c->subtitleAccess->TryWrite());
    Press(tabs, "workspace-banner-back");
    EXPECT_TRUE(tabs->ComparisonInFront());

    // Ending it from the tab strip confirms the decision it discards.
    Press(tabs, "workspace-tab-close");
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
