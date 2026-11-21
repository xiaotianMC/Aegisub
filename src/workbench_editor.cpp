// SPDX-License-Identifier: ISC
#include "workbench_editor.h"
#include "include/aegisub/context.h"
#include <libaegisub/exception.h>
#include <libaegisub/log.h>

namespace {
std::optional<workbench::Manifest> OwnerOf(workbench::Path const& file) {
	namespace fs = std::filesystem;
	if (!file.is_absolute() || !fs::is_regular_file(file)) return {};
	for (auto dir = file.parent_path(); !dir.empty(); dir = dir.parent_path()) {
		if (fs::exists(dir / ".aeg")) {
			if (!fs::is_regular_file(dir / ".aeg/project.json")) return {};
			auto project = workbench::Open(dir);
			if (project.root != dir) return {};
			return project;
		}
		if (dir == dir.root_path()) break;
	}
	return {};
}
}

void RecordWorkbenchFile(agi::Context* context, workbench::Path const& source, workbench::FileEvent event,
                         workbench::Path const& output) {
	using namespace workbench;
	namespace fs = std::filesystem;
	if (!context || !context->frame || !context->path) return;
	// Editor-only users do not create a workbench library by opening a subtitle.
	try {
		auto database = Roots::Resolve(*context->path).database;
		if (!fs::is_regular_file(database)) return;
		auto from = OwnerOf(source);
		auto to = output != source ? OwnerOf(output) : std::nullopt;
		if (!from && !to) return;
		Index index(database);
		bool recorded = from && index.RecordFile(*from, Path(source.lexically_relative(from->root)), event);
		if (to) index.RecordFile(*to, Path(output.lexically_relative(to->root)), recorded ? FileEvent::Opened : event);
	}
	catch (agi::Exception const& e) { LOG_W("workbench") << e.GetMessage(); }
	catch (std::exception const& e) { LOG_W("workbench") << e.what(); }
}
