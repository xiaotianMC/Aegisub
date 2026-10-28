// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include "selection_controller.h"

#include <string>

class AssFile;
class AssDialogue;
namespace agi { struct Context; }

std::string GetSubtitleBinding(const AssFile& file, const AssDialogue& line);
int SubtitleBindingColorIndex(std::string const& group);
void BindSelectedSubtitles(agi::Context *c);
void UnbindSelectedSubtitles(agi::Context *c);

/// Expand a selection to include every member of the binding groups it touches.
Selection ExpandBoundSelection(AssFile& file, Selection const& selection);
