// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "flat_dialog.h"

#include "flat_controls.h"
#include "help_button.h"

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cmath>

namespace {
/// The enabled, shown FlatButton with @p id in @p dialog
FlatButton *FindButton(wxWindow *window, int id) {
	for (auto child : window->GetChildren()) {
		if (auto button = dynamic_cast<FlatButton*>(child); button && button->GetId() == id && button->IsEnabled() && button->IsShown())
			return button;
		if (child->IsTopLevel()) continue;
		if (auto found = FindButton(child, id)) return found;
	}
	return nullptr;
}

/// The button Escape and the close button press, as wxDialog finds it
FlatButton *EscapeButton(wxDialog *dialog) {
	int id = dialog->GetEscapeId();
	if (id == wxID_NONE) return nullptr;
	if (id != wxID_ANY) return FindButton(dialog, id);
	for (int candidate : std::initializer_list<int>{wxID_CANCEL, wxID_CLOSE, dialog->GetAffirmativeId()})
		if (auto button = FindButton(dialog, candidate)) return button;
	return nullptr;
}

/// Whether Enter in @p focus is for the dialog's affirmative button rather
/// than for the control: a one-line field taking no Enter of its own, a
/// check box, or nothing in particular
bool EnterIsForDialog(wxWindow *focus, wxDialog *dialog) {
	if (!focus || focus == dialog) return true;
	if (wxGetTopLevelParent(focus) != dialog) return false;
	if (auto text = dynamic_cast<wxTextCtrl*>(focus))
		return !text->IsMultiLine() && !text->HasFlag(wxTE_PROCESS_ENTER);
	if (dynamic_cast<wxCheckBox*>(focus) || dynamic_cast<FlatCheck*>(focus)) return true;
	return typeid(*focus) == typeid(wxPanel);
}

/// The FlatButton whose underlined letter is @p key
FlatButton *MnemonicButton(wxWindow *window, int key) {
	for (auto child : window->GetChildren()) {
		if (child->IsTopLevel()) continue;
		if (auto button = dynamic_cast<FlatButton*>(child); button && button->IsEnabled() && button->IsShownOnScreen()) {
			wxString label = button->GetLabel();
			for (size_t i = 0; i + 1 < label.size(); ++i) {
				if (label[i] != '&') continue;
				if (label[i + 1] == '&') { ++i; continue; }
				if (wxToupper(label[i + 1]) == wxToupper(key)) return button;
				break;
			}
		}
		if (auto found = MnemonicButton(child, key)) return found;
	}
	return nullptr;
}
}

namespace flat {

void UseFlatButtonKeys(wxDialog *dialog) {
	dialog->Bind(wxEVT_CHAR_HOOK, [dialog](wxKeyEvent &event) {
		int key = event.GetKeyCode();
		if (key == WXK_ESCAPE) {
			if (auto button = EscapeButton(dialog)) {
				button->Press();
				return;
			}
		}
		else if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && !event.HasAnyModifiers()) {
			if (EnterIsForDialog(wxWindow::FindFocus(), dialog)) {
				if (auto button = FindButton(dialog, dialog->GetAffirmativeId())) {
					button->Press();
					return;
				}
			}
		}
		else if (event.GetModifiers() == wxMOD_ALT && key < 128 && wxIsalnum(key)) {
			if (auto button = MnemonicButton(dialog, key)) {
				button->Press();
				return;
			}
		}
		event.Skip();
	});
	// The title bar's close button, as wxDialog has it press Cancel
	dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent &event) {
		auto button = dialog->IsShown() ? EscapeButton(dialog) : nullptr;
		if (button && event.CanVeto()) button->Press();
		else event.Skip();
	});
}

DialogButtons::DialogButtons(wxDialog *dialog, long flags, const char *help_page)
: wxBoxSizer(wxHORIZONTAL)
, dialog(dialog)
, left(new wxBoxSizer(wxHORIZONTAL))
, right(new wxBoxSizer(wxHORIZONTAL))
{
	Add(left, 0, wxALIGN_CENTER_VERTICAL);
	AddStretchSpacer(1);
	Add(right, 0, wxALIGN_CENTER_VERTICAL);

	if (flags & wxHELP) {
		auto help = Make(wxID_HELP, "", false);
		left->Add(help);
		if (help_page) {
			std::string page = help_page;
			help->Bind(wxEVT_BUTTON, [page](wxCommandEvent &) { HelpButton::OpenPage(page.c_str()); });
		}
	}

	// The affirmative button last on the right, as GTK and macOS have it;
	// first, as Windows has it
	struct Standard { long flag; int id; bool accent; };
#ifdef __WXMSW__
	const Standard order[] = {{wxYES, wxID_YES, true}, {wxOK, wxID_OK, true}, {wxNO, wxID_NO, false},
	                          {wxCANCEL, wxID_CANCEL, false}, {wxCLOSE, wxID_CLOSE, false}, {wxAPPLY, wxID_APPLY, false}};
#else
	const Standard order[] = {{wxAPPLY, wxID_APPLY, false}, {wxCLOSE, wxID_CLOSE, false}, {wxCANCEL, wxID_CANCEL, false},
	                          {wxNO, wxID_NO, false}, {wxOK, wxID_OK, true}, {wxYES, wxID_YES, true}};
#endif
	for (auto const& standard : order) {
		if (!(flags & standard.flag)) continue;
		// Close alone is what the dialog is for
		bool accent = standard.accent || (standard.id == wxID_CLOSE && !(flags & (wxOK | wxYES)));
		right->Add(Make(standard.id, "", accent), 0, right->IsEmpty() ? 0 : wxLEFT, dialog->FromDIP(6));
	}
	// As CreateStdDialogButtonSizer has them: Close alone accepts and ends
	if (flags & wxOK) dialog->SetAffirmativeId(wxID_OK);
	else if (flags & wxYES) dialog->SetAffirmativeId(wxID_YES);
	else if (flags & wxCLOSE) dialog->SetAffirmativeId(wxID_CLOSE);
	UseFlatButtonKeys(dialog);
}

FlatButton *DialogButtons::Make(int id, wxString const& label, bool accent) {
	auto button = new FlatButton(dialog, label, id, accent);
	button->SetMinSize(wxSize(std::max(dialog->FromDIP(76), button->GetBestSize().x), dialog->FromDIP(28)));
	buttons.push_back(button);
	return button;
}

FlatButton *DialogButtons::Get(int id) const {
	for (auto button : buttons)
		if (button->GetId() == id) return button;
	return nullptr;
}

FlatButton *DialogButtons::AddLeft(wxString const& label, int id) {
	auto button = Make(id, label, false);
	left->Add(button, 0, left->IsEmpty() ? 0 : wxLEFT, dialog->FromDIP(6));
	return button;
}

FlatButton *DialogButtons::AddRight(wxString const& label, int id, bool accent) {
	auto button = Make(id, label, accent);
	right->Insert(0, button, 0, wxRIGHT, dialog->FromDIP(6));
	return button;
}

wxString FormatNumber(double v, int precision) {
	double unit = std::pow(10.0, precision);
	v = std::round(v * unit) / unit;
	if (v == 0) v = 0; // no "-0"
	return wxString::FromCDouble(v);
}

NumberField *NumberField::Create(wxWindow *parent, wxString const& label, double value, double min, double max, double step, int precision, int width) {
	auto field = new NumberField(parent, label, value, min, max, step, precision, width);
	field->frame->Bind(wxEVT_DESTROY, [field](wxWindowDestroyEvent &event) {
		event.Skip();
		if (event.GetEventObject() == field->frame) delete field;
	});
	return field;
}

NumberField::NumberField(wxWindow *parent, wxString const& label, double value, double min, double max, double step, int precision, int width)
: frame(new FlatFrame(parent, label))
, text(new wxTextCtrl(frame, -1, FormatNumber(value, precision), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE))
, value(value)
, min(min)
, max(max)
, step(step)
, precision(precision)
{
	frame->SetContent(text, width);
	text->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
		event.Skip();
		double v;
		if (text->GetValue().ToCDouble(&v) && v >= this->min && v <= this->max) Take(v, false);
	});
	auto show_taken = [this] { text->ChangeValue(FormatNumber(this->value, this->precision)); };
	text->Bind(wxEVT_KILL_FOCUS, [show_taken](wxFocusEvent &event) { event.Skip(); show_taken(); });
	text->Bind(wxEVT_TEXT_ENTER, [show_taken](wxCommandEvent &) { show_taken(); });
	frame->SetWheel([this](int steps) { Take(this->value + steps * this->step, true); });
	frame->SetScrub([this](int steps, bool final) {
		if (!scrub_start) scrub_start = this->value;
		Take(*scrub_start + steps * this->step, true);
		if (final) scrub_start.reset();
	});
}

void NumberField::Take(double v, bool show) {
	v = std::clamp(v, min, max);
	double unit = std::pow(10.0, precision);
	v = std::round(v * unit) / unit;
	value = v;
	if (show) text->ChangeValue(FormatNumber(v, precision));
	if (on_change) on_change(v);
}

void NumberField::SetValue(double v) {
	value = std::clamp(v, min, max);
	text->ChangeValue(FormatNumber(value, precision));
}

void NumberField::SetRange(double new_min, double new_max) {
	min = new_min;
	max = new_max;
	SetValue(value);
}

void NumberField::SetToolTip(wxString const& tip) {
	frame->SetToolTip(tip);
	text->SetToolTip(tip);
}

bool NumberField::Enable(bool enable) {
	text->Enable(enable);
	return frame->Enable(enable);
}

}
