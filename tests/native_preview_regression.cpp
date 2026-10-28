// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Runs against the native application objects, without injecting mouse/keyboard input.
#include "main.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "dialog_rectangle.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_ctrl.h"

#include <cstdio>
#include <stdexcept>
#include <wx/dialog.h>
#include <wx/spinctrl.h>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T> static T *find_child(wxWindow *parent) {
    if (auto child = dynamic_cast<T*>(parent)) return child;
    for (auto child : parent->GetChildren())
        if (auto result = find_child<T>(child)) return result;
    return nullptr;
}

static void edit(agi::Context& c, const std::function<void(wxDialog&, wxSpinCtrlDouble&)>& action) {
    std::exception_ptr failure;
    wxTheApp->CallAfter([&] {
        wxDialog *dialog = nullptr;
        for (auto window : wxTopLevelWindows)
            if (auto candidate = dynamic_cast<wxDialog*>(window); candidate && candidate->IsModal())
                dialog = candidate;
        try {
            require(dialog, "shape dialog did not open");
            auto width = find_child<wxSpinCtrlDouble>(dialog);
            require(width, "shape width field missing");
            action(*dialog, *width);
        }
        catch (...) {
            failure = std::current_exception();
            if (dialog) dialog->EndModal(wxID_CANCEL);
        }
    });
    EditDrawing(&c);
    if (failure) std::rethrow_exception(failure);
}

static void set_width(wxSpinCtrlDouble& field, const wxString& value) {
    field.SetValue(value);
    wxCommandEvent changed(wxEVT_TEXT, field.GetId());
    changed.SetEventObject(&field);
    field.ProcessWindowEvent(changed);
}

int main(int argc, char **argv) {
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    int result = 0;
    try {
        auto& c = wxGetApp().NewProjectContext();
        c.project->LoadSubtitles("../tests/data/shape-editor-test.ass", "UTF-8", false);
        auto line = c.selectionController->GetActiveLine();
        const auto original = line->Text;
        auto editor = find_child<SubsTextEditCtrl>(c.parent);
        require(editor, "native subtitle editor missing");
        auto synced = [&] {
            require(std::string(editor->GetTextRaw().data()) == line->Text.get(), "subtitle editor did not synchronize");
        };
        edit(c, [&](wxDialog& dialog, wxSpinCtrlDouble& width) {
            set_width(width, "180");
            require(line->Text.get().find("\\fscx150") != std::string::npos, "width did not update while dialog was open");
            synced();
            require(!c.subsController->IsModified(), "preview changed saved state");
            require(c.subsController->GetUndoDescription().empty(), "preview added undo history");
            set_width(width, "120");
            require(line->Text == original, "returning to original width changed text");
            set_width(width, "180");
            synced();
            dialog.EndModal(wxID_CANCEL);
        });
        require(line->Text == original, "cancel did not restore text");
        synced();
        require(!c.subsController->IsModified(), "cancel changed saved state");
        edit(c, [&](wxDialog& dialog, wxSpinCtrlDouble& width) {
            set_width(width, "180");
            const auto valid = line->Text;
            set_width(width, "-1");
            require(line->Text == valid, "invalid width changed subtitle");
            set_width(width, "240");
            synced();
            dialog.EndModal(wxID_OK);
        });
        require(c.subsController->IsModified(), "confirmation did not create an edit");
        c.subsController->Undo();
        line = c.selectionController->GetActiveLine();
        require(line->Text == original, "one undo did not restore the whole edit");
        synced();
        require(!c.subsController->IsModified(), "undo did not restore saved state");

        std::puts("PASS: live synchronization, cancel, invalid input and single undo");
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
