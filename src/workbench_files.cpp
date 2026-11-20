// SPDX-License-Identifier: ISC
#include "workbench_internal.h"
#include <libaegisub/exception.h>
#include <libaegisub/sha256.h>

#include <cerrno>
#include <iterator>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace workbench::detail {
namespace fs = std::filesystem;
using Error = std::runtime_error;

bool InternalDirectoryName(Path const& name) {
	auto text = name.string();
	return text.size() == 4 && text[0] == '.' && (text[1] == 'a' || text[1] == 'A')
	       && (text[2] == 'e' || text[2] == 'E') && (text[3] == 'g' || text[3] == 'G');
}

void CheckStop(std::stop_token const& stop) {
	if (stop.stop_requested()) throw agi::UserCancelException("Cancelled");
}

Path ProjectFile(Manifest const& project, Path const& relative) {
	if (project.kind != Kind::Project || relative.empty() || relative.has_root_path() || relative.filename().empty()
	    || relative != relative.lexically_normal() || relative.generic_string().find_first_of("\\:") != std::string::npos
	    || relative.string().find('\0') != std::string::npos)
		throw Error("Expected a file path relative to this project");
	Path entry = project.root;
	for (auto const& part : relative) {
		if (part == "." || part == ".." || InternalDirectoryName(Path(part))) throw Error("Invalid project file path");
		entry /= part;
		if (Linked(entry)) throw Error("Linked project file or directory: " + entry.string());
		if (entry != project.root / relative && fs::exists(entry / ".aeg"))
			throw Error("This file belongs to a nested project: " + entry.string());
	}
	return entry;
}

namespace {
class Input {
#ifdef _WIN32
	HANDLE handle;
#else
	int fd;
#endif
public:
	Input(Manifest const& project, Path const& relative) {
		auto file = ProjectFile(project, relative);
#ifdef _WIN32
		handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (handle == INVALID_HANDLE_VALUE) throw Error("Cannot open " + file.string());
#else
		fd = ::open(project.root.c_str(), O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
		for (auto it = relative.begin(); fd >= 0 && it != relative.end(); ++it) {
			int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
			if (std::next(it) != relative.end()) flags |= O_DIRECTORY;
			int next = ::openat(fd, it->c_str(), flags);
			::close(fd);
			fd = next;
		}
		if (fd < 0) throw Error("Cannot open " + file.string());
#endif
	}
	Input(Input const&) = delete;
	~Input() {
#ifdef _WIN32
		CloseHandle(handle);
#else
		::close(fd);
#endif
	}
	FileStamp Stamp() const {
#ifdef _WIN32
		BY_HANDLE_FILE_INFORMATION info{};
		if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info)
		    || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			throw Error("Expected a regular project file");
		auto value = [](DWORD high, DWORD low) { return (uint64_t(high) << 32) | low; };
		auto size = value(info.nFileSizeHigh, info.nFileSizeLow);
		if (size > uint64_t((std::numeric_limits<int64_t>::max)())) throw Error("File too large");
		return {static_cast<int64_t>(size), std::to_string(info.dwVolumeSerialNumber) + "/" +
			std::to_string(value(info.nFileIndexHigh, info.nFileIndexLow)),
			static_cast<int64_t>(value(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime))};
#else
		struct stat info{};
		if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) throw Error("Expected a regular project file");
#ifdef __APPLE__
		auto modified = info.st_mtimespec;
#else
		auto modified = info.st_mtim;
#endif
		// Keep seconds and nanoseconds separate to preserve the full native timestamp range.
		return {info.st_size, std::to_string(info.st_dev) + "/" + std::to_string(info.st_ino),
		        static_cast<int64_t>(modified.tv_sec), static_cast<int32_t>(modified.tv_nsec)};
#endif
	}
	size_t Read(char *buffer, size_t capacity) const {
#ifdef _WIN32
		DWORD done;
		if (!ReadFile(handle, buffer, static_cast<DWORD>(capacity), &done, nullptr)) throw Error("Failed to read project file");
		return done;
#else
		ssize_t done;
		do { done = ::read(fd, buffer, capacity); } while (done < 0 && errno == EINTR);
		if (done < 0) throw Error("Failed to read project file");
		return static_cast<size_t>(done);
#endif
	}
};
}

FileStamp InspectFile(Manifest const& project, Path const& relative) {
	return Input(project, relative).Stamp();
}

Fingerprint HashFile(Manifest const& project, Path const& relative, std::stop_token stop) {
	CheckStop(stop);
	Input in(project, relative);
	auto before = in.Stamp();
	agi::Sha256 hash;
	std::vector<char> buffer(256 * 1024);
	int64_t bytes = 0;
	while (true) {
		CheckStop(stop);
		auto n = in.Read(buffer.data(), buffer.size());
		if (!n) break;
		if (before.bytes - bytes < static_cast<int64_t>(n)) throw Error("File grew during verification");
		hash.Update(buffer.data(), n);
		bytes += static_cast<int64_t>(n);
	}
	if (bytes != before.bytes || in.Stamp() != before || InspectFile(project, relative) != before)
		throw Error("File changed during verification: " + relative.string());
	return {std::move(before), hash.HexDigest()};
}
}
