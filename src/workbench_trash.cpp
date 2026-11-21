// SPDX-License-Identifier: ISC
#include "workbench_trash.h"
#include "compat.h"

#include <wx/filefn.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <stdexcept>

#if !defined(wxHAS_MOVE_TO_TRASH) && defined(__WXGTK__) && defined(__linux__)
#include <gio/gio.h>
#endif

void workbench::TrashFolder(Path const& folder) {
	wxString reason;
#ifdef wxHAS_MOVE_TO_TRASH
	wxLogCollector logs;
	if (wxMoveToTrash(to_wx(folder))) return;
	reason = logs.GetMessages();
	reason.Trim();
#elif defined(__WXGTK__) && defined(__linux__)
	// Older wxGTK releases lack wxMoveToTrash. Use the same GIO operation.
	auto file = g_file_new_for_path(folder.c_str());
	GError* error = nullptr;
	auto moved = g_file_trash(file, nullptr, &error);
	g_object_unref(file);
	std::unique_ptr<GError, decltype(&g_error_free)> details(error, g_error_free);
	if (moved) return;
	if (details) reason = wxString::FromUTF8(details->message);
#endif
	auto message = _("The system could not move the folder to Trash.");
	if (!reason.empty()) message += "\n" + reason;
	throw std::runtime_error(from_wx(message));
}
