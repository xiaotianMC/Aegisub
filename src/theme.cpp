// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "theme.h"

#include "compat.h"
#include "options.h"
#include "libresrc/libresrc.h"

#include <libaegisub/color.h>

#include <cstring>
#include <map>
#include <memory>

#include <wx/app.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/window.h>
#include <wx/settings.h>
#include <wx/toolbar.h>

#ifdef WITH_GTK3_APPEARANCE
#include <gtk/gtk.h>
#endif

namespace theme {
namespace {
agi::signal::Signal<> changed;
agi::signal::Connection mode_listener;
agi::signal::Connection icons_listener;
bool last_dark = false;
bool native_dark = false;
Mode startup_mode = Mode::System;

// Dark defaults of the interface colour options. Colours drawn over video or
// on the audio display (already dark) are not listed: they do not follow the
// appearance.
std::map<std::string, wxColour, std::less<>> const& DarkDefaults() {
    static const std::map<std::string, wxColour, std::less<>> defaults = {
        {"Colour/Subtitle Grid/Background/Background", {0x1e, 0x1f, 0x22}},
        {"Colour/Subtitle Grid/Background/Comment", {0x28, 0x2c, 0x3a}},
        {"Colour/Subtitle Grid/Background/Inframe", {0x2e, 0x2c, 0x1f}},
        {"Colour/Subtitle Grid/Background/Selected Comment", {0x21, 0x40, 0x4a}},
        {"Colour/Subtitle Grid/Background/Selection", {0x1d, 0x47, 0x38}},
        {"Colour/Subtitle Grid/Header", {0x2b, 0x3c, 0x4b}},
        {"Colour/Subtitle Grid/Left Column", {0x25, 0x33, 0x2a}},
        {"Colour/Subtitle Grid/Lines", {0x3a, 0x3d, 0x42}},
        {"Colour/Subtitle Grid/Standard", {0xd8, 0xda, 0xde}},
        {"Colour/Subtitle Grid/Selection", {0xff, 0xff, 0xff}},
        {"Colour/Subtitle Grid/Collision", {0xff, 0x6b, 0x6b}},
        {"Colour/Subtitle Grid/CPS Error", {0xd9, 0x46, 0x46}},
        {"Colour/Subtitle Grid/Active Border", {0xff, 0x7b, 0xf0}},
        {"Colour/Subtitle/Background", {0x1e, 0x1f, 0x22}},
        {"Colour/Subtitle/Syntax/Normal", {0xd8, 0xda, 0xde}},
        {"Colour/Subtitle/Syntax/Comment", {0xd8, 0xda, 0xde}},
        {"Colour/Subtitle/Syntax/Drawing Command", {0xd8, 0xda, 0xde}},
        {"Colour/Subtitle/Syntax/Drawing X", {0xe8, 0xa0, 0xa0}},
        {"Colour/Subtitle/Syntax/Drawing Y", {0xa0, 0xd8, 0xa0}},
        {"Colour/Subtitle/Syntax/Brackets", {0x6e, 0xa8, 0xff}},
        {"Colour/Subtitle/Syntax/Slashes", {0xff, 0x79, 0xc6}},
        {"Colour/Subtitle/Syntax/Tags", {0xa8, 0xae, 0xb5}},
        {"Colour/Subtitle/Syntax/Parameters", {0x9c, 0xcc, 0x65}},
        {"Colour/Subtitle/Syntax/Error", {0xff, 0x80, 0x80}},
        {"Colour/Subtitle/Syntax/Background/Error", {0x5a, 0x26, 0x26}},
        {"Colour/Subtitle/Syntax/Line Break", {0x7a, 0x7f, 0x87}},
        {"Colour/Subtitle/Syntax/Karaoke Template", {0xc7, 0x92, 0xea}},
        {"Colour/Subtitle/Syntax/Karaoke Variable", {0xc7, 0x92, 0xea}},
    };
    return defaults;
}

Mode CurrentMode() {
    auto value = OPT_GET("App/Theme")->GetInt();
    return value == 1 ? Mode::Light : value == 2 ? Mode::Dark : Mode::System;
}

#ifdef WITH_GTK3_APPEARANCE
// GTK picks the dark variant of the theme on request. A system that is dark
// through the theme's name (e.g. "Yaru-dark") is made light by dropping the
// suffix; the original name comes back for "follow the system".
bool ThemeInstalled(std::string const& name) {
    auto has = [&](std::string const& dir) {
        auto path = g_build_filename(dir.c_str(), name.c_str(), "gtk-3.0", nullptr);
        bool found = g_file_test(path, G_FILE_TEST_IS_DIR);
        g_free(path);
        return found;
    };
    if (has(std::string(g_get_user_data_dir()) + "/themes") || has(std::string(g_get_home_dir()) + "/.themes")) return true;
    for (auto dirs = g_get_system_data_dirs(); *dirs; ++dirs)
        if (has(std::string(*dirs) + "/themes")) return true;
    return false;
}

void ApplyGtk(Mode mode) {
    auto settings = gtk_settings_get_default();
    if (!settings) return;
    static std::string original_name;
    static gboolean original_prefer_dark = false;
    static bool saved = false;
    if (!saved) {
        gchar *name = nullptr;
        g_object_get(settings, "gtk-theme-name", &name, "gtk-application-prefer-dark-theme", &original_prefer_dark, nullptr);
        original_name = name ? name : "";
        g_free(name);
        saved = true;
    }
    std::string name = original_name;
    gboolean prefer_dark = original_prefer_dark;
    if (mode == Mode::Dark) {
        // Not every theme draws its dark variant on request (Yaru's entries
        // stay light): use the theme's own dark sibling when installed.
        prefer_dark = true;
        if (!name.ends_with("-dark") && ThemeInstalled(name + "-dark")) name += "-dark";
    }
    if (mode == Mode::Light) {
        prefer_dark = false;
        for (auto suffix : {"-dark", "-Dark", ":dark"})
            if (name.size() > strlen(suffix) && name.ends_with(suffix)) name.resize(name.size() - strlen(suffix));
    }
    g_object_set(settings, "gtk-theme-name", name.c_str(), "gtk-application-prefer-dark-theme", prefer_dark, nullptr);
}
#endif

void Apply() {
#ifdef WITH_GTK3_APPEARANCE
    ApplyGtk(CurrentMode());
#endif
}

void Notify() {
    bool dark = IsDark();
    last_dark = dark;
    libresrc_set_dark(dark);
    changed();
}
}

void Init() {
    [[maybe_unused]] auto mode = CurrentMode();
    startup_mode = mode;
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 3, 0)
    // Windows themes its own controls only from startup.
    if (mode != Mode::Light && wxTheApp)
        native_dark = wxTheApp->MSWEnableDarkMode(mode == Mode::Dark ? wxApp::DarkMode_Always : wxApp::DarkMode_Auto)
            && (mode == Mode::Dark || wxSystemSettings::GetAppearance().IsDark());
#endif
    Apply();
    last_dark = IsDark();
    libresrc_set_dark(last_dark);
    mode_listener = OPT_SUB("App/Theme", [] { Apply(); Notify(); });
    // Icons are remade on appearance changes: so they are for the icon style
    libresrc_set_classic(OPT_GET("App/Classic Icons")->GetBool());
    icons_listener = OPT_SUB("App/Classic Icons", [] {
        libresrc_set_classic(OPT_GET("App/Classic Icons")->GetBool());
        Notify();
    });
}

bool IsDark() {
    switch (CurrentMode()) {
        case Mode::Light: return false;
        case Mode::Dark: return true;
        case Mode::System: break;
    }
#if defined(__WXMSW__) && !wxCHECK_VERSION(3, 3, 0)
    // Before wxWidgets 3.3 Windows controls stay light: so does the rest.
    return false;
#else
    return wxSystemSettings::GetAppearance().IsDark();
#endif
}

bool RestartNeeded() {
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 3, 0)
    // Changed since startup, and Windows still draws the other way
    return CurrentMode() != startup_mode && IsDark() != native_dark;
#else
    return false;
#endif
}

bool NativeDark() {
    return native_dark;
}

long Border(long border) {
    if (!native_dark) return border;
    if (border == wxBORDER_SUNKEN || border == wxBORDER_RAISED) return wxBORDER_SIMPLE;
    return border;
}

wxColour Colour(std::string const& option) {
    auto opt = OPT_GET(option);
    if (IsDark() && opt->IsDefault()) {
        auto const& defaults = DarkDefaults();
        if (auto it = defaults.find(option); it != defaults.end()) return it->second;
    }
    return to_wx(opt->GetColor());
}

bool HasDarkDefault(std::string const& option) {
    return DarkDefaults().contains(option);
}

void SystemColoursChanged() {
    if (CurrentMode() == Mode::System && IsDark() != last_dark) Notify();
}

agi::signal::UnscopedConnection AddChangeListener(std::function<void()> listener) {
    return changed.Connect(std::move(listener));
}

wxColour Surface() {
#ifdef __WXGTK__
    // GTK themes draw toolbars in a colour of their own that they keep
    // whatever the program asks: take it for the whole block.
    return wxToolBar::GetClassDefaultAttributes().colBg;
#else
    return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
#endif
}

wxColour Backdrop() {
    // Darker than the blocks in both appearances, like video editors do:
    // a little in the light one, clearly in the dark one.
    wxColour surface = Surface();
    double keep = IsDark() ? 0.62 : 0.86;
    auto channel = [&](unsigned char c) { return static_cast<unsigned char>(c * keep + 0.5); };
    return wxColour(channel(surface.Red()), channel(surface.Green()), channel(surface.Blue()));
}

int BlockGap(wxWindow const *window) {
    return window ? window->FromDIP(5) : 5;
}

namespace {
void Use(wxWindow *window, wxColour (*colour)()) {
    window->SetBackgroundColour(colour());
    auto connection = std::make_shared<agi::signal::Connection>(AddChangeListener([window, colour] {
        window->SetBackgroundColour(colour());
        window->Refresh();
    }));
    window->Bind(wxEVT_DESTROY, [window, connection](wxWindowDestroyEvent &event) {
        if (event.GetEventObject() == window) connection->Disconnect();
        event.Skip();
    });
}
}

void UseSurface(wxWindow *window) { Use(window, Surface); }
void UseBackdrop(wxWindow *window) { Use(window, Backdrop); }

wxColour Mix(wxColour const& from, wxColour const& to, double share) {
    auto channel = [share](unsigned char a, unsigned char b) {
        return static_cast<unsigned char>(a + (b - a) * share + 0.5);
    };
    return wxColour(channel(from.Red(), to.Red()), channel(from.Green(), to.Green()), channel(from.Blue(), to.Blue()));
}

Ui UiColours() {
    if (IsDark()) return {{0x6f, 0xb0, 0xff}, {0x4a, 0x3f, 0x1c}, {0xf3, 0xe6, 0xb5},
                          {0x25, 0x26, 0x29}, {0xd8, 0xda, 0xde}, {0x8b, 0x90, 0x99}, {0x2b, 0x3f, 0x5c}};
    return {{0x1a, 0x63, 0xc4}, {0xff, 0xf4, 0xce}, {0x5c, 0x44, 0x00},
            {0xff, 0xff, 0xff}, {0x1f, 0x23, 0x28}, {0x6a, 0x73, 0x7d}, {0xdc, 0xe8, 0xfa}};
}

std::pair<int, int> TextInk(wxFont const& font, wxString const& text) {
    static std::map<wxString, std::pair<int, int>> cache;
    wxString key = font.GetNativeFontInfoDesc() + "\n" + text;
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;

    const int pad = 2;
    wxSize extent;
    {
        wxBitmap one(1, 1);
        wxMemoryDC memory(one);
        wxGCDC dc(memory);
        dc.SetFont(font);
        extent = dc.GetTextExtent(text);
    }
    std::pair<int, int> rows{0, std::max(1, extent.y)};
    wxBitmap bitmap(extent.x + 2 * pad, extent.y + 2 * pad, 24);
    {
        wxMemoryDC memory(bitmap);
        memory.SetBackground(*wxWHITE_BRUSH);
        memory.Clear();
        wxGCDC dc(memory);
        dc.SetFont(font);
        dc.SetTextForeground(*wxBLACK);
        dc.DrawText(text, pad, pad);
    }
    wxImage image = bitmap.ConvertToImage();
    int top = -1, bottom = -1;
    for (int y = 0; y < image.GetHeight(); ++y)
        for (int x = 0; x < image.GetWidth(); ++x)
            if (image.GetRed(x, y) < 128) {
                if (top < 0) top = y;
                bottom = y + 1;
                break;
            }
    if (top >= 0) rows = {top - pad, bottom - pad};
    cache[key] = rows;
    return rows;
}
}
