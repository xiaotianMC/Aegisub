// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "status_info.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subtitle_project_access.h"
#include "workspace_tabs.h"

#include <wx/intl.h>

namespace status_info {
wxString Line(agi::Context *c) {
    auto active = c->selectionController->ReadActiveLine();
    if (!active) return {};
    size_t total = c->subtitleAccess->Read().Events.size();
    size_t selected = c->selectionController->ReadSelectedSet().size();
    auto times = fmt_wx("%s → %s", active->Start.GetAssFormatted(), active->End.GetAssFormatted());
    if (selected > 1)
        return fmt_tl("Line %d of %d (%d selected) · %s", active->Row + 1, total, selected, times);
    return fmt_tl("Line %d of %d · %s", active->Row + 1, total, times);
}

wxString File(agi::Context *c) {
    if (c->workspaceTabs && c->workspaceTabs->Comparison())
        return fmt_tl("Comparing with %s", c->workspaceTabs->ComparisonName());
    return c->subsController->IsModified() ? _("Unsaved changes") : _("No unsaved changes");
}
}
