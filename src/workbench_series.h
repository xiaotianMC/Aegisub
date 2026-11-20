// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"

class wxWindow;
wxWindow* CreateWorkbenchSeries(wxWindow* parent, workbench::Manifest const& series, workbench::Path const& database, bool writable,
                         std::function<workbench::Folders()> project_template,
                         std::function<void(workbench::Manifest const&, workbench::Manifest)> open_child,
                         std::function<void(workbench::Manifest const&)> updated, std::function<void(wxWindow*)> leave);
