// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"
namespace agi { struct Context; }

void RecordWorkbenchFile(agi::Context* context, workbench::Path const& source, workbench::FileEvent event,
                         workbench::Path const& output = {});
