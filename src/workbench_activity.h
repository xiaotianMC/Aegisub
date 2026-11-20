// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"
#include <functional>

class wxWindow;
void ShowWorkbenchActivity(wxWindow* parent, workbench::Path const& database,
                           std::optional<workbench::Manifest> const& project, bool show_tasks,
                           std::function<void()> notify_home);
