// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"
#include <functional>

class wxWindow;
namespace agi { struct Context; }
enum class WorkbenchActivityPage { Files, Tasks, Dashboard, Downloads };
void ShowWorkbenchActivity(wxWindow* parent, workbench::Path const& database,
                           agi::Context* context, std::optional<workbench::Manifest> const& project, WorkbenchActivityPage page,
                           std::function<void()> notify_home);
void CloseWorkbenchProjectActivity(workbench::Path const& database, workbench::Location const& project);
