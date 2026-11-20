// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"

class wxString;
class wxWindow;
wxString WorkbenchStatusLabel(std::string const& status);
std::optional<workbench::Manifest> EditWorkbenchMetadata(wxWindow* parent, workbench::Manifest const& project);
