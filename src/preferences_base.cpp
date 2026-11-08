// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

/// @file preferences_base.cpp
/// @brief Base preferences dialogue classes
/// @ingroup configuration_ui

#include "preferences_base.h"
#include "utils.h"

#include "colour_button.h"
#include "compat.h"
#include "options.h"
#include "preferences.h"
#include "flat_controls.h"
#include "theme.h"

#include <libaegisub/exception.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <memory>

#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/dirdlg.h>
#include <wx/fontdlg.h>
#include <wx/textctrl.h>
#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/settings.h>
#include <wx/simplebook.h>

#undef Bool

#define OPTION_UPDATER(type, evttype, opt, body)                            \
	class type {                                                            \
		std::string name;                                                   \
		Preferences *parent;                                                \
	public:                                                                 \
		type(std::string const& n, Preferences *p) : name(n), parent(p) { } \
		void operator()(evttype& evt) {                                     \
			evt.Skip();                                                     \
			parent->SetOption(std::make_unique<agi::opt>(name, body));      \
		}                                                                   \
	}

OPTION_UPDATER(StringUpdater, wxCommandEvent, OptionValueString, from_wx(evt.GetString()));
OPTION_UPDATER(BoolUpdater, wxCommandEvent, OptionValueBool, !!evt.GetInt());
OPTION_UPDATER(ColourUpdater, ValueEvent<agi::Color>, OptionValueColor, evt.Get());

namespace {
void browse_button(wxTextCtrl *ctrl) {
	wxDirDialog dlg(nullptr, _("Please choose the folder:"), config::path->Decode(from_wx(ctrl->GetValue())).wstring());
	if (dlg.ShowModal() == wxID_OK) {
		wxString dir = dlg.GetPath();
		if (!dir.empty())
			ctrl->SetValue(dir);
	}
}

/// The width of the column of controls, in DIP
const int control_width = 220;

/// A text field drawn as the edit box's are, @p width DIP wide at least
wxTextCtrl *MakeText(wxWindow *parent, FlatFrame *&frame, wxString const& value, int width = 120) {
	frame = new FlatFrame(parent, "");
	auto text = new wxTextCtrl(frame, -1, value, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
	frame->SetContent(text, width);
	// Enter is done with it, not the dialog's OK
	text->Bind(wxEVT_TEXT_ENTER, [](wxCommandEvent &) { flat::LeaveField(); });
	return text;
}

/// A number field: typed in, or changed by the wheel a step at a time
struct NumberField {
	FlatFrame *frame;
	/// Show @p value, without calling back
	std::function<void(double)> set;
};

NumberField MakeNumber(wxWindow *parent, double value, OptionAddArgs const& args, bool integer, std::function<void(double)> changed) {
	FlatFrame *frame;
	auto text = MakeText(parent, frame, "", 80);
	auto format = [integer](double v) {
		return integer ? wxString::Format("%d", static_cast<int>(std::lround(v))) : wxString::FromCDouble(v);
	};
	auto clamp = [=](double v) {
		v = std::clamp(v, args.min, args.max);
		return integer ? std::round(v) : v;
	};
	auto current = std::make_shared<double>(value);
	text->ChangeValue(format(value));
	// Taken as it is typed, when it is a number in range
	text->Bind(wxEVT_TEXT, [=](wxCommandEvent &event) {
		event.Skip();
		double v;
		if (!text->GetValue().ToCDouble(&v) || v < args.min || v > args.max) return;
		if (integer && v != std::round(v)) return;
		*current = v;
		changed(v);
	});
	// Leaving it shows what was taken
	text->Bind(wxEVT_KILL_FOCUS, [=](wxFocusEvent &event) {
		event.Skip();
		text->ChangeValue(format(*current));
	});
	frame->SetWheel([=](int steps) {
		double v = clamp(*current + steps * args.inc);
		if (v == *current) return;
		*current = v;
		text->ChangeValue(format(v));
		changed(v);
	});
	return {frame, [=](double v) { *current = clamp(v); text->ChangeValue(format(*current)); }};
}

/// A thin line: under a section's title, or between the list of pages and them
class Divider final : public wxWindow {
public:
	Divider(wxWindow *parent, bool vertical = false) : wxWindow(parent, -1) {
		int thick = std::max(1, FromDIP(1));
		SetMinSize(vertical ? wxSize(thick, -1) : wxSize(-1, thick));
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
			wxPaintDC dc(this);
			auto p = flat::Colours(this);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(theme::Mix(p.surface, p.text, 0.15)));
			dc.DrawRectangle(wxPoint(0, 0), GetClientSize());
		});
	}
	bool AcceptsFocus() const override { return false; }
};

/// @p value as a new option of its type named @p name
std::unique_ptr<agi::OptionValue> Copy(agi::OptionValue const& value) {
	auto const& name = value.GetName();
	switch (value.GetType()) {
		case agi::OptionType::Bool: return std::make_unique<agi::OptionValueBool>(name, value.GetBool());
		case agi::OptionType::Int: return std::make_unique<agi::OptionValueInt>(name, value.GetInt());
		case agi::OptionType::Double: return std::make_unique<agi::OptionValueDouble>(name, value.GetDouble());
		case agi::OptionType::String: return std::make_unique<agi::OptionValueString>(name, value.GetString());
		case agi::OptionType::Color: return std::make_unique<agi::OptionValueColor>(name, value.GetColor());
		default: return nullptr;
	}
}

/// The default of the option @p option, as an option of its own
std::unique_ptr<agi::OptionValue> DefaultOf(agi::OptionValue const& option) {
	auto const& name = option.GetName();
	switch (option.GetType()) {
		case agi::OptionType::Bool: return std::make_unique<agi::OptionValueBool>(name, static_cast<agi::OptionValueBool const&>(option).GetDefaultValue());
		case agi::OptionType::Int: return std::make_unique<agi::OptionValueInt>(name, static_cast<agi::OptionValueInt const&>(option).GetDefaultValue());
		case agi::OptionType::Double: return std::make_unique<agi::OptionValueDouble>(name, static_cast<agi::OptionValueDouble const&>(option).GetDefaultValue());
		case agi::OptionType::String: return std::make_unique<agi::OptionValueString>(name, static_cast<agi::OptionValueString const&>(option).GetDefaultValue());
		case agi::OptionType::Color: return std::make_unique<agi::OptionValueColor>(name, static_cast<agi::OptionValueColor const&>(option).GetDefaultValue());
		default: return nullptr;
	}
}

/// Whether @p value is the default of @p option
bool IsDefault(agi::OptionValue const& value, agi::OptionValue const& option) {
	auto fallback = DefaultOf(option);
	if (!fallback || value.GetType() != option.GetType()) return true;
	switch (option.GetType()) {
		case agi::OptionType::Bool: return value.GetBool() == fallback->GetBool();
		case agi::OptionType::Int: return value.GetInt() == fallback->GetInt();
		case agi::OptionType::Double: return value.GetDouble() == fallback->GetDouble();
		case agi::OptionType::String: return value.GetString() == fallback->GetString();
		case agi::OptionType::Color: return value.GetColor() == fallback->GetColor();
		default: return true;
	}
}

/// Whether the check box @p window is ticked
bool Checked(wxWindow *window) {
	if (auto check = dynamic_cast<FlatCheck*>(window)) return check->GetValue();
	if (auto check = dynamic_cast<wxCheckBox*>(window)) return check->GetValue();
	return false;
}
}

// PreferencesBook

/// The list of pages: names, the one shown marked, and while searching how
/// many matches each has
class PreferencesBook::Nav final : public wxWindow {
	struct Item { wxString name; bool sub; };
	std::vector<Item> items;
	std::vector<size_t> matches;
	int selection = -1, hover = -1;
	agi::signal::Connection theme_changed;

	int RowHeight() const { return GetCharHeight() + FromDIP(12); }
	int At(int y) const {
		int row = (y - FromDIP(4)) / RowHeight();
		return y >= FromDIP(4) && row < static_cast<int>(items.size()) ? row : -1;
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC buffered(this);
		wxGCDC dc(buffered);
		auto p = flat::Colours(this);
		wxSize size = GetClientSize();
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(p.surface));
		dc.DrawRectangle(wxPoint(0, 0), size);
		wxFont font = GetFont(), bold = GetFont().Bold();
		bool searching = !matches.empty();
		for (size_t i = 0; i < items.size(); ++i) {
			wxRect row(FromDIP(4), FromDIP(4) + RowHeight() * static_cast<int>(i), size.x - FromDIP(8), RowHeight());
			bool chosen = static_cast<int>(i) == selection;
			size_t count = i < matches.size() ? matches[i] : 0;
			if (chosen || static_cast<int>(i) == hover) {
				dc.SetBrush(wxBrush(chosen ? theme::Mix(p.surface, p.accent, 0.14) : theme::Mix(p.surface, p.text, 0.05)));
				dc.DrawRoundedRectangle(row, FromDIP(5));
			}
			if (chosen) {
				dc.SetBrush(wxBrush(p.accent));
				dc.DrawRoundedRectangle(wxRect(row.x, row.y + FromDIP(6), FromDIP(3), row.height - FromDIP(12)), FromDIP(1));
			}
			dc.SetFont(chosen || (searching && count) ? bold : font);
			dc.SetTextForeground(searching && !count ? p.dim : chosen ? p.accent : p.text);
			int x = row.x + FromDIP(items[i].sub ? 26 : 12);
			int right = row.GetRight() - FromDIP(8);
			if (searching && count) {
				// How many matches, in a pill on the right
				dc.SetFont(font);
				wxString number = wxString::Format("%zu", count);
				wxSize extent = dc.GetTextExtent(number);
				wxRect pill(right - extent.x - FromDIP(12), row.y + (row.height - extent.y - FromDIP(4)) / 2, extent.x + FromDIP(12), extent.y + FromDIP(4));
				dc.SetBrush(wxBrush(theme::Mix(p.surface, p.accent, 0.2)));
				dc.DrawRoundedRectangle(pill, pill.height / 2);
				dc.SetTextForeground(p.accent);
				dc.DrawText(number, pill.x + FromDIP(6), pill.y + FromDIP(2));
				right = pill.x - FromDIP(4);
				dc.SetFont(chosen || count ? bold : font);
				dc.SetTextForeground(chosen ? p.accent : p.text);
			}
			wxString shown = items[i].name;
			while (shown.size() > 1 && dc.GetTextExtent(shown).x > right - x) shown = shown.Left(shown.size() - 2) + wxString::FromUTF8("…");
			dc.DrawText(shown, x, row.y + (row.height - dc.GetCharHeight()) / 2);
		}
	}

public:
	PreferencesBook *book;

	Nav(PreferencesBook *book) : wxWindow(book, -1, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS), book(book) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &Nav::OnPaint, this);
		Bind(wxEVT_MOTION, [this](wxMouseEvent &e) {
			int now = At(e.GetY());
			if (now != hover) { hover = now; Refresh(false); }
		});
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = -1; Refresh(false); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
			SetFocus();
			int row = At(e.GetY());
			if (row >= 0) this->book->SetSelection(row);
		});
		Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &e) {
			int key = e.GetKeyCode();
			if (key == WXK_UP && selection > 0) this->book->SetSelection(selection - 1);
			else if (key == WXK_DOWN && selection + 1 < static_cast<int>(items.size())) this->book->SetSelection(selection + 1);
			else e.Skip();
		});
		Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
		theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	}

	void Add(wxString const& name, bool sub) {
		items.push_back({name, sub});
		InvalidateBestSize();
	}
	void Select(int index) { selection = index; Refresh(false); }
	void SetMatches(std::vector<size_t> const& counts) { matches = counts; Refresh(false); }
	std::vector<size_t> const& Matches() const { return matches; }

	wxSize DoGetBestClientSize() const override {
		wxClientDC dc(const_cast<Nav*>(this));
		dc.SetFont(GetFont().Bold());
		int widest = 0;
		for (auto const& item : items)
			widest = std::max(widest, dc.GetTextExtent(item.name).x + FromDIP(item.sub ? 26 : 12));
		// Room for a count while searching
		return wxSize(std::max(FromDIP(170), widest + FromDIP(52)), FromDIP(8) + RowHeight() * static_cast<int>(items.size()));
	}
	bool AcceptsFocusFromKeyboard() const override { return true; }
};

PreferencesBook::PreferencesBook(wxWindow *parent)
: wxPanel(parent, -1)
{
	theme::UseSurface(this);
	nav = new Nav(this);
	pages = new wxSimplebook(this, -1);
	theme::UseSurface(pages);
	nav_column = new wxBoxSizer(wxVERTICAL);
	nav_column->Add(nav, wxSizerFlags(1).Expand());
	auto row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(nav_column, wxSizerFlags().Expand().Border(wxRIGHT, FromDIP(8)));
	row->Add(new Divider(this, true), wxSizerFlags().Expand());
	row->Add(pages, wxSizerFlags(1).Expand().Border(wxLEFT, FromDIP(16)));
	SetSizer(row);
}

wxWindow *PreferencesBook::PageParent() const { return pages; }

void PreferencesBook::AddPage(wxWindow *page, wxString const& name, bool sub) {
	pages->AddPage(page, name, false);
	nav->Add(name, sub);
	if (pages->GetPageCount() == 1) ChangeSelection(0);
}

size_t PreferencesBook::GetPageCount() const { return pages->GetPageCount(); }
wxWindow *PreferencesBook::GetPage(size_t page) const { return pages->GetPage(page); }
wxString PreferencesBook::GetPageText(size_t page) const { return pages->GetPageText(page); }
int PreferencesBook::GetSelection() const { return pages->GetSelection(); }

void PreferencesBook::ChangeSelection(size_t page) {
	if (page >= pages->GetPageCount()) page = 0;
	pages->ChangeSelection(page);
	nav->Select(static_cast<int>(page));
}

void PreferencesBook::SetSelection(size_t page) {
	if (page >= pages->GetPageCount()) return;
	ChangeSelection(page);
	if (on_change) on_change(page);
}

void PreferencesBook::AddAboveList(wxWindow *window) {
	nav_column->Insert(nav_column->GetItemCount() - 1, window, wxSizerFlags().Expand().Border(wxBOTTOM | wxLEFT | wxRIGHT, FromDIP(4)));
}

void PreferencesBook::SetMatches(std::vector<size_t> const& matches) {
	nav->SetMatches(matches);
}

wxString PreferencesBook::MatchText(size_t page) const {
	auto const& matches = nav->Matches();
	return page < matches.size() && matches[page] ? wxString::Format("%zu", matches[page]) : wxString();
}

// OptionPage

/// Before a setting's name: a dot when it isn't its default, which turns to
/// ↺ under the pointer and restores the default when clicked
class OptionPage::Mark final : public wxWindow {
	OptionPage *page;
	std::string option;
	bool hover = false;

public:
	Mark(wxWindow *parent, OptionPage *page, std::string option)
	: wxWindow(parent, -1), page(page), option(std::move(option))
	{
		SetMinSize(FromDIP(wxSize(14, 14)));
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
			wxAutoBufferedPaintDC buffered(this);
			wxGCDC dc(buffered);
			auto p = flat::Colours(this);
			wxSize size = GetClientSize();
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
			dc.DrawRectangle(wxPoint(0, 0), size);
			if (!Modified()) return;
			wxPoint centre(size.x / 2, size.y / 2);
			if (!hover) {
				dc.SetBrush(wxBrush(p.accent));
				dc.DrawCircle(centre, FromDIP(3));
				return;
			}
			// ↺ : back to the default
			int r = FromDIP(5);
			wxPen pen(p.accent, std::max(1, FromDIP(3) / 2));
			pen.SetCap(wxCAP_ROUND);
			dc.SetPen(pen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawEllipticArc(centre.x - r, centre.y - r, 2 * r, 2 * r, -60, 200);
			// The arrow's head at the arc's upper left end
			double end = 200 * std::numbers::pi / 180;
			wxPoint tip(centre.x + static_cast<int>(r * std::cos(end)), centre.y - static_cast<int>(r * std::sin(end)));
			int head = FromDIP(3);
			dc.DrawLine(tip.x, tip.y, tip.x - head, tip.y - head);
			dc.DrawLine(tip.x, tip.y, tip.x + head, tip.y - head);
		});
		Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hover = true; Refresh(false); });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = false; Refresh(false); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
			if (Modified()) this->page->Reset(this->option);
		});
		Update();
	}

	bool Modified() const { return page->IsModified(option); }

	/// Show whether the setting is changed, and what a click does
	void Update() {
		bool modified = Modified();
		SetToolTip(modified ? _("Changed from the default: click to restore the default") : wxString());
		SetCursor(modified ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
		Refresh(false);
	}

	bool AcceptsFocus() const override { return false; }
};

OptionPage::OptionPage(PreferencesBook *book, Preferences *parent, wxString name, int style)
: wxScrolled<wxPanel>(book->PageParent(), -1, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
, sizer(new wxBoxSizer(wxVERTICAL))
, parent(parent)
{
	book->AddPage(this, name, style & PAGE_SUB);
	// Every page scrolls when the window is too short for it
	SetScrollRate(0, FromDIP(20));
	DisableKeyboardScrolling();
	// The drawn controls fill in around them with this colour
	theme::UseSurface(this);
}

OptionPage::Mark *OptionPage::AddMark(PageSection section, std::string const& option) {
	auto mark = new Mark(section.box, this, option);
	section.sizer->Add(mark, 0, wxALIGN_CENTRE_VERTICAL);
	return mark;
}

void OptionPage::AddRow(std::string const& option, Mark *mark, wxWindow *control, std::function<void(agi::OptionValue const&)> show) {
	rows.push_back({option, mark, control, std::move(show)});
}

wxSizer *OptionPage::Label(PageSection section, wxWindow *name, wxString const& help) {
	auto column = new wxBoxSizer(wxVERTICAL);
	column->Add(name);
	if (!help.empty()) {
		auto text = new wxStaticText(section.box, -1, help);
		text->SetForegroundColour(flat::Colours(this).dim);
		text->Wrap(FromDIP(380));
		// Under a check box's words, not its box
		int indent = dynamic_cast<FlatCheck*>(name) ? FromDIP(20) : 0;
		column->AddSpacer(FromDIP(2));
		column->Add(text, 0, wxLEFT, indent);
	}
	section.sizer->Add(column, 1, wxALIGN_CENTRE_VERTICAL);
	return column;
}

template<class T>
OptionPage::Mark *OptionPage::Add(PageSection section, wxString const& label, T *control, std::string const& option, wxString const& help) {
	Mark *mark = nullptr;
	if (!option.empty()) mark = AddMark(section, option);
	else section.sizer->AddSpacer(0);
	Label(section, new wxStaticText(section.box, -1, label), help);
	// In a column as wide on every page
	auto column = new wxBoxSizer(wxHORIZONTAL);
	column->SetMinSize(wxSize(FromDIP(control_width), -1));
	column->Add(control, wxSizerFlags(1).Expand());
	section.sizer->Add(column, wxSizerFlags().Expand().CentreVertical());
	return mark;
}

void OptionPage::CellSkip(PageSection) {
	// Each setting has a row of its own now: nothing to skip
}

void OptionPage::Note(PageSection section, wxString const& text) {
	section.sizer->AddSpacer(0);
	auto note = new wxStaticText(section.box, -1, text);
	note->SetForegroundColour(flat::Colours(this).dim);
	note->Wrap(FromDIP(420));
	section.sizer->Add(note, 0, wxALIGN_CENTRE_VERTICAL);
	section.sizer->AddSpacer(0);
}

wxWindow *OptionPage::OptionAdd(PageSection section, const wxString &name, const char *opt_name, OptionAddArgs kwargs) {
	parent->AddChangeableOption(opt_name);
	const auto opt = OPT_GET(opt_name);
	std::string option = opt_name;
	auto parent = this->parent;

	switch (opt->GetType()) {
		case agi::OptionType::Bool: {
			// The label is the check box's own: clicking it ticks
			auto mark = AddMark(section, option);
			auto cb = new FlatCheck(section.box, name);
			Label(section, cb, kwargs.help);
			section.sizer->AddSpacer(0);
			cb->SetValue(opt->GetBool());
			cb->Bind(wxEVT_CHECKBOX, BoolUpdater(opt_name, parent));
			AddRow(option, mark, cb, [=](agi::OptionValue const& v) {
				// Through its event: what depends on it follows
				if (cb->GetValue() != v.GetBool()) cb->Toggle();
				else parent->SetOption(Copy(v));
			});
			return cb;
		}

		case agi::OptionType::Int:
		case agi::OptionType::Double: {
			bool integer = opt->GetType() == agi::OptionType::Int;
			auto changed = [=](double v) {
				if (integer) parent->SetOption(std::make_unique<agi::OptionValueInt>(option, static_cast<int64_t>(std::lround(v))));
				else parent->SetOption(std::make_unique<agi::OptionValueDouble>(option, v));
			};
			auto field = MakeNumber(section.box, integer ? opt->GetInt() : opt->GetDouble(), kwargs, integer, changed);
			auto mark1 = Add(section, name, field.frame, option, kwargs.help);
			AddRow(option, mark1, field.frame, [=](agi::OptionValue const& v) {
				double value = integer ? v.GetInt() : v.GetDouble();
				field.set(value);
				changed(value);
			});
			return field.frame;
		}

		case agi::OptionType::String: {
			FlatFrame *frame;
			auto text = MakeText(section.box, frame, to_wx(opt->GetString()));
			text->Bind(wxEVT_TEXT, StringUpdater(opt_name, parent));
			auto mark2 = Add(section, name, frame, option, kwargs.help);
			AddRow(option, mark2, frame, [=](agi::OptionValue const& v) {
				text->ChangeValue(to_wx(v.GetString()));
				parent->SetOption(Copy(v));
			});
			return frame;
		}

		case agi::OptionType::Color: {
			auto cb = new ColourButton(section.box, wxSize(40,10), kwargs.alpha, opt->GetColor());
			cb->Bind(EVT_COLOR, ColourUpdater(opt_name, parent));
			auto mark = AddMark(section, option);
			Label(section, new wxStaticText(section.box, -1, name), kwargs.help);
			section.sizer->Add(cb, wxSizerFlags().CentreVertical());
			AddRow(option, mark, cb, [=](agi::OptionValue const& v) {
				cb->SetColor(v.GetColor());
				parent->SetOption(Copy(v));
			});
			return cb;
		}

		default:
			throw agi::InternalError("Unsupported type");
	}
}

void OptionPage::OptionChoice(PageSection section, const wxString &name, const wxArrayString &choices, const char *opt_name, bool translate, wxString const& help) {
	parent->AddChangeableOption(opt_name);
	const auto opt = OPT_GET(opt_name);
	std::string option = opt_name;

	std::vector<wxString> shown;
	for (auto const& choice : choices) shown.push_back(translate ? wxGetTranslation(choice) : choice);

	auto list = new FlatChoice(section.box, name);
	list->Set(shown);
	auto mark3 = Add(section, name, list, option, help);

	auto parent = this->parent;
	switch (opt->GetType()) {
		case agi::OptionType::Int: {
			auto select = [=](int64_t val) {
				if (!shown.empty()) list->SetStringSelection(shown[val >= 0 && val < (int64_t)shown.size() ? val : 0]);
			};
			select(opt->GetInt());
			list->on_choose = [=](int index) {
				parent->SetOption(std::make_unique<agi::OptionValueInt>(option, index));
			};
			AddRow(option, mark3, list, [=](agi::OptionValue const& v) {
				select(v.GetInt());
				parent->SetOption(Copy(v));
			});
			break;
		}
		case agi::OptionType::String: {
			auto select = [=](std::string const& value) {
				wxString val(to_wx(value));
				if (translate) val = wxGetTranslation(val);
				if (std::find(shown.begin(), shown.end(), val) != shown.end())
					list->SetStringSelection(val);
				else if (!shown.empty())
					list->SetStringSelection(shown.front());
			};
			select(opt->GetString());
			list->on_choose = [=](int index) {
				parent->SetOption(std::make_unique<agi::OptionValueString>(option, from_wx(choices[index])));
			};
			AddRow(option, mark3, list, [=](agi::OptionValue const& v) {
				select(v.GetString());
				parent->SetOption(Copy(v));
			});
			break;
		}

		default:
			throw agi::InternalError("Unsupported type");
	}
}

PageSection OptionPage::PageSizer(wxString name) {
	// A title over a line, then a row for each setting: a mark when it is
	// changed, its name, and its control in a column on the right
	auto title = new wxStaticText(this, -1, name);
	title->SetFont(title->GetFont().Bold());
	if (!sizer->IsEmpty()) sizer->AddSpacer(FromDIP(18));
	sizer->Add(title, 0, wxLEFT | wxRIGHT, FromDIP(4));
	sizer->Add(new Divider(this), 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));
	auto flex = new wxFlexGridSizer(3, FromDIP(8), FromDIP(4));
	flex->AddGrowableCol(1, 1);
	sizer->Add(flex, 0, wxEXPAND | wxRIGHT, FromDIP(4));
	return {flex, this};
}

void OptionPage::OptionBrowse(PageSection section, const wxString &name, const char *opt_name, wxWindow *enabler, bool do_enable) {
	parent->AddChangeableOption(opt_name);
	const auto opt = OPT_GET(opt_name);

	if (opt->GetType() != agi::OptionType::String)
		throw agi::InternalError("Option must be agi::OptionType::String for BrowseButton.");

	FlatFrame *frame;
	auto text = MakeText(section.box, frame, to_wx(opt->GetString()));
	text->Bind(wxEVT_TEXT, StringUpdater(opt_name, parent));

	auto browse = new FlatButton(section.box, _("Browse..."));
	browse->Bind(wxEVT_BUTTON, std::bind(browse_button, text));

	auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
	button_sizer->Add(frame, wxSizerFlags(1).Expand());
	button_sizer->Add(browse, wxSizerFlags().Expand().Border(wxLEFT, FromDIP(4)));

	auto mark4 = Add(section, name, button_sizer, opt_name, {});
	auto parent = this->parent;
	AddRow(opt_name, mark4, frame, [=](agi::OptionValue const& v) {
		text->ChangeValue(to_wx(v.GetString()));
		parent->SetOption(Copy(v));
	});

	if (enabler) {
		if (do_enable) {
			EnableIfChecked(enabler, frame);
			EnableIfChecked(enabler, browse);
		}
		else {
			DisableIfChecked(enabler, frame);
			DisableIfChecked(enabler, browse);
		}
	}
}

void OptionPage::OptionFont(PageSection section, std::string opt_prefix) {
	const auto face_opt = OPT_GET(opt_prefix + "Font Face");
	const auto size_opt = OPT_GET(opt_prefix + "Font Size");
	std::string face_name = face_opt->GetName(), size_name = size_opt->GetName();

	parent->AddChangeableOption(face_name);
	parent->AddChangeableOption(size_name);

	FlatFrame *frame;
	auto font_name = MakeText(section.box, frame, to_wx(face_opt->GetString()));
	font_name->Bind(wxEVT_TEXT, StringUpdater(face_name.c_str(), parent));
	font_name->SetHint(wxNORMAL_FONT->GetFaceName());

	auto parent = this->parent;
	auto set_size = [=](double v) {
		parent->SetOption(std::make_unique<agi::OptionValueInt>(size_name, static_cast<int64_t>(std::lround(v))));
	};
	auto font_size = MakeNumber(section.box, size_opt->GetInt(), {.min = 3, .max = 42}, true, set_size);

	auto pick_btn = new FlatButton(section.box, _("Choose..."));
	auto current_size = [size = font_size.frame] {
		double v = 0;
		static_cast<wxTextCtrl*>(size->GetContent())->GetValue().ToCDouble(&v);
		return static_cast<int>(v);
	};
	pick_btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
		wxFont font = *wxNORMAL_FONT;
		wxString fontname = font_name->GetValue();
		if (!fontname.empty()) font.SetFaceName(fontname);
		if (current_size() > 0) font.SetPointSize(current_size());
		font = wxGetFontFromUser(parent, font);
		if (!font.IsOk()) return;
		font_name->SetValue(font.GetFaceName());
		font_size.set(font.GetPointSize());
		set_size(font.GetPointSize());
	});

	auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
	button_sizer->Add(frame, wxSizerFlags(1).Expand());
	button_sizer->Add(pick_btn, wxSizerFlags().Expand().Border(wxLEFT, FromDIP(4)));

	auto mark5 = Add(section, _("Font Face"), button_sizer, face_name, {});
	AddRow(face_name, mark5, frame, [=](agi::OptionValue const& v) {
		font_name->ChangeValue(to_wx(v.GetString()));
		parent->SetOption(Copy(v));
	});
	auto mark6 = Add(section, _("Font Size"), font_size.frame, size_name, {});
	AddRow(size_name, mark6, font_size.frame, [=](agi::OptionValue const& v) {
		font_size.set(v.GetInt());
		set_size(v.GetInt());
	});
}

void OptionPage::EnableIfChecked(wxWindow *cbx, wxWindow *ctrl) {
	if (!dynamic_cast<FlatCheck*>(cbx) && !dynamic_cast<wxCheckBox*>(cbx)) return;
	ctrl->Enable(Checked(cbx));
	cbx->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent& evt) { ctrl->Enable(!!evt.GetInt()); evt.Skip(); });
}

void OptionPage::DisableIfChecked(wxWindow *cbx, wxWindow *ctrl) {
	if (!dynamic_cast<FlatCheck*>(cbx) && !dynamic_cast<wxCheckBox*>(cbx)) return;
	ctrl->Enable(!Checked(cbx));
	cbx->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent& evt) { ctrl->Enable(!evt.GetInt()); evt.Skip(); });
}

bool OptionPage::IsModified(std::string const& option) const {
	auto live = OPT_GET(option);
	auto pending = parent->Pending(option);
	return !::IsDefault(pending ? *pending : *live, *live);
}

void OptionPage::Reset(std::string const& option) {
	auto fallback = DefaultOf(*OPT_GET(option));
	if (!fallback) return;
	for (auto const& row : rows)
		if (row.option == option) row.show(*fallback);
	RefreshMarks();
}

void OptionPage::ResetAll() {
	for (auto const& option : Options())
		if (IsModified(option)) Reset(option);
}

std::vector<std::string> OptionPage::Options() const {
	std::vector<std::string> options;
	for (auto const& row : rows)
		if (std::find(options.begin(), options.end(), row.option) == options.end())
			options.push_back(row.option);
	return options;
}

wxWindow *OptionPage::ControlOf(std::string const& option) const {
	for (auto const& row : rows)
		if (row.option == option) return row.control;
	return nullptr;
}

void OptionPage::RefreshMarks() {
	for (auto const& row : rows)
		if (row.mark) row.mark->Update();
}

void OptionPage::ScrollTo(wxWindow *window) {
	int unit = 0;
	GetScrollPixelsPerUnit(nullptr, &unit);
	if (!unit || !window) return;
	wxPoint at = CalcUnscrolledPosition(ScreenToClient(window->GetScreenPosition()));
	Scroll(-1, std::max(0, (at.y - FromDIP(48)) / unit));
}
