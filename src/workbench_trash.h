// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"

namespace workbench {
// Throws when the system cannot move the folder to trash.
void TrashFolder(Path const& folder);
}
