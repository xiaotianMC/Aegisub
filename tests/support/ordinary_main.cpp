// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <gtest/gtest.h>
#include "options.h"
#include "libresrc/libresrc.h"
#include "command/command.h"
#include "include/aegisub/hotkey.h"
#include <libaegisub/dispatch.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>
#include <libaegisub/mru.h>
#include <wx/app.h>
#include <wx/image.h>
#include <wx/log.h>
#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
    // Reuse AegisubApp's factory, but never run OnInit (which opens the editor,
    // loads user settings, scripts and recovery files).
    if (!wxEntryStart(argc, argv)) return 2;
    wxInitAllImageHandlers();
    wxLog::SetActiveTarget(new wxLogStderr);
    wxSetAssertHandler([](wxString const& file, int line, wxString const&, wxString const& condition, wxString const&) {
        std::fprintf(stderr, "wx assertion %s:%d: %s\n", file.utf8_str().data(), line, condition.utf8_str().data());
        std::abort();
    });
    agi::dispatch::Init([](agi::dispatch::Thunk) {});
    agi::log::LogSink log; agi::log::log = &log;
    agi::Path paths;
    auto native_root = std::filesystem::current_path() / "ordinary-user";
    std::filesystem::create_directories(native_root);
    auto root = agi::fs::path(native_root.wstring());
    paths.SetToken("?user", root); paths.SetToken("?local", root); paths.SetToken("?data", root);
    config::path = &paths;
    agi::Options options{"", GET_DEFAULT_CONFIG(default_config), agi::Options::FLUSH_SKIP};
    config::opt = &options;
    options.Get("App/Auto/Save")->SetBool(false);
    options.Get("App/Auto/Save on Every Change")->SetBool(false);
    agi::MRUManager mru{root / "mru.json", GET_DEFAULT_CONFIG(default_mru), &options};
    config::mru = &mru;
    hotkey::init(); cmd::init_builtin_commands();
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    cmd::clear();
    config::mru = nullptr; config::opt = nullptr; config::path = nullptr;
    wxEntryCleanup();
    agi::log::log = nullptr;
    return result;
}
