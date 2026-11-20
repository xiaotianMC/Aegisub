// SPDX-License-Identifier: ISC
#pragma once
#include "workbench_store.h"

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
void CheckStop(std::stop_token const& stop);
Manifest LinkEpisode(Manifest const& series, Manifest const& child, std::string const& label);
bool PublishAsset(Manifest const& project, std::string const& task, RegistrationOutput const& output,
                  FileStamp const& verified, bool existing_only);
bool ContainsOutput(Manifest const& project, std::string const& task, RegistrationOutput const& output);
}
