// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "libaegisub/io.h"

#include <libaegisub/access.h>
#include "libaegisub/fs.h"
#include "libaegisub/log.h"
#include "libaegisub/util.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <random>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace agi::io {
using namespace agi::fs;

std::unique_ptr<std::istream> Open(path const& file, bool binary) {
	LOG_D("agi/io/open/file") << file;

	auto stream = std::make_unique<std::ifstream>(file, (binary ? std::ios::binary : std::ios::in));
	if (stream->fail()) {
		acs::CheckFileRead(file);
		throw IOFatal("Unknown fatal error occurred opening " + file.string());
	}

	return std::unique_ptr<std::istream>(stream.release());
}

namespace {
// Open the same handle exclusively and keep writing through it until commit.
std::FILE *open_exclusive(path const& temporary, path const& destination, bool binary) {
#ifdef _WIN32
	int fd = -1;
	int flags = _O_WRONLY | _O_CREAT | _O_EXCL | _O_NOINHERIT | (binary ? _O_BINARY : _O_TEXT);
	if (auto error = _wsopen_s(&fd, temporary.c_str(), flags, _SH_DENYRW, _S_IREAD | _S_IWRITE)) {
		errno = error;
		return nullptr;
	}
	auto file = _fdopen(fd, binary ? "wb" : "w");
#else
	// A replacement must not make a private document world-readable.
	struct stat existing{};
	mode_t mode = 0666;
	if (::stat(destination.c_str(), &existing) == 0) mode = existing.st_mode & 0777;
	else if (errno != ENOENT) return nullptr;
	int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
	if (fd < 0) return nullptr;
	auto file = ::fdopen(fd, binary ? "wb" : "w");
#endif
	if (!file) {
		int error = errno;
#ifdef _WIN32
		_close(fd);
#else
		::close(fd);
#endif
		std::error_code ignored;
		std::filesystem::remove(temporary, ignored);
		throw fs::FileSystemUnknownError(std::generic_category().message(error));
	}
	return file;
}
}

class Save::Output final : public std::streambuf {
	path const destination;
	path temporary;
	std::FILE *file = nullptr;

	std::streamsize xsputn(char const *data, std::streamsize count) override {
		if (count <= 0) return 0;
		return std::fwrite(data, 1, static_cast<size_t>(count), file);
	}
	int_type overflow(int_type ch) override {
		if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
		return std::fputc(traits_type::to_char_type(ch), file) == EOF ? traits_type::eof() : ch;
	}
	int sync() override { return std::fflush(file) == 0 ? 0 : -1; }
	pos_type seekoff(off_type offset, std::ios::seekdir direction, std::ios::openmode) override {
		int origin = direction == std::ios::beg ? SEEK_SET : direction == std::ios::cur ? SEEK_CUR : SEEK_END;
#ifdef _WIN32
		if ((origin != SEEK_CUR || offset != 0) && _fseeki64(file, offset, origin)) return pos_type(-1);
		return pos_type(_ftelli64(file));
#else
		if ((origin != SEEK_CUR || offset != 0) && ::fseeko(file, offset, origin)) return pos_type(-1);
		return pos_type(::ftello(file));
#endif
	}
	pos_type seekpos(pos_type position, std::ios::openmode mode) override {
		return seekoff(off_type(position), std::ios::beg, mode);
	}

public:
	std::ostream stream{this};

	Output(path const& target, bool binary) : destination(target) {
		std::random_device random;
		for (int attempt = 0; attempt < 64; ++attempt) {
			temporary = target.parent_path() / (".aegisub-save-" + std::to_string(random()) + "-" + std::to_string(random()) + ".tmp");
			if (temporary == target) continue;
			file = open_exclusive(temporary, target, binary);
			if (file) return;
			if (errno != EEXIST) {
				auto error = std::generic_category().message(errno);
				acs::CheckDirWrite(target.parent_path().empty() ? path(".") : target.parent_path());
				throw fs::FileSystemUnknownError("Cannot create a temporary file for " + target.string() + ": " + error);
			}
		}
		throw fs::WriteDenied(target);
	}

	~Output() {
		if (file) std::fclose(file);
		std::error_code ignored;
		if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
	}

	void Commit() {
		stream.flush();
		if (!stream || std::ferror(file))
			throw fs::FileSystemUnknownError("Failed to write " + destination.string());
#ifdef _WIN32
		int sync_result = _commit(_fileno(file));
#else
		int sync_result = ::fsync(::fileno(file));
#endif
		int error = sync_result == 0 ? 0 : errno;
		if (std::fclose(std::exchange(file, nullptr)) != 0 && !error) error = errno;
		if (error)
			throw fs::FileSystemUnknownError("Failed to finish writing " + destination.string() + ": " + std::generic_category().message(error));

#ifdef _WIN32
		for (int i = 0; ; ++i) {
			try {
				fs::Rename(temporary, destination);
				break;
			}
			catch (fs::FileSystemError const&) {
				// A Windows virus scanner can briefly hold the destination open.
				if (i == 9) throw;
				util::sleep_for(100);
			}
		}
#else
		fs::Rename(temporary, destination);
#endif
		temporary.clear();
	}
};

Save::Save(path const& file, bool binary) : output(std::make_unique<Output>(file, binary)) {
	LOG_D("agi/io/save/file") << file;
}

Save::~Save() = default;

std::ostream& Save::Get() {
	if (!output) throw InternalError("Save already finished");
	return output->stream;
}

void Save::Commit() {
	if (!output) throw InternalError("Save already finished");
	auto pending = std::move(output);
	pending->Commit();
}

} // namespace agi::io
