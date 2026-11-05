// Copyright (c) 2006, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "base_grid.h"

#include "theme.h"

#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/menu.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "audio_box.h"
#include "compat.h"
#include "dialog_rectangle.h"
#include "grid_column.h"
#include "options.h"
#include "project.h"
#include "utils.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subtitle_binding.h"
#include "video_controller.h"

#include <libaegisub/util.h>

#include <algorithm>

#include <wx/tooltip.h>
#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>

// Check menu.h for id range allocation before editing this enum
enum {
	GRID_SCROLLBAR = 1730,
	MENU_SHOW_COL = (wxID_HIGHEST + 1) + 2000 // Needs 15 IDs after this
};

namespace {
/// @p a over @p b with opacity @p alpha.
wxColour Mix(wxColour const& a, wxColour const& b, double alpha) {
	auto channel = [&](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x * alpha + y * (1 - alpha) + 0.5); };
	return wxColour(channel(a.Red(), b.Red()), channel(a.Green(), b.Green()), channel(a.Blue(), b.Blue()));
}
}

BaseGrid::BaseGrid(wxWindow* parent, agi::Context *context)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxSUNKEN_BORDER)
, scrollBar(new wxScrollBar(this, GRID_SCROLLBAR, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL))
, context(context)
, columns(GetGridColumns())
, columns_visible(OPT_GET("Subtitle/Grid/Column")->GetListBool())
, seek_listener(context->videoController->AddSeekListener(&BaseGrid::OnSeek, this))
{
	scrollBar->SetScrollbar(0,10,100,10);

	auto scrollbarpositioner = new wxBoxSizer(wxHORIZONTAL);
	scrollbarpositioner->AddStretchSpacer();
	scrollbarpositioner->Add(scrollBar, 0, wxEXPAND, 0);

	SetSizerAndFit(scrollbarpositioner);

	SetBackgroundStyle(wxBG_STYLE_PAINT);

	for (size_t i : agi::util::range(std::min(columns_visible.size(), columns.size()))) {
		if (!columns_visible[i])
			columns[i]->SetVisible(false);
	}

	UpdateStyle();
	OnHighlightVisibleChange(*OPT_GET("Subtitle/Grid/Highlight Subtitles in Frame"));

	connections = agi::signal::make_vector({
		context->subtitleAccess->Read().AddCommitListener("base_grid.AddCommitListener", [this](int type, AssDialogue const*, SubtitleModelRebindTicket ticket) { OnSubtitlesCommit(type); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)),

		context->selectionController->AddActiveLineListener("base_grid.AddActiveLineListener", [this](SubtitleModelRebindTicket ticket) { OnActiveLineChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)),
		context->selectionController->AddSelectionListener("base_grid.AddSelectionListener", [&](SubtitleModelRebindTicket ticket){ Refresh(false); if (ticket.Current()) ticket.Acknowledge(ticket.Revision());}, std::weak_ptr<void const>(model_observer_lifetime)),

		OPT_SUB("Subtitle/Grid/Font Face", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Subtitle/Grid/Font Size", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Active Border", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Background/Background", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Background/Comment", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Background/Inframe", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Background/Selected Comment", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Background/Selection", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Collision", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Header", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Left Column", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Lines", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Selection", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Colour/Subtitle Grid/Standard", &BaseGrid::UpdateStyle, this),
		OPT_SUB("Subtitle/Grid/Alternate Rows", [this] { Refresh(false); }),

		theme::AddChangeListener([this] { UpdateStyle(); }),
		OPT_SUB("Subtitle/Grid/Highlight Subtitles in Frame", &BaseGrid::OnHighlightVisibleChange, this),
		OPT_SUB("Subtitle/Grid/Hide Overrides", [&](agi::OptionValue const&) { Refresh(false); }),
	});

	Bind(wxEVT_CONTEXT_MENU, &BaseGrid::OnContextMenu, this);
}

BaseGrid::~BaseGrid() { }

BEGIN_EVENT_TABLE(BaseGrid,wxWindow)
	EVT_PAINT(BaseGrid::OnPaint)
	EVT_SIZE(BaseGrid::OnSize)
	EVT_COMMAND_SCROLL(GRID_SCROLLBAR,BaseGrid::OnScroll)
	EVT_MOUSE_EVENTS(BaseGrid::OnMouseEvent)
	EVT_KEY_DOWN(BaseGrid::OnKeyDown)
	EVT_CHAR_HOOK(BaseGrid::OnCharHook)
	EVT_MENU_RANGE(MENU_SHOW_COL,MENU_SHOW_COL+15,BaseGrid::OnShowColMenu)
	EVT_DPI_CHANGED(BaseGrid::OnDPIChanged)
END_EVENT_TABLE()

void BaseGrid::OnDPIChanged(wxDPIChangedEvent &e) {
	UpdateStyle();
	e.Skip();
}

void BaseGrid::OnSubtitlesCommit(int type) {
	if (type == AssFile::COMMIT_NEW || (type & AssFile::COMMIT_EXTRADATA)) {
		bool bound = std::any_of(context->subtitleAccess->Read().Events.begin(), context->subtitleAccess->Read().Events.end(), [&](auto const& line) {
			return !GetSubtitleBinding(context->subtitleAccess->Read(), line).empty();
		});
		if (bound) {
			columns[1]->SetVisible(true);
			columns_visible.resize(columns.size(), true);
			columns_visible[1] = true;
		}
	}
	if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_ORDER || type & AssFile::COMMIT_DIAG_ADDREM)
		UpdateMaps();

	if (type & AssFile::COMMIT_DIAG_META) {
		SetColumnWidths();
		Refresh(false);
		return;
	}
	if (type & AssFile::COMMIT_DIAG_TIME)
		Refresh(false);
	else if (type & AssFile::COMMIT_DIAG_TEXT) {
		for (auto const& rect : text_refresh_rects)
			RefreshRect(rect, false);
	}
}

void BaseGrid::OnShowColMenu(wxCommandEvent &event) {
	int item = event.GetId() - MENU_SHOW_COL;
	bool new_value = !columns_visible[item];

	columns_visible.resize(columns.size(), true);
	columns_visible[item] = new_value;
	OPT_SET("Subtitle/Grid/Column")->SetListBool(columns_visible);
	columns[item]->SetVisible(new_value);

	SetColumnWidths();

	Refresh(false);
}

void BaseGrid::OnHighlightVisibleChange(agi::OptionValue const& opt) {
	if (opt.GetBool())
		seek_listener.Unblock();
	else
		seek_listener.Block();
}

void BaseGrid::UpdateStyle() {
	font = *wxNORMAL_FONT;
	wxString fontname = FontFace("Subtitle/Grid");
	if (!fontname.empty()) font.SetFaceName(fontname);
	font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());
	font.SetWeight(wxFONTWEIGHT_NORMAL);

	wxClientDC dc(this);
	dc.SetFont(font);

	// Set line height: the text and 2 pixels above and below it, growing
	// with the display scale.
	GridColumn::row_padding = std::max(0, (FromDIP(4) - 4) / 2);
	lineHeight = dc.GetCharHeight() + 4 + 2 * GridColumn::row_padding;

	// Set row brushes
	row_colors.Default.SetColour(theme::Colour("Colour/Subtitle Grid/Background/Background"));
	row_colors.Header.SetColour(theme::Colour("Colour/Subtitle Grid/Header"));
	row_colors.Selection.SetColour(theme::Colour("Colour/Subtitle Grid/Background/Selection"));
	row_colors.Comment.SetColour(theme::Colour("Colour/Subtitle Grid/Background/Comment"));
	row_colors.Visible.SetColour(theme::Colour("Colour/Subtitle Grid/Background/Inframe"));
	row_colors.SelectedComment.SetColour(theme::Colour("Colour/Subtitle Grid/Background/Selected Comment"));
	row_colors.LeftCol.SetColour(theme::Colour("Colour/Subtitle Grid/Left Column"));
	row_colors.Alternate.SetColour(Mix(theme::Colour("Colour/Subtitle Grid/Standard"), row_colors.Default.GetColour(), theme::IsDark() ? 0.045 : 0.035));

	if (width_helper)
		width_helper->ClearCache();

	SetColumnWidths();

	AdjustScrollbar();
	Refresh(false);
}

void BaseGrid::UpdateMaps() {
	index_line_map.clear();

	for (auto const& curdiag : context->subtitleAccess->Read().Events)
		index_line_map.push_back(&curdiag);

	if (auto active = context->selectionController->ReadActiveLine()) active_row = active->Row;
    else active_row = -1;
    SetColumnWidths();
	AdjustScrollbar();
	Refresh(false);
}

void BaseGrid::OnActiveLineChanged() {
    auto new_active = context->selectionController->ReadActiveLine();
	if (new_active) {
		if (new_active->Row != active_row)
			MakeRowVisible(new_active->Row);
		active_row = new_active->Row;
        extend_id = new_active->Id;
		Refresh(false);
	}
	else
		active_row = -1;
}

void BaseGrid::MakeRowVisible(int row) {
	int h = GetClientSize().GetHeight();

	if (row < yPos + 1)
		ScrollTo(row - 1);
	else if (row > yPos + h/lineHeight - 3)
		ScrollTo(row - h/lineHeight + 3);
}

void BaseGrid::SelectRow(int row, bool addToSelected, bool select) {
	if (row < 0 || (size_t)row >= index_line_map.size()) return;

    auto line = index_line_map.at(row);

	if (!addToSelected) {
		context->selectionController->SetSelectedIDs({line->Id});
		return;
	}

	bool selected = context->selectionController->SelectedIDs().contains(line->Id);
	if (select != selected) {
		auto selection = context->selectionController->SelectedIDs();
		if (select)
			selection.insert(line->Id);
		else
			selection.erase(line->Id);
		context->selectionController->SetSelectedIDs(std::move(selection));
	}
}

void BaseGrid::OnSeek() {
	int lines = GetClientSize().GetHeight() / lineHeight + 1;
	lines = mid(0, lines, GetRows() - yPos);

	auto it = begin(visible_rows);
	for (int i : boost::irange(yPos, yPos + lines)) {
		if (IsDisplayed(index_line_map[i])) {
			if (it == end(visible_rows) || *it != i) {
				Refresh(false);
				return;
			}
			++it;
		}
	}
	if (it != end(visible_rows))
		Refresh(false);
}

void BaseGrid::OnPaint(wxPaintEvent &) {
	// Find which columns need to be repainted
	std::vector<char> paint_columns;
	paint_columns.resize(columns.size(), false);
	bool any = false;
	for (wxRegionIterator region(GetUpdateRegion()); region; ++region) {
		wxRect updrect = region.GetRect();
		int x = 0;
		for (size_t i : agi::util::range(columns.size())) {
			int width = columns[i]->Width();
			if (width && updrect.x < x + width && updrect.x + updrect.width > x) {
				paint_columns[i] = true;
				any = true;
			}
			x += width;
		}
	}

	if (!any) return;

	int w = 0;
	int h = 0;
	GetClientSize(&w,&h);
	w -= scrollBar->GetSize().GetWidth();

	wxAutoBufferedPaintDC dc(this);
	dc.SetFont(font);

	dc.SetBackground(row_colors.Default);
	dc.Clear();

	// Draw labels
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(row_colors.LeftCol);
	dc.DrawRectangle(0, lineHeight, columns[0]->Width(), h-lineHeight);

	// Row colors
	wxColour text_standard(theme::Colour("Colour/Subtitle Grid/Standard"));
	wxColour text_selection(theme::Colour("Colour/Subtitle Grid/Selection"));
	wxColour text_collision(theme::Colour("Colour/Subtitle Grid/Collision"));
	wxColour accent = theme::UiColours().accent;
	bool alternate_rows = OPT_GET("Subtitle/Grid/Alternate Rows")->GetBool();

	// First grid row
	wxPen grid_pen(theme::Colour("Colour/Subtitle Grid/Lines"));
	dc.SetPen(grid_pen);
	dc.DrawLine(0, 0, w, 0);
	dc.SetPen(*wxTRANSPARENT_PEN);

	auto paint_text = [&](wxString const& str, int x, int y, int col) {
		int left = x + 4;
		if (columns[col]->Centered()) {
			wxSize ext = dc.GetTextExtent(str);
			left += (columns[col]->Width() - 6 - ext.GetWidth()) / 2;
		}

		dc.DrawText(str, left, y + 2 + GridColumn::row_padding);
	};

	// Paint header
	{
		dc.SetTextForeground(text_standard);
		dc.SetBrush(row_colors.Header);
		dc.DrawRectangle(0, 0, w, lineHeight);

		int x = 0;
		for (size_t i : agi::util::range(columns.size())) {
			if (paint_columns[i])
				paint_text(columns[i]->Header(), x, 0, i);
			x += columns[i]->Width();
		}

		dc.SetPen(grid_pen);
		dc.DrawLine(0, lineHeight, w, lineHeight);
	}

	// Paint the rows
	const int drawPerScreen = h/lineHeight + 1;
	const int nDraw = mid(0, drawPerScreen, GetRows() - yPos);
	const int grid_x = columns[0]->Width();

	const auto active_line = context->selectionController->ReadActiveLine();
	auto const& mutable_selection = context->selectionController->ReadSelectedSet();
    std::set<AssDialogue const*> selection(mutable_selection.begin(), mutable_selection.end());
	visible_rows.clear();

	for (int i : agi::util::range(nDraw)) {
		wxBrush color = row_colors.Default;
		AssDialogue const* curDiag = index_line_map[i + yPos];

		bool inSel = !!selection.count(curDiag);
		if (inSel && curDiag->Comment)
			color = row_colors.SelectedComment;
		else if (inSel)
			color = row_colors.Selection;
		else if (curDiag->Comment)
			color = row_colors.Comment;

		if (OPT_GET("Subtitle/Grid/Highlight Subtitles in Frame")->GetBool() && IsDisplayed(curDiag)) {
			if (color == row_colors.Default)
				color = row_colors.Visible;
			visible_rows.push_back(i + yPos);
		}
		if (color == row_colors.Default && (i + yPos) % 2 == 1 && alternate_rows)
			color = row_colors.Alternate;
		dc.SetBrush(color);

		// Draw row background color
		if (color != row_colors.Default) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(grid_x, (i + 1) * lineHeight + 1, w, lineHeight);
		}

		if (active_line != curDiag && curDiag->CollidesWith(active_line))
			dc.SetTextForeground(text_collision);
		else if (inSel)
			dc.SetTextForeground(text_selection);
		else if (curDiag->Comment)
			dc.SetTextForeground(Mix(text_standard, color.GetColour(), 0.55));
		else
			dc.SetTextForeground(text_standard);

		// Draw text
		int x = 0;
		int y = (i + 1) * lineHeight;
		for (size_t j : agi::util::range(columns.size())) {
			if (paint_columns[j])
				columns[j]->Paint(dc, x, y + GridColumn::row_padding, curDiag, context);
			x += columns[j]->Width();
		}

		// Selected rows: a bar in the accent colour at the left edge.
		if (inSel) {
			dc.SetBrush(wxBrush(accent));
			dc.DrawRectangle(0, y + 1, FromDIP(3), lineHeight - 1);
		}

		// Draw grid
		dc.SetPen(grid_pen);
		dc.DrawLine(0, y + lineHeight, w , y + lineHeight);
		dc.SetPen(*wxTRANSPARENT_PEN);
	}

	// Draw grid columns
	{
		int maxH = (nDraw + 1) * lineHeight;
		int x = 0;
		dc.SetPen(grid_pen);
		for (auto const& column : columns) {
			x += column->Width();
			if (x < w)
				dc.DrawLine(x, 0, x, maxH);
		}
		dc.DrawLine(0, 0, 0, maxH);
		dc.DrawLine(w, 0, w, maxH);
	}

	if (active_line && active_line->Row >= yPos && active_line->Row < yPos + nDraw) {
		dc.SetPen(wxPen(theme::Colour("Colour/Subtitle Grid/Active Border")));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(0, (active_line->Row - yPos + 1) * lineHeight, w, lineHeight + 1);
	}
}

void BaseGrid::OnSize(wxSizeEvent &) {
	AdjustScrollbar();
	Refresh(false);
}

void BaseGrid::OnScroll(wxScrollEvent &event) {
	int newPos = event.GetPosition();
	if (yPos != newPos) {
		{ yPos = newPos; auto ui = context->subtitleAccess->UIState(); ui.scroll_position = yPos; context->subtitleAccess->UpdateUIState(ui); }
		Refresh(false);
	}
}

void BaseGrid::OnMouseEvent(wxMouseEvent &event) {
	int h = GetClientSize().GetHeight();
	bool shift = event.ShiftDown();
	bool alt = event.AltDown();
	bool ctrl = event.CmdDown();

	// Row that mouse is over
	bool click = event.LeftDown();
	bool dclick = event.LeftDClick();
	int row = event.GetY() / lineHeight + yPos - 1;
	if (holding && !click)
		row = mid(0, row, GetRows()-1);
	AssDialogue const* dlg = GetDialogue(row);
	if (!dlg) row = 0;

	bool over_number = dlg && event.GetY() >= lineHeight
		&& event.GetX() >= 0 && event.GetX() < columns[0]->Width();
	if (event.Moving() || event.Leaving()) {
		wxString tip;
		if (over_number && !event.Leaving())
			tip = dlg->Comment ? _("Click to show this subtitle line") : _("Click to hide this subtitle line");
		if (!GetToolTip() || GetToolTip()->GetTip() != tip)
			SetToolTip(tip);
		SetCursor(over_number && !event.Leaving() ? wxCURSOR_HAND : wxCURSOR_ARROW);
	}

	// The number cell is a per-row visibility switch. Modified clicks keep
	// their normal selection behavior; a double click counts as a second click.
	if ((click || dclick) && over_number && !holding && !shift && !ctrl && !alt) {
        auto write = context->subtitleAccess->TryWrite();
        if (!write) return;
        auto target = GetDialogue(row);
        auto edit = context->subtitleAccess->Edit(write->Lease());
        edit.SetDialogueField(target->Id, &AssDialogueBase::Comment, !target->Comment);
        edit.Commit(_("comment change"), AssFile::COMMIT_DIAG_META, -1, target->Id);
		return;
	}

    int extend_row = ExtendRow();
	if (event.ButtonDown() && OPT_GET("Subtitle/Grid/Focus Allow")->GetBool())
		SetFocus();

	if (holding) {
		if (!event.LeftIsDown()) {
			if (dlg)
				MakeRowVisible(row);
			holding = false;
			ReleaseMouse();
		}
		else {
			// Only scroll if the mouse has moved to a different row to avoid
			// scrolling on sloppy clicks
			if (row != extend_row) {
				if (row <= yPos)
					ScrollTo(yPos - 3);
				// When dragging down we give a 3 row margin to make it easier
				// to see what's going on, but we don't want to scroll down if
				// the user clicks on the bottom row and drags up
				else if (row > yPos + h / lineHeight - (row > extend_row ? 3 : 1))
					ScrollTo(yPos + 3);
			}
		}
	}
	else if (click && dlg) {
		holding = true;
		CaptureMouse();
	}

	if ((click || holding || dclick) && dlg) {
        auto const& rows = index_line_map;
        auto target = rows.at(row);
        int old_extend = rows.at(extend_row)->Id;

		// SetActiveID will scroll the grid if the row is only half-visible,
		// but we don't want to scroll until the mouse moves or the button is
		// released, to avoid selecting multiple lines on a click
		int old_y_pos = yPos;
		context->selectionController->SetActiveID(target->Id);
		ScrollTo(old_y_pos);
		extend_id = target->Id;

		auto selection = context->selectionController->SelectedIDs();

		// Toggle selected
		if (click && ctrl && !shift && !alt) {
			// Ctrl+click on a bound row is the temporary single-line edit
			// escape hatch; do not expand the binding group.
			if (!GetSubtitleBinding(context->subtitleAccess->Read(), *target).empty()) {
				context->selectionController->SetSelectionAndActiveIDs({target->Id}, target->Id, false);
				return;
			}
			bool isSel = !!selection.count(target->Id);
			if (isSel && selection.size() == 1) return;
			SelectRow(row, true, !isSel);
			return;
		}

		// Normal click
		if ((click || dclick) && !shift && !ctrl && !alt) {
			SelectRow(row, false);
			if (dclick) {
				if (CanEditDrawing(context)) {
					EditDrawing(context);
					return;
				}
				context->audioBox->ScrollToActiveLine();
				context->videoController->JumpToTime(target->Start);
			}
			return;
		}

		// Alt+click on a bound row also edits only that member.
		if (click && !shift && !ctrl && alt) {
			if (!GetSubtitleBinding(context->subtitleAccess->Read(), *target).empty())
				context->selectionController->SetSelectionAndActiveIDs({target->Id}, target->Id, false);
			return;
		}

		// Block select
		if ((click && shift && !alt) || holding) {
			extend_id = old_extend;
			int i1 = row;
			int i2 = ExtendRow();

			if (i1 > i2)
				std::swap(i1, i2);

			// Toggle each
			std::set<int> newsel;
			if (ctrl) newsel = selection;
			for (int i = i1; i <= i2; i++)
				newsel.insert(rows.at(i)->Id);
			context->selectionController->SetSelectedIDs(std::move(newsel));
			return;
		}

		return;
	}

	// Mouse wheel
	if (event.GetWheelRotation() != 0) {
		if (ForwardMouseWheelEvent(this, event)) {
			int step = shift ? h / lineHeight - 2 : 3;
			scrollWheelProgress += event.GetWheelRotation();
			ScrollTo(yPos - step * (scrollWheelProgress / event.GetWheelDelta()));
			scrollWheelProgress %= event.GetWheelDelta();
		}
		return;
	}

	event.Skip();
}

void BaseGrid::OnContextMenu(wxContextMenuEvent &evt) {
	wxPoint pos = evt.GetPosition();
	if (pos == wxDefaultPosition || ScreenToClient(pos).y > lineHeight) {
		// Context actions target the row under the pointer, including an unselected rectangle.
		if (pos != wxDefaultPosition) {
            auto const& rows = index_line_map;
			int row = ScreenToClient(pos).y / lineHeight + yPos - 1;
			if (auto line = static_cast<size_t>(row) < rows.size() ? rows[row] : nullptr) {
				if (context->selectionController->ReadSelectedSet().count(line))
					context->selectionController->SetActiveID(line->Id);
				else
					context->selectionController->SetSelectionAndActiveIDs({line->Id}, line->Id);
			}
		}
		context_menu = menu::GetMenu("grid_context", (wxID_HIGHEST + 1) + 8000, context);
		menu::OpenPopupMenu(context_menu.get(), this);
	}
	else {
		wxMenu menu;
		for (size_t i : agi::util::range(columns.size())) {
			if (columns[i]->CanHide())
				menu.Append(MENU_SHOW_COL + i, columns[i]->Description(), "", wxITEM_CHECK)->Check(columns[i]->Visible());
		}
		PopupMenu(&menu);
	}
}

void BaseGrid::ScrollTo(int y) {
	int nextY = mid(0, y, GetRows() - 1);
	if (yPos != nextY) {
		{ yPos = nextY; auto ui = context->subtitleAccess->UIState(); ui.scroll_position = yPos; context->subtitleAccess->UpdateUIState(ui); }
		scrollBar->SetThumbPosition(yPos);
		Refresh(false);
	}
}

void BaseGrid::AdjustScrollbar() {
	wxSize clientSize = GetClientSize();
	wxSize scrollbarSize = scrollBar->GetSize();

	scrollBar->Freeze();
	scrollBar->SetSize(clientSize.GetWidth() - scrollbarSize.GetWidth(), 0, scrollbarSize.GetWidth(), clientSize.GetHeight());

	if (GetRows() <= 1) {
		yPos = 0;
		scrollBar->Enable(false);
		scrollBar->Thaw();
		return;
	}

	if (!scrollBar->IsEnabled())
		scrollBar->Enable(true);

	int drawPerScreen = clientSize.GetHeight() / lineHeight;
	int rows = GetRows();

	{ yPos = mid(0, yPos, rows - 1); auto ui = context->subtitleAccess->UIState(); ui.scroll_position = yPos; context->subtitleAccess->UpdateUIState(ui); }

	scrollBar->SetScrollbar(yPos, drawPerScreen, rows + drawPerScreen - 1, drawPerScreen - 2, true);
	scrollBar->Thaw();
}

void BaseGrid::SetColumnWidths() {
	int w, h;
	GetClientSize(&w, &h);

	// DC for text extents test
	wxClientDC dc(this);
	dc.SetFont(font);

	text_refresh_rects.clear();
	int x = 0;

	if (!width_helper)
		width_helper = std::make_unique<WidthHelper>();
	width_helper->SetDC(&dc);

	for (auto const& column : columns) {
		column->UpdateWidth(context, *width_helper);
		if (column->Width() && column->RefreshOnTextChange())
			text_refresh_rects.emplace_back(x, 0, column->Width(), h);
		x += column->Width();
	}
	width_helper->Age();
}

AssDialogue const* BaseGrid::GetDialogue(int n) const {
	if (static_cast<size_t>(n) >= index_line_map.size()) return nullptr;
	return index_line_map[n];
}
int BaseGrid::ExtendRow() const {
    for (size_t i = 0; i < index_line_map.size(); ++i)
        if (index_line_map[i]->Id == extend_id) return static_cast<int>(i);
    return std::max(0, active_row);
}

bool BaseGrid::IsDisplayed(const AssDialogue *line) const {
	if (!context->project->VideoProvider()) return false;
	int frame = context->videoController->GetFrameN();
	return context->project->Timecodes().FrameAtTime(line->Start, agi::vfr::START) <= frame
		&& context->project->Timecodes().FrameAtTime(line->End, agi::vfr::END) >= frame;
}

void BaseGrid::OnCharHook(wxKeyEvent &event) {
	if ((event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER)
		&& !event.HasAnyModifiers() && CanEditDrawing(context)) {
		EditDrawing(context);
		return;
	}
	if (hotkey::check("Subtitle Grid", context, event))
		return;

	int key = event.GetKeyCode();

	if (key == WXK_UP || key == WXK_DOWN ||
		key == WXK_PAGEUP || key == WXK_PAGEDOWN ||
		key == WXK_HOME || key == WXK_END)
	{
		event.Skip();
		return;
	}

	hotkey::check("Audio", context, event);
}

void BaseGrid::OnKeyDown(wxKeyEvent &event) {
	int w,h;
	GetClientSize(&w, &h);

	int key = event.GetKeyCode();
	bool ctrl = event.CmdDown();
	bool alt = event.AltDown();
	bool shift = event.ShiftDown();

	int dir = 0;
	int step = 1;
	if (key == WXK_UP) dir = -1;
	else if (key == WXK_DOWN) dir = 1;
	else if (key == WXK_PAGEUP) {
		dir = -1;
		step = h / lineHeight - 2;
	}
	else if (key == WXK_PAGEDOWN) {
		dir = 1;
		step = h / lineHeight - 2;
	}
	else if (key == WXK_HOME) {
		dir = -1;
		step = GetRows();
	}
	else if (key == WXK_END) {
		dir = 1;
		step = GetRows();
	}

	if (!dir) {
		event.Skip();
		return;
	}
    auto const& rows = index_line_map;

	auto active_line = context->selectionController->ReadActiveLine();
    if (rows.empty()) { context->selectionController->SetActiveID(0); return; }
	int old_extend = rows.at(ExtendRow())->Id;
	int next = mid(0, (active_line ? active_line->Row : 0) + dir * step, GetRows() - 1);
	context->selectionController->SetActiveID(rows.at(next)->Id);

	// Move selection
	if (!ctrl && !shift && !alt) {
		SelectRow(next);
		return;
	}

	// Move active only
	if (alt && !shift && !ctrl)
		return;

	// Shift-selection
	if (shift && !ctrl && !alt) {
		extend_id = old_extend;
		// Set range
		int begin = next;
		int end = ExtendRow();
		if (end < begin)
			std::swap(begin, end);

		// Select range
		std::set<int> newsel;
		for (int i = begin; i <= end; i++)
			newsel.insert(rows.at(i)->Id);

		context->selectionController->SetSelectedIDs(std::move(newsel));

		MakeRowVisible(next);
		return;
	}
}

void BaseGrid::SetByFrame(bool state) {
	if (byFrame == state) return;
	byFrame = state;
	for (auto& column : columns)
		column->SetByFrame(byFrame);
	SetColumnWidths();
	Refresh(false);
}
