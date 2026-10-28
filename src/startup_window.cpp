// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "startup_window.h"

#include "main.h"
#include "libresrc/libresrc.h"

#include <algorithm>
#include <memory>
#include <wx/bmpbuttn.h>
#include <wx/dcmemory.h>
#include <wx/frame.h>
#include <wx/graphics.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

namespace {
const wxColour background(246, 248, 252), ink(25, 34, 51), muted(101, 113, 132), blue(0, 103, 192);

wxFont Font(int pixels, bool bold = false) {
    return wxFont(wxFontInfo(wxSize(0, pixels)).FaceName("Segoe UI")
        .Weight(bold ? wxFONTWEIGHT_SEMIBOLD : wxFONTWEIGHT_NORMAL));
}

enum class ButtonKind { Projects, Editor, Back, Open, Minimize, Maximize, Close };

// Bitmap buttons retain native keyboard and accessibility behaviour while the
// artwork supplies a consistent Fluent-style appearance at the current DPI.
class FluentButton final : public wxBitmapButton {
    ButtonKind kind;
    wxString title, description, action;

    wxBitmap Artwork(bool highlighted) {
        auto size = GetClientSize();
        wxBitmap bitmap(std::max(1, size.x), std::max(1, size.y), 32);
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        auto gc = std::unique_ptr<wxGraphicsContext>(wxGraphicsContext::Create(dc));
        if (!gc) return bitmap;
        double scale = FromDIP(100) / 100.0;
        gc->Scale(scale, scale);
        double w = size.x / scale, h = size.y / scale;
        auto text = [&](wxString const& label, int pixels, wxColour colour, double x, double y, bool bold = false) {
            gc->SetFont(Font(pixels, bold), colour);
            gc->DrawText(label, x, y);
        };
        auto line = [&](double x1, double y1, double x2, double y2) { gc->StrokeLine(x1, y1, x2, y2); };
        if (kind == ButtonKind::Projects || kind == ButtonKind::Editor) {
            gc->SetBrush(wxBrush(highlighted ? wxColour(249, 252, 255) : *wxWHITE));
            gc->SetPen(wxPen(highlighted ? blue : wxColour(221, 227, 235), highlighted ? 2 : 1));
            gc->DrawRoundedRectangle(2, 2, w - 4, h - 4, 14);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(wxColour(234, 243, 255)));
            gc->DrawRoundedRectangle(28, 28, 58, 58, 14);
            gc->SetPen(wxPen(blue, 2));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawRoundedRectangle(43, 44, 28, 25, 3);
            if (kind == ButtonKind::Projects) {
                line(43, 51, 71, 51);
                line(52, 51, 52, 69);
                line(62, 51, 62, 69);
            }
            else {
                line(49, 52, 65, 52);
                line(49, 58, 65, 58);
                line(49, 64, 59, 64);
            }
            text(title, 23, ink, 28, 109, true);
            text(description, 14, muted, 28, 151);
            text(action, 14, blue, 28, h - 48, true);
            gc->SetPen(wxPen(blue, 1.7));
            line(w - 53, h - 38, w - 35, h - 38);
            line(w - 41, h - 44, w - 35, h - 38);
            line(w - 41, h - 32, w - 35, h - 38);
        }
        else if (kind == ButtonKind::Back || kind == ButtonKind::Open) {
            bool primary = kind == ButtonKind::Open;
            gc->SetBrush(wxBrush(primary ? (highlighted ? wxColour(0, 86, 164) : blue)
                : highlighted ? wxColour(232, 237, 245) : GetParent()->GetBackgroundColour()));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(1, 1, w - 2, h - 2, 7);
            gc->SetFont(Font(14, primary), primary ? *wxWHITE : ink);
            double tw, th;
            gc->GetTextExtent(title, &tw, &th);
            gc->DrawText(title, (w - tw) / 2, (h - th) / 2);
        }
        else {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(highlighted ? (kind == ButtonKind::Close ? wxColour(196, 43, 28)
                : wxColour(229, 234, 242)) : GetParent()->GetBackgroundColour()));
            gc->DrawRectangle(0, 0, w, h);
            gc->SetPen(wxPen(highlighted && kind == ButtonKind::Close ? *wxWHITE : ink, 1));
            double x = w / 2, y = h / 2;
            if (kind == ButtonKind::Minimize) line(x - 5, y + 3, x + 5, y + 3);
            else if (kind == ButtonKind::Maximize) {
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->DrawRectangle(x - 5, y - 5, 10, 10);
            }
            else { line(x - 5, y - 5, x + 5, y + 5); line(x + 5, y - 5, x - 5, y + 5); }
        }
        gc.reset();
        dc.SelectObject(wxNullBitmap);
        return bitmap;
    }

    void Redraw() {
        if (GetClientSize().x < 1 || GetClientSize().y < 1) return;
        SetBitmapLabel(Artwork(false));
        SetBitmapCurrent(Artwork(true));
        SetBitmapFocus(Artwork(true));
        SetBitmapPressed(Artwork(true));
    }
public:
    FluentButton(wxWindow *parent, ButtonKind kind, wxString title, wxSize size,
        wxString description = {}, wxString action = {})
    : wxBitmapButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, parent->FromDIP(size), wxBORDER_NONE)
    , kind(kind), title(std::move(title)), description(std::move(description)), action(std::move(action)) {
        SetName(this->title);
        SetLabel(this->title);
        SetToolTip(this->description.empty() ? this->title : this->title + "\n" + this->description);
        SetMinSize(parent->FromDIP(size));
        SetCursor(wxCURSOR_HAND);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { Redraw(); event.Skip(); });
        Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) { Redraw(); event.Skip(); });
        Redraw();
    }
};

wxStaticText *Label(wxWindow *parent, wxString const& text, int pixels, wxColour colour = ink, bool bold = false) {
    auto label = new wxStaticText(parent, wxID_ANY, text);
    auto font = Font(parent->FromDIP(pixels), bold);
    label->SetFont(font);
    label->SetForegroundColour(colour);
    return label;
}

class StartupWindow final : public wxFrame {
    wxPanel *home, *board;
    wxWindow *home_focus, *board_focus;
    wxBoxSizer *pages;

    void ShowBoard(bool show) {
        Freeze();
        home->Show(!show);
        board->Show(show);
        Layout();
        Thaw();
        (show ? board_focus : home_focus)->SetFocus();
    }

public:
    void OpenDashboard() { ShowBoard(true); }

    StartupWindow()
    : wxFrame(nullptr, wxID_ANY, "Aegisub Native",
        wxDefaultPosition, wxDefaultSize,
#ifdef __WXMSW__
        wxDEFAULT_FRAME_STYLE & ~wxCAPTION
#else
        wxDEFAULT_FRAME_STYLE
#endif
    ) {
        SetBackgroundColour(background);
        SetClientSize(FromDIP(wxSize(1000, 660)));
        SetMinClientSize(FromDIP(wxSize(860, 580)));
#ifdef __WXMSW__
        SetIcon(wxICON(wxicon));
        // Prefer Windows 11 rounded corners and a light frame, where supported.
        if (auto dwm = LoadLibraryW(L"dwmapi.dll")) {
            using SetAttribute = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
            auto set = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
            if (set) {
                DWORD corner = 2, light = 0;
                set((HWND)GetHandle(), 33, &corner, sizeof(corner));
                set((HWND)GetHandle(), 20, &light, sizeof(light));
            }
            FreeLibrary(dwm);
        }
#endif
        auto root = new wxBoxSizer(wxVERTICAL);
        auto header = new wxPanel(this);
        header->SetBackgroundColour(background);
        auto chrome = new wxBoxSizer(wxHORIZONTAL);
        auto brand = Label(header, "Aegisub Native", 14, ink, true);
        chrome->Add(brand, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(26));
        chrome->AddStretchSpacer();
#ifdef __WXMSW__
        auto drag = [this](wxMouseEvent&) {
            ::ReleaseCapture();
            ::SendMessageW((HWND)GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        };
        header->Bind(wxEVT_LEFT_DOWN, drag);
        brand->Bind(wxEVT_LEFT_DOWN, drag);
        header->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent&) { Maximize(!IsMaximized()); });
        for (auto kind : {ButtonKind::Minimize, ButtonKind::Maximize, ButtonKind::Close}) {
            auto title = kind == ButtonKind::Minimize ? _("Minimize") : kind == ButtonKind::Maximize ? _("Maximize / Restore") : _("Close");
            auto button = new FluentButton(header, kind, title, wxSize(46, 46));
            button->Bind(wxEVT_BUTTON, [this, kind](wxCommandEvent&) {
                if (kind == ButtonKind::Minimize) Iconize();
                else if (kind == ButtonKind::Maximize) Maximize(!IsMaximized());
                else Close();
            });
            chrome->Add(button, 0);
        }
#endif
        header->SetSizer(chrome);
        root->Add(header, 0, wxEXPAND);
        pages = new wxBoxSizer(wxVERTICAL);
        home = new wxPanel(this);
        board = new wxPanel(this);
        home->SetBackgroundColour(background);
        board->SetBackgroundColour(background);
        auto content = new wxBoxSizer(wxVERTICAL);
        content->AddStretchSpacer(2);
        content->Add(Label(home, _("Your subtitle workspace"), 34, ink, true), 0, wxALIGN_CENTER_HORIZONTAL);
        content->AddSpacer(FromDIP(14));
        content->Add(Label(home, _("Organize your projects, or jump straight into editing."), 15, muted), 0, wxALIGN_CENTER_HORIZONTAL);
        content->AddSpacer(FromDIP(36));
        auto choices = new wxBoxSizer(wxHORIZONTAL);
        auto projects = new FluentButton(home, ButtonKind::Projects, _("Project dashboard"), wxSize(352, 264),
            _("A dedicated space for your\nsubtitle projects."), _("Go to dashboard"));
        auto editor = new FluentButton(home, ButtonKind::Editor, _("Subtitle editor"), wxSize(352, 264),
            _("Open the familiar editor and\nstart working right away."), _("Open editor"));
        home_focus = projects;
        projects->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowBoard(true); });
        editor->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { wxGetApp().OpenEditor(); });
        choices->Add(projects, 1, wxEXPAND);
        choices->AddSpacer(FromDIP(24));
        choices->Add(editor, 1, wxEXPAND);
        content->Add(choices, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(60));
        content->AddStretchSpacer(3);
        content->Add(Label(home, _("You can also open subtitle files directly from Windows."), 12, muted), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(24));
        home->SetSizer(content);

        auto dashboard = new wxBoxSizer(wxVERTICAL);
        auto back = new FluentButton(board, ButtonKind::Back, _("Back to start"), wxSize(128, 40));
        board_focus = back;
        back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowBoard(false); });
        dashboard->Add(back, 0, wxLEFT | wxTOP, FromDIP(48));
        dashboard->AddSpacer(FromDIP(24));
        dashboard->Add(Label(board, _("Project dashboard"), 30, ink, true), 0, wxLEFT, FromDIP(60));
        dashboard->AddSpacer(FromDIP(10));
        dashboard->Add(Label(board, _("A place for your subtitle projects."), 15, muted), 0, wxLEFT, FromDIP(60));
        dashboard->AddStretchSpacer(2);
        dashboard->Add(Label(board, _("No projects yet"), 23, ink, true), 0, wxALIGN_CENTER_HORIZONTAL);
        dashboard->AddSpacer(FromDIP(12));
        dashboard->Add(Label(board, _("Your projects will appear here."), 15, muted), 0, wxALIGN_CENTER_HORIZONTAL);
        dashboard->AddSpacer(FromDIP(28));
        auto open = new FluentButton(board, ButtonKind::Open, _("Open subtitle editor"), wxSize(184, 42));
        open->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { wxGetApp().OpenEditor(); });
        dashboard->Add(open, 0, wxALIGN_CENTER_HORIZONTAL);
        dashboard->AddStretchSpacer(3);
        board->SetSizer(dashboard);
        pages->Add(home, 1, wxEXPAND);
        pages->Add(board, 1, wxEXPAND);
        board->Hide();
        root->Add(pages, 1, wxEXPAND);
        SetSizer(root);
        Layout();
        Centre();
        projects->SetFocus();
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_ESCAPE && board->IsShown()) ShowBoard(false);
            else event.Skip();
        });
    }
};
}

wxFrame *CreateStartupWindow() { return new StartupWindow; }
void ShowStartupDashboard(wxFrame *window) { static_cast<StartupWindow *>(window)->OpenDashboard(); }
