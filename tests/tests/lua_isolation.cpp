// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
//
// C1-C1c-9: Lua macros run through the production LuaScript, LuaCommand,
// DialogProgress worker thread, SubsController history and SelectionController.
// The `lua_legacy` tests characterise the behaviour that must survive the
// migration to an isolated work model; `lua_isolation` tests assert the new
// transactional guarantees.
#include <gtest/gtest.h>
#include "include/aegisub/context.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "auto4_base.h"
#include "auto4_lua_factory.h"
#include "command/command.h"
#include "compat.h"
#include "options.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subtitle_project_access.h"
#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/modalhook.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <chrono>
#include <cstdio>
#include <utility>
#include "ass_exporter.h"
#include "subtitle_format.h"
#include "auto4_lua.h"

namespace {
void register_lua() {
    static bool registered = false;
    if (registered) return;
    // run_isolated.py copies automation/include next to the test root; the
    // production Lua loader looks for it under ?data.
    auto source = std::filesystem::current_path() / "automation" / "include";
    auto target = std::filesystem::path(config::path->Decode("?data/automation/include").wstring());
    std::filesystem::create_directories(target);
    std::filesystem::copy(source, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    Automation4::ScriptFactory::Register(std::make_unique<Automation4::LuaScriptFactory>());
    registered = true;
}

std::vector<wxWindow*> all_children(wxWindow* parent) {
    std::vector<wxWindow*> found;
    for (auto child : parent->GetChildren()) {
        found.push_back(child);
        auto nested = all_children(child);
        found.insert(found.end(), nested.begin(), nested.end());
    }
    return found;
}

// Drives the real DialogProgress modal loop: optionally presses Cancel as soon
// as the dialog runs, and closes a dialog left open for its log output. Polls
// with a timer so it does not depend on idle-event delivery.
class ProgressDriver : public wxModalDialogHook, public wxTimer {
    wxDialog* dialog = nullptr;
public:
    bool press_cancel = false;
    int seen = 0;
    /// Runs on the host thread when the progress dialog starts, while the
    /// script is (or is about to be) running on its worker thread.
    std::function<void()> on_enter;
    ProgressDriver() { Register(); }
    ~ProgressDriver() { Stop(); }
    int Enter(wxDialog* d) override {
        ++seen;
        dialog = d;
        if (auto action = std::exchange(on_enter, nullptr)) action();
        Start(10);
        return wxID_NONE;
    }
    void Exit(wxDialog* d) override { if (d == dialog) { Stop(); dialog = nullptr; } }
    void Notify() override {
        if (!dialog || !dialog->IsModal()) return;
        for (auto child : all_children(dialog)) {
            auto button = dynamic_cast<wxButton*>(child);
            if (!button || button->GetId() != wxID_CANCEL) continue;
            if (press_cancel) {
                press_cancel = false;
                wxCommandEvent click(wxEVT_BUTTON, wxID_CANCEL);
                click.SetEventObject(button);
                dialog->GetEventHandler()->ProcessEvent(click);
            }
            else if (button->GetLabel() == _("Close"))
                dialog->EndModal(wxID_CANCEL);
            return;
        }
    }
};

class lua_macro : public testing::Test {
protected:
    std::unique_ptr<agi::Context> c;
    wxFrame* window = nullptr;
    std::vector<std::unique_ptr<Automation4::Script>> scripts;
    ProgressDriver progress;
    wxString base_undo;
    bool base_modified = false, base_undo_empty = false;
    void ExpectHistoryUnchanged() {
        EXPECT_EQ(base_undo, c->subsController->GetUndoDescription());
        EXPECT_EQ(base_modified, c->subsController->IsModified());
        EXPECT_EQ(base_undo_empty, c->subsController->IsUndoStackEmpty());
    }

    void SetUp() override {
        register_lua();
        c = std::make_unique<agi::Context>();
        window = new wxFrame(nullptr, wxID_ANY, "lua isolation tests");
        c->parent = window;
        auto write = c->subtitleAccess->TryWrite();
        c->ass->LoadDefault();
        c->ass->Events.front().Text = "first";
        auto second = new AssDialogue; second->Text = "second"; c->ass->Events.push_back(*second);
        auto third = new AssDialogue; third->Text = "third"; c->ass->Events.push_back(*third);
        c->ass->Commit("fixture", AssFile::COMMIT_NEW);
        c->selectionController->SetSelectionAndActiveIDs({Id(0)}, Id(0));
        base_undo = c->subsController->GetUndoDescription();
        base_modified = c->subsController->IsModified();
        base_undo_empty = c->subsController->IsUndoStackEmpty();
    }
    void TearDown() override {
        scripts.clear();
        delete window;
        c.reset();
    }

    int Id(size_t row) const {
        auto it = c->ass->Events.begin();
        std::advance(it, row);
        return it->Id;
    }
    std::vector<std::string> Texts() const {
        std::vector<std::string> out;
        for (auto const& line : c->ass->Events) out.push_back(line.Text.get());
        return out;
    }
    std::vector<int> Ids() const {
        std::vector<int> out;
        for (auto const& line : c->ass->Events) out.push_back(line.Id);
        return out;
    }

    /// Write a script with the given body, load it with the production factory
    /// and return the registered macro.
    cmd::Command* Load(std::string const& stem, std::string const& body) {
        auto path = std::filesystem::current_path() / (stem + ".lua");
        std::ofstream(path) << "script_name = \"" << stem << "\"\n" << body;
        auto script = Automation4::ScriptFactory::CreateFromFile(agi::fs::path(path.wstring()), false);
        EXPECT_TRUE(script && script->GetLoadedState()) << (script ? script->GetDescription() : "no script");
        auto macros = script->GetMacros();
        EXPECT_EQ(1u, macros.size());
        scripts.push_back(std::move(script));
        return macros.empty() ? nullptr : macros.front();
    }
    void Run(cmd::Command* macro) { (*macro)(c.get()); }
};

// Line index helper for scripts: first dialogue line index in `subs`.
constexpr const char* first_dialogue = R"(
local function first_dialogue(subs)
    for i = 1, #subs do if subs[i].class == "dialogue" then return i end end
end
)";
}

TEST_F(lua_macro, legacy_multiple_undo_points_produce_one_history_step_each) {
    auto macro = Load("multi_undo", std::string(first_dialogue) + R"(
aegisub.register_macro("Multi undo", "", function(subs, sel, act)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "a"; subs[i] = l
    aegisub.set_undo_point("step one")
    aegisub.set_undo_point("duplicate without change")
    l = subs[i]; l.text = "b"; subs[i] = l
    aegisub.set_undo_point("step two")
    l = subs[i + 1]; l.text = "c"; subs[i + 1] = l
end)
)");
    ASSERT_TRUE(macro);
    auto before = c->subsController->GetUndoDescription();
    Run(macro);
    EXPECT_EQ((std::vector<std::string>{"b", "c", "third"}), Texts());
    EXPECT_EQ("Multi undo", c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subsController->IsModified());

    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"b", "second", "third"}), Texts());
    EXPECT_EQ("step two", c->subsController->GetUndoDescription());
    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"a", "second", "third"}), Texts());
    EXPECT_EQ("step one", c->subsController->GetUndoDescription());
    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    EXPECT_EQ(before, c->subsController->GetUndoDescription());

    c->subsController->Redo(); EXPECT_EQ("a", Texts()[0]);
    c->subsController->Redo(); EXPECT_EQ("b", Texts()[0]);
    c->subsController->Redo(); EXPECT_EQ((std::vector<std::string>{"b", "c", "third"}), Texts());
    EXPECT_TRUE(c->subsController->IsRedoStackEmpty());
}

TEST_F(lua_macro, legacy_empty_undo_point_description_merges_into_next_step) {
    auto macro = Load("empty_undo", std::string(first_dialogue) + R"(
aegisub.register_macro("Empty undo", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "x"; subs[i] = l
    aegisub.set_undo_point("")
    l = subs[i + 1]; l.text = "y"; subs[i + 1] = l
end)
)");
    ASSERT_TRUE(macro);
    Run(macro);
    EXPECT_EQ((std::vector<std::string>{"x", "y", "third"}), Texts());
    EXPECT_EQ("Empty undo", c->subsController->GetUndoDescription());
    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    ExpectHistoryUnchanged();
}

TEST_F(lua_macro, legacy_read_only_macro_creates_no_history) {
    auto macro = Load("read_only", R"(
aegisub.register_macro("Read only", "", function(subs, sel, act)
    local n = 0
    for i, l in ipairs(subs) do if l.class == "dialogue" then n = n + 1 end end
    assert(n == 3)
end)
)");
    ASSERT_TRUE(macro);
    auto ids = Ids();
    Run(macro);
    EXPECT_EQ(ids, Ids());
    ExpectHistoryUnchanged();
    EXPECT_EQ(Id(0), c->selectionController->ActiveID());
}

TEST_F(lua_macro, legacy_replaced_lines_get_new_ids_and_untouched_lines_keep_theirs) {
    auto macro = Load("ids", std::string(first_dialogue) + R"(
aegisub.register_macro("Ids", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i + 1]; l.text = "replaced"; subs[i + 1] = l
    local n = subs[i]; n.text = "inserted"; subs.insert(i, n)
    subs.delete(i + 3)
end)
)");
    ASSERT_TRUE(macro);
    auto ids = Ids();
    Run(macro);
    EXPECT_EQ((std::vector<std::string>{"inserted", "first", "replaced"}), Texts());
    auto after = Ids();
    EXPECT_EQ(ids[0], after[1]);
    EXPECT_NE(ids[1], after[2]);
    EXPECT_GT(after[2], ids[2]);
    EXPECT_GT(after[0], ids[2]);
    EXPECT_EQ(3u, std::set<int>(after.begin(), after.end()).size());
    c->subsController->Undo();
    EXPECT_EQ(ids, Ids());
}

TEST_F(lua_macro, legacy_returned_selection_and_active_row_are_installed_by_id) {
    auto macro = Load("selection", std::string(first_dialogue) + R"(
aegisub.register_macro("Selection", "", function(subs, sel, act)
    local i = first_dialogue(subs)
    assert(#sel == 1 and sel[1] == i and act == i)
    return {i + 1, i + 2}, i + 2
end)
)");
    ASSERT_TRUE(macro);
    Run(macro);
    EXPECT_EQ((std::set<int>{Id(1), Id(2)}), c->selectionController->SelectedIDs());
    EXPECT_EQ(Id(2), c->selectionController->ActiveID());
}

TEST_F(lua_macro, legacy_script_info_styles_and_extradata_round_trip_through_history) {
    auto macro = Load("sections", std::string(first_dialogue) + R"(
aegisub.register_macro("Sections", "", function(subs)
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "info" and l.key == "Title" then l.value = "lua title"; subs[i] = l end
        if l.class == "style" then
            local s = l; s.name = "Lua Style"; s.fontsize = 42; subs.append(s)
            break
        end
    end
    local i = first_dialogue(subs)
    local l = subs[i]; l.style = "Lua Style"; l.extra = {["lua.key"] = "lua value"}; subs[i] = l
end)
)");
    ASSERT_TRUE(macro);
    { auto write = c->subtitleAccess->TryWrite(); c->ass->SetScriptInfo("Title", "orig"); c->ass->Commit("title", AssFile::COMMIT_SCRIPTINFO); }
    auto extradata = c->ass->Extradata.size();
    Run(macro);
    EXPECT_EQ("lua title", c->ass->GetScriptInfo("Title"));
    ASSERT_NE(nullptr, c->ass->GetStyle("Lua Style"));
    EXPECT_EQ(42, c->ass->GetStyle("Lua Style")->fontsize);
    auto const& line = c->ass->Events.front();
    EXPECT_EQ("Lua Style", line.Style.get());
    auto entries = c->ass->GetExtradata(line.ExtradataIds.get());
    ASSERT_EQ(1u, entries.size());
    EXPECT_EQ("lua.key", entries[0].key); EXPECT_EQ("lua value", entries[0].value);
    EXPECT_EQ(extradata + 1, c->ass->Extradata.size());

    c->subsController->Undo();
    EXPECT_EQ("orig", c->ass->GetScriptInfo("Title"));
    EXPECT_EQ(nullptr, c->ass->GetStyle("Lua Style"));
    EXPECT_TRUE(c->ass->Events.front().ExtradataIds.get().empty());
    c->subsController->Redo();
    EXPECT_EQ("lua title", c->ass->GetScriptInfo("Title"));
    EXPECT_EQ(1u, c->ass->GetExtradata(c->ass->Events.front().ExtradataIds.get()).size());
}

TEST_F(lua_macro, legacy_runtime_error_and_cancel_leave_text_and_history_unchanged) {
    auto failing = Load("failing", std::string(first_dialogue) + R"(
aegisub.register_macro("Failing", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "partial"; subs[i] = l
    aegisub.set_undo_point("never")
    error("boom")
end)
)");
    auto cancelling = Load("cancelling", std::string(first_dialogue) + R"(
aegisub.register_macro("Cancelling", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "partial"; subs[i] = l
    aegisub.cancel()
end)
)");
    ASSERT_TRUE(failing && cancelling);
    auto ids = Ids();
    Run(failing);
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    EXPECT_EQ(ids, Ids());
    ExpectHistoryUnchanged();
    Run(cancelling);
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    ExpectHistoryUnchanged();
}

TEST_F(lua_macro, legacy_stored_subtitle_object_is_rejected_on_later_runs) {
    auto macro = Load("stale", R"(
local saved
aegisub.register_macro("Stale", "", function(subs)
    if saved then
        local ok, err = pcall(function() return saved[1] end)
        assert(not ok and tostring(err):find("no longer valid"), tostring(err))
        local l = subs[1]; l.value = "second run"; subs[1] = l
    end
    saved = subs
end)
)");
    ASSERT_TRUE(macro);
    Run(macro);
    ExpectHistoryUnchanged();
    Run(macro);
    EXPECT_EQ("Stale", c->subsController->GetUndoDescription());
}

// ---- New transactional guarantees (fail on the pre-migration implementation) ----

TEST_F(lua_macro, isolation_failed_or_cancelled_run_leaves_no_extradata_behind) {
    auto failing = Load("leak", std::string(first_dialogue) + R"(
aegisub.register_macro("Leak", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.extra = {["leak.key"] = "leak value"}; subs[i] = l
    error("boom")
end)
)");
    ASSERT_TRUE(failing);
    auto extradata = c->ass->Extradata.size();
    auto next = c->ass->next_extradata_id;
    Run(failing);
    EXPECT_EQ(extradata, c->ass->Extradata.size());
    EXPECT_EQ(next, c->ass->next_extradata_id);
}

TEST_F(lua_macro, isolation_comparison_session_rejects_the_whole_result_without_partial_write) {
    auto macro = Load("blocked", std::string(first_dialogue) + R"(
aegisub.register_macro("Blocked", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "one"; subs[i] = l
    aegisub.set_undo_point("one")
    l = subs[i]; l.text = "two"; subs[i] = l
end)
)");
    ASSERT_TRUE(macro);
    auto ids = Ids();
    auto revision = c->subtitleEditGate.Revision();
    auto session = c->subtitleEditGate.TryBeginComparison();
    ASSERT_TRUE(session);
    try { Run(macro); } catch (...) {}
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    EXPECT_EQ(ids, Ids());
    ExpectHistoryUnchanged();
    EXPECT_EQ(revision.document_epoch, c->subtitleEditGate.Revision().document_epoch);
    session.reset();
}

TEST_F(lua_macro, isolation_validate_parse_karaoke_does_not_write_live_extradata) {
    auto macro = Load("validate", R"(
aegisub.register_macro("Validate", "", function() end, function(subs)
    for i = 1, #subs do
        if subs[i].class == "dialogue" then
            local l = subs[i]; l.extra = {["validate.key"] = "validate value"}
            aegisub.parse_karaoke_data(l)
            return true
        end
    end
    return true
end)
)");
    ASSERT_TRUE(macro);
    auto extradata = c->ass->Extradata.size();
    EXPECT_TRUE(macro->Validate(c.get()));
    EXPECT_EQ(extradata, c->ass->Extradata.size());
}

TEST_F(lua_macro, isolation_undo_point_before_first_script_info_edit_keeps_script_info) {
    auto macro = Load("info_probe", std::string(first_dialogue) + R"(
aegisub.register_macro("Info probe", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "s1"; subs[i] = l
    aegisub.set_undo_point("s1")
    for j = 1, #subs do
        local e = subs[j]
        if e.class == "info" and e.key == "Title" then e.value = "changed"; subs[j] = e end
    end
end)
)");
    ASSERT_TRUE(macro);
    auto info = c->ass->Info.size();
    Run(macro);
    EXPECT_EQ(info, c->ass->Info.size());
    c->subsController->Undo();
    EXPECT_EQ("s1", Texts()[0]);
    EXPECT_EQ(info, c->ass->Info.size()) << "checkpoint s1 lost Script Info";
}

namespace {
constexpr const char* rewrite_first = R"(
aegisub.register_macro("Rewrite", "", function(subs)
    for i = 1, #subs do
        if subs[i].class == "dialogue" then
            local l = subs[i]; l.text = "rewritten"; subs[i] = l
            aegisub.set_undo_point("rewrite")
            l = subs[i]; l.text = "rewritten twice"; subs[i] = l
            return
        end
    end
end)
)";
}

TEST_F(lua_macro, isolation_edit_during_run_makes_the_result_stale) {
    auto macro = Load("stale_edit", rewrite_first);
    ASSERT_TRUE(macro);
    progress.on_enter = [&] {
        auto write = c->subtitleAccess->TryWrite(); ASSERT_TRUE(write);
        c->subtitleAccess->Edit(write->Lease()).SetDialogueField(Id(1), &AssDialogueBase::Text, std::string("host edit"));
        c->subtitleAccess->Edit(write->Lease()).Commit("host edit", AssFile::COMMIT_DIAG_TEXT);
    };
    auto ids = Ids();
    Run(macro);
    EXPECT_EQ(1, progress.seen);
    EXPECT_EQ((std::vector<std::string>{"first", "host edit", "third"}), Texts());
    EXPECT_EQ(ids, Ids());
    EXPECT_EQ("host edit", c->subsController->GetUndoDescription());
    c->subsController->Undo();
    ExpectHistoryUnchanged();
}

TEST_F(lua_macro, isolation_comparison_started_during_run_rejects_publication) {
    auto macro = Load("stale_session", rewrite_first);
    ASSERT_TRUE(macro);
    std::optional<agi::SubtitleEditGate::ComparisonSession> session;
    progress.on_enter = [&] { session = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(session); };
    auto ids = Ids();
    auto revision = c->subtitleEditGate.Revision();
    Run(macro);
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    EXPECT_EQ(ids, Ids());
    EXPECT_EQ(revision, c->subtitleEditGate.Revision());
    ExpectHistoryUnchanged();
    session.reset();
    // Nothing was left registered: a normal run works afterwards.
    Run(macro);
    EXPECT_EQ("rewritten twice", Texts()[0]);
}

TEST_F(lua_macro, isolation_close_during_run_keeps_old_result_out_of_the_new_document) {
    auto macro = Load("stale_close", rewrite_first);
    ASSERT_TRUE(macro);
    progress.on_enter = [&] { c->subsController->Close(); };
    auto epoch = c->subtitleEditGate.Revision().document_epoch;
    Run(macro);
    EXPECT_EQ(epoch + 1, c->subtitleEditGate.Revision().document_epoch);
    ASSERT_EQ(1u, c->ass->Events.size());
    EXPECT_EQ("", Texts()[0]);
    EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    EXPECT_FALSE(c->subsController->IsModified());
}

TEST_F(lua_macro, isolation_user_cancel_after_completion_discards_the_result) {
    // The script makes its changes, waits until the user presses Cancel, then
    // returns normally: the completed result must still be discarded.
    auto macro = Load("user_cancel", std::string(first_dialogue) + R"(
aegisub.register_macro("User cancel", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "made before cancel"; subs[i] = l
    aegisub.set_undo_point("before cancel")
    while not aegisub.progress.is_cancelled() do end
end)
)");
    ASSERT_TRUE(macro);
    progress.press_cancel = true;
    auto ids = Ids();
    Run(macro);
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    EXPECT_EQ(ids, Ids());
    ExpectHistoryUnchanged();
}

TEST_F(lua_macro, isolation_validate_does_not_reenter_the_lua_state_of_a_running_macro) {
    auto macro = Load("reentry", std::string(first_dialogue) + R"(
aegisub.register_macro("Reentry", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "ran"; subs[i] = l
end, function() return true end)
)");
    ASSERT_TRUE(macro);
    EXPECT_TRUE(macro->Validate(c.get()));
    bool during = true;
    progress.on_enter = [&] { during = macro->Validate(c.get()); };
    Run(macro);
    EXPECT_FALSE(during);
    EXPECT_EQ("ran", Texts()[0]);
    EXPECT_TRUE(macro->Validate(c.get()));
}

TEST_F(lua_macro, isolation_observer_exception_after_publication_keeps_model_and_history_consistent) {
    auto macro = Load("observer", rewrite_first);
    ASSERT_TRUE(macro);
    int refreshed = 0;
    agi::signal::Connection reader = c->ass->AddCommitListener([&](int, AssDialogue const*) { ++refreshed; });
    agi::signal::Connection thrower = c->ass->AddCommitListener([](int, AssDialogue const*) { throw std::runtime_error("observer"); });
    EXPECT_THROW(Run(macro), std::runtime_error);
    thrower.Disconnect();
    EXPECT_EQ("rewritten twice", Texts()[0]);
    EXPECT_EQ("Rewrite", c->subsController->GetUndoDescription());
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    // The exclusive borrow was released: ordinary editing and history work.
    c->subsController->Undo();
    EXPECT_EQ("rewritten", Texts()[0]);
    c->subsController->Undo();
    EXPECT_EQ("first", Texts()[0]);
    ExpectHistoryUnchanged();
    (void)refreshed;
}

TEST_F(lua_macro, isolation_one_commit_notification_for_a_multi_step_run) {
    auto macro = Load("notify", rewrite_first);
    ASSERT_TRUE(macro);
    std::vector<int> types;
    agi::signal::Connection reader = c->ass->AddCommitListener([&](int type, AssDialogue const*) { types.push_back(type); });
    Run(macro);
    ASSERT_EQ(1u, types.size());
    EXPECT_EQ(AssFile::COMMIT_DIAG_ADDREM, types[0]);
    EXPECT_EQ("rewritten twice", Texts()[0]);
}

TEST_F(lua_macro, legacy_deleting_every_line_commits_a_default_line_and_undo_restores_all) {
    auto macro = Load("delete_all", R"(
aegisub.register_macro("Delete all", "", function(subs)
    local d = {}
    for i = 1, #subs do if subs[i].class == "dialogue" then d[#d + 1] = i end end
    subs.delete(d)
    aegisub.set_undo_point("deleted")
    subs.append({class = "dialogue", comment = false, layer = 0, start_time = 0, end_time = 0, style = "Default",
                 actor = "", margin_l = 0, margin_r = 0, margin_t = 0, effect = "", text = "appended"})
end)
)");
    ASSERT_TRUE(macro);
    auto ids = Ids();
    Run(macro);
    ASSERT_EQ(1u, c->ass->Events.size());
    EXPECT_EQ("appended", Texts()[0]);
    c->subsController->Undo();
    ASSERT_EQ(1u, c->ass->Events.size());
    EXPECT_EQ("", Texts()[0]);
    EXPECT_EQ("deleted", c->subsController->GetUndoDescription());
    EXPECT_EQ(0, c->ass->Events.front().Row);
    c->subsController->Undo();
    EXPECT_EQ(ids, Ids());
    c->subsController->Redo(); c->subsController->Redo();
    EXPECT_EQ("appended", Texts()[0]);
}

TEST_F(lua_macro, legacy_rewritten_bound_lines_keep_their_binding_extradata_entry) {
    uint32_t binding;
    {
        auto write = c->subtitleAccess->TryWrite();
        binding = c->ass->AddExtradata("aegisub.enhanced.subtitle-binding", "1:group");
        c->ass->Events.front().ExtradataIds = std::vector<uint32_t>{binding};
        std::next(c->ass->Events.begin())->ExtradataIds = std::vector<uint32_t>{binding};
        c->ass->AddExtradata("unknown.key", "opaque 42");
        c->ass->Commit("bind", AssFile::COMMIT_EXTRADATA);
    }
    auto before = c->ass->Extradata;
    auto macro = Load("binding", rewrite_first);
    ASSERT_TRUE(macro);
    Run(macro);
    EXPECT_EQ(std::vector<uint32_t>{binding}, c->ass->Events.front().ExtradataIds.get());
    EXPECT_EQ(std::vector<uint32_t>{binding}, std::next(c->ass->Events.begin())->ExtradataIds.get());
    ASSERT_EQ(before.size(), c->ass->Extradata.size());
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].id, c->ass->Extradata[i].id);
        EXPECT_EQ(before[i].value, c->ass->Extradata[i].value);
    }
}

TEST_F(lua_macro, legacy_macro_without_a_name_applies_changes_without_history) {
    auto macro = Load("nameless", std::string(first_dialogue) + R"(
aegisub.register_macro("", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "nameless"; subs[i] = l
end)
)");
    ASSERT_TRUE(macro);
    int notified = 0;
    agi::signal::Connection reader = c->ass->AddCommitListener([&](int, AssDialogue const*) { ++notified; });
    Run(macro);
    EXPECT_EQ("nameless", Texts()[0]);
    ExpectHistoryUnchanged();
    EXPECT_EQ(1, notified); // deliberate: observers are told about the change
    EXPECT_EQ(0, c->ass->Events.front().Row);
}

TEST_F(lua_macro, isolation_macro_result_survives_save_and_reopen) {
    auto macro = Load("save_reopen", std::string(first_dialogue) + R"(
aegisub.register_macro("Save reopen", "", function(subs)
    local i = first_dialogue(subs)
    local l = subs[i]; l.text = "{\\i1}saved"; l.actor = "Lua"; l.extra = {["lua.saved"] = "yes"}; subs[i] = l
end)
)");
    ASSERT_TRUE(macro);
    Run(macro);
    auto path = agi::fs::path((std::filesystem::current_path() / "lua-saved.ass").wstring());
    c->subsController->Save(path, "utf-8");
    EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Load(path, "utf-8");
    EXPECT_EQ("{\\i1}saved", Texts()[0]);
    EXPECT_EQ("Lua", c->ass->Events.front().Actor.get());
    auto extra = c->ass->GetExtradata(c->ass->Events.front().ExtradataIds.get());
    ASSERT_EQ(1u, extra.size());
    EXPECT_EQ("yes", extra[0].value);
}

TEST_F(lua_macro, isolation_export_filter_changes_only_its_detached_copy) {
    auto path = std::filesystem::current_path() / "filter.lua";
    std::ofstream(path) << R"(script_name = "filter"
aegisub.register_filter("Lua isolation filter", "", 1, function(subs)
    for i = 1, #subs do
        if subs[i].class == "dialogue" then local l = subs[i]; l.text = "filtered"; subs[i] = l end
    end
end)
)";
    auto script = Automation4::ScriptFactory::CreateFromFile(agi::fs::path(path.wstring()), false);
    ASSERT_TRUE(script && script->GetLoadedState());
    auto filters = script->GetFilters();
    ASSERT_EQ(1u, filters.size());
    AssFile copy(*c->ass);
    auto ids = Ids();
    filters[0]->ProcessSubs(&copy, window);
    for (auto const& line : copy.Events) EXPECT_EQ("filtered", line.Text.get());
    EXPECT_EQ((std::vector<std::string>{"first", "second", "third"}), Texts());
    EXPECT_EQ(ids, Ids());
    ExpectHistoryUnchanged();
    scripts.push_back(std::move(script));
}

TEST_F(lua_macro, compatibility_bundled_autoload_scripts_load_and_strip_tags_runs) {
    auto source = std::filesystem::path(AEGISUB_SOURCE_ROOT) / "automation" / "autoload";
    ASSERT_TRUE(std::filesystem::exists(source)) << source;
    cmd::Command* strip = nullptr;
    for (auto const& entry : std::filesystem::directory_iterator(source)) {
        auto script = Automation4::ScriptFactory::CreateFromFile(agi::fs::path(entry.path().wstring()), false);
        ASSERT_TRUE(script) << entry.path();
        EXPECT_TRUE(script->GetLoadedState()) << entry.path() << ": " << script->GetDescription();
        for (auto macro : script->GetMacros())
            if (entry.path().filename() == "strip-tags.lua") strip = macro;
        scripts.push_back(std::move(script));
    }
    ASSERT_TRUE(strip);
    {
        auto write = c->subtitleAccess->TryWrite();
        c->ass->Events.front().Text = "{\\b1}bold{\\i1} text";
        c->ass->Commit("tags", AssFile::COMMIT_DIAG_TEXT);
    }
    auto id = Id(1);
    Run(strip);
    EXPECT_EQ("bold text", Texts()[0]);
    EXPECT_EQ(id, Id(1));
    c->subsController->Undo();
    EXPECT_EQ("{\\b1}bold{\\i1} text", Texts()[0]);
}

namespace {
long status_kb(const char* field) {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind(field, 0) == 0) return std::stol(line.substr(std::string(field).size()));
    return -1; // not available on this platform
}
}

TEST_F(lua_macro, performance_phases_for_large_documents) {
    auto macro = Load("bulk", R"(
aegisub.register_macro("Bulk", "", function(subs)
    for i = 1, #subs do
        if subs[i].class == "dialogue" then local l = subs[i]; l.text = l.text .. "!"; l.extra = {["bulk"] = "x"}; subs[i] = l end
        if i % 1000 == 0 then aegisub.progress.set(i * 100 / #subs) end
    end
    aegisub.set_undo_point("bulk one")
    for i = 1, #subs do
        if subs[i].class == "dialogue" then local l = subs[i]; l.layer = 1; subs[i] = l end
    end
end)
)");
    ASSERT_TRUE(macro);
    using clock = std::chrono::steady_clock;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    for (int n : {1000, 5000, 10000}) {
        {
            auto write = c->subtitleAccess->TryWrite();
            c->ass->Events.clear_and_dispose([](AssDialogue* d) { delete d; });
            for (int i = 0; i < n; ++i) { auto l = new AssDialogue; l->Text = "line " + std::to_string(i); c->ass->Events.push_back(*l); }
            for (int i = 0; i < 200; ++i) c->ass->AddExtradata("key" + std::to_string(i), "value");
            c->ass->Commit("bulk fixture", AssFile::COMMIT_NEW);
            c->selectionController->SetSelectionAndActiveIDs({Id(0)}, Id(0));
        }
        // Host phases measured on the same production API the macro uses.
        long rss_before = status_kb("VmRSS:");
        auto t0 = clock::now();
        auto transaction = c->subsController->BeginLuaTransaction();
        auto t1 = clock::now();
        LuaSubtitleResult result;
        std::vector<AssEntry const*> lines;
        for (auto const& info : transaction.Work().Info) { (void)info; lines.push_back(nullptr); }
        for (auto const& style : transaction.Work().Styles) lines.push_back(&style);
        for (auto const& event : transaction.Work().Events) {
            // A replaced line, as the Lua API creates it: same values, new ID.
            auto copy = std::make_unique<AssDialogue>();
            auto id = copy->Id;
            static_cast<AssDialogueBase&>(*copy) = event;
            copy->Id = id;
            copy->Text = event.Text.get() + "!";
            lines.push_back(copy.get());
            result.pool.push_back(std::move(copy));
        }
        auto t2 = clock::now();
        result.steps.push_back({"bulk one", AssFile::COMMIT_DIAG_ADDREM, true, lines});
        result.steps.push_back({"bulk two", AssFile::COMMIT_DIAG_ADDREM, true, lines});
        result.final_lines = lines;
        auto publication = c->subsController->PrepareLuaPublication(std::move(transaction), std::move(result));
        auto t3 = clock::now();
        auto ids = publication.FinalEventIds();
        c->subsController->PublishLua(std::move(publication), {ids.front()}, ids.front());
        auto t4 = clock::now();
        // Full production macro run (includes script execution on the worker).
        auto t5 = clock::now();
        Run(macro);
        auto t6 = clock::now();
        EXPECT_EQ("line 0!!", Texts()[0]);
        EXPECT_EQ(1, c->ass->Events.front().Layer);
        std::printf("[lua-perf] lines=%d snapshot=%.1fms convert=%.1fms validate+history=%.1fms publish=%.1fms full-macro=%.1fms rss-before=%ldkB rss-after=%ldkB peak=%ldkB\n",
            n, ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4), ms(t5, t6), rss_before, status_kb("VmRSS:"), status_kb("VmHWM:"));
    }
}

TEST_F(lua_macro, isolation_subtitle_methods_report_cpp_errors_as_lua_errors) {
    // delete/append raise C++ exceptions internally; they must reach the script
    // as ordinary Lua errors with their message, not cross the C boundary.
    auto macro = Load("errors", std::string(first_dialogue) + R"(
aegisub.register_macro("Errors", "", function(subs)
    local ok1, e1 = pcall(subs.delete, 9999)
    local ok2, e2 = pcall(subs.append, {class = "dialogue"})
    local i = first_dialogue(subs)
    local l = subs[i]
    l.text = tostring(ok1) .. "|" .. tostring(e1) .. "|" .. tostring(ok2) .. "|" .. tostring(e2)
    subs[i] = l
end)
)");
    ASSERT_TRUE(macro);
    Run(macro);
    auto text = Texts()[0];
    EXPECT_NE(std::string::npos, text.find("false|")) << text;
    EXPECT_NE(std::string::npos, text.find("Out of range line index")) << text;
    EXPECT_NE(std::string::npos, text.find("Invalid or missing field")) << text;
}
