// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"
#include <libaegisub/io.h>

namespace workbench::detail {
std::string NewId();
int64_t Now();
bool Linked(Path const& path);
bool ValidId(std::string const& id);
bool ValidDisplayName(std::string const& name);
std::string ReadBytes(Path const& file);

class WriteLock {
	intptr_t handle = -1;
public:
	explicit WriteLock(Path const& path, bool allow_busy = false);
	~WriteLock();
	WriteLock(WriteLock const&) = delete;
	bool Acquired() const { return handle != -1; }
};

struct FileStamp {
	int64_t bytes;
	std::string identity;
	int64_t modified;
	int32_t nanoseconds = 0;
	bool operator==(FileStamp const&) const = default;
};
struct Fingerprint {
	FileStamp stamp;
	std::string sha256;
};
Path ProjectFile(Manifest const& project, Path const& relative);
Fingerprint HashFile(Manifest const& project, Path const& relative, std::stop_token stop);
FileStamp InspectFile(Manifest const& project, Path const& relative);
bool InternalDirectoryName(Path const& name);
struct RemovalScope {
	int64_t files;
	std::string identity, digest;
};
RemovalScope InspectRemovalScope(Path const& root);
std::string InspectRemovalIdentity(Path const& root);
bool ContainsPath(Path const& parent, Path const& child);
bool PathOccupied(Path const& path);
Path ProjectWriteLock(Path const& root);
void CreateRemovalStage(Path const& directory);
void MoveRemovalFolder(Path const& source, Path const& destination);
std::optional<Manifest> ParentSeries(Manifest const& project);
std::string EpisodeMembership(Manifest const& series, std::string const& project_id, Path const& relative);
std::unique_ptr<agi::io::Save> PrepareEpisodeRemoval(Manifest const& series, std::string const& project_id,
                                                    Path const& relative);
void CommitEpisodeRemoval(Manifest const& series, agi::io::Save& output);
void CheckStop(std::stop_token const& stop);
Manifest LinkEpisode(Manifest const& series, Manifest const& child, std::string const& label);
bool PublishAsset(Manifest const& project, std::string const& task, RegistrationOutput const& output,
                  FileStamp const& verified, bool existing_only);
bool ContainsOutput(Manifest const& project, std::string const& task, RegistrationOutput const& output);
}
