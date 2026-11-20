// SPDX-License-Identifier: ISC
#pragma once

#include <libaegisub/fs.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace agi { class Options; class Path; }
struct sqlite3;

namespace workbench {
using Path = agi::fs::path;
using Folders = std::map<std::string, std::string>;
enum class Kind { Project, Series };

struct Roots {
	Path templates;
	Path database;
	static Roots Resolve(agi::Path const& paths);
};

Folders DefaultFolders(Kind kind);
struct DirectoryTemplate {
	Folders folders;
	std::optional<std::string> bytes;
};
DirectoryTemplate LoadTemplate(Path const& directory, Kind kind);
void SaveTemplate(Path const& directory, Kind kind, Folders const& folders, DirectoryTemplate const& expected);
Folders ImportTemplate(Path const& file, Kind kind);
void ExportTemplate(Path const& file, Kind kind, Folders const& folders);

struct PreferenceChange {
	std::string key;
	bool current, incoming;
};
struct PreferenceImport {
	Path source, destination;
	std::optional<std::string> previous_file;
	std::vector<PreferenceChange> choices;
};
PreferenceImport PreviewPreferences(Path const& source, Path const& destination, agi::Options& active);
Path ImportPreferences(PreferenceImport const& preview, std::vector<size_t> const& selected, agi::Options& active);

struct Metadata {
	std::string description, source, source_language, target_language, owner;
	std::vector<std::string> tags;
	std::string status = "todo";
	bool operator==(Metadata const&) const = default;
};
struct Episode {
	std::string project_id, label, name;
	Path relative;
};
struct Manifest {
	Path root;
	Kind kind;
	std::string id, name;
	int64_t revision;
	Folders folders;
	std::string bytes; // Compare the exact opened revision, including edits made by other tools.
	Path cover; // Relative to the project or series, empty if no cover was chosen.
	Metadata metadata;
	std::vector<Episode> episodes;
};

struct EpisodeState {
	std::string name, status, error;
};
struct SeriesOverview {
	Manifest series;
	std::vector<EpisodeState> states; // Same order as series.episodes.
};
SeriesOverview InspectSeries(Manifest const& series, std::stop_token stop = {});
Manifest OpenEpisode(Manifest const& series, Episode const& episode);
Manifest CreateEpisodeProject(Manifest const& series, std::string const& name, Folders const& folders);

Manifest Create(Path const& parent, std::string const& name, Kind kind, Folders const& folders);
Manifest RegisterFolder(Path const& root, std::string const& name, Kind kind, Folders const& folders);
Manifest Open(Path const& root);
Manifest Rename(Manifest const& expected, std::string const& name);
Manifest UpdateMetadata(Manifest const& expected, Metadata const& values);
Path CoverFile(Manifest const& manifest);
Manifest SetCover(Manifest const& expected, Path const& source,
                  std::function<bool(std::string const&)> const& can_decode);

struct Location {
	std::string id, name;
	Path root;
};

struct FileEntry {
	Path relative;
	int64_t bytes;
};
struct ScanState {
	int64_t generation = 0, files = 0;
};
struct RegistrationTask {
	std::string id, project_id, project_name, name, state, reason;
	int64_t outputs, registered;
};
struct RegistrationOutput {
	std::string id, asset_id;
	Path relative;
	int64_t bytes;
	std::string sha256, file_id, stage;
};

class Index {
	struct CloseDatabase { void operator()(sqlite3 *db) const; };
	std::unique_ptr<sqlite3, CloseDatabase> db;
	Path file;
	void CheckLocation() const;
public:
	explicit Index(Path const& database);
	void Remember(Manifest const& manifest);
	Manifest AddEpisode(Manifest const& series, Manifest const& child, std::string const& label);
	std::vector<Location> List(int offset = 0, int count = 50) const;
	ScanState Scan(std::string const& project_id) const;
	std::vector<FileEntry> Files(std::string const& project_id, int offset = 0, int count = 50) const;
	void RebuildFiles(Manifest const& project, std::stop_token stop = {});
	std::string QueueRegistration(Manifest const& project, std::vector<Path> const& files, std::stop_token stop = {});
	std::vector<RegistrationTask> Registrations(int offset = 0, int count = 50) const;
	int64_t UnfinishedRegistrations() const;
	std::vector<RegistrationOutput> Outputs(std::string const& task_id, int offset = 0, int count = 50) const;
	void RunRegistration(std::string const& task_id, std::stop_token stop = {});
	void CancelRegistration(std::string const& task_id);
	void RecoverInterrupted();
};
}
