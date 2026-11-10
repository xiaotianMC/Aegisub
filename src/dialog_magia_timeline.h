// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include "magia_timeline.h"

#include <optional>

namespace agi { struct Context; }

/// What to run MagiaTimeline with, chosen over what its config.yml says
/// (docs/magiatimeline.md): the strategy and its preset, how frames are
/// sampled and whether the text is read. Starts from @p start; offers only
/// what @p installed has. Nothing when it is cancelled.
std::optional<magia_timeline::Values> ShowMagiaTimelineDialog(agi::Context *c,
	magia_timeline::Installed const& installed, magia_timeline::Values const& start,
	agi::fs::path const& config_file);
