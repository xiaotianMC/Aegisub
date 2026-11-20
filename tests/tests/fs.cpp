// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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
//
// Aegisub Project http://www.aegisub.org/

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <main.h>
#include <util.h>

#ifdef _WIN32
#include <share.h>
#elif GTEST_HAS_DEATH_TEST
#include <csignal>
#include <cstdlib>
#include <sys/resource.h>
#endif

#include <libaegisub/file_mapping.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>

#undef CreateDirectory

using namespace agi::fs;

TEST(lagi_fs, failed_serializer_preserves_existing_file) {
	path target = "data/interrupted-save.ass";
	std::ofstream(target, std::ios::binary) << "complete original";
	EXPECT_THROW({
		agi::io::Save file(target, true);
		file.Get() << "partial replacement";
		throw std::runtime_error("serialization failed");
	}, std::runtime_error);
	std::ifstream original(target, std::ios::binary);
	EXPECT_EQ("complete original", std::string(std::istreambuf_iterator<char>(original), {}));
}

TEST(lagi_fs, failed_stream_discards_the_private_temporary_file) {
	path folder = "data/save-failure";
	CreateDirectory(folder);
	path target = folder / "existing.ass";
	std::ofstream(target) << "old";
	agi::io::Save file(target);
	file.Get() << "new";
	file.Get().setstate(std::ios::badbit);
	EXPECT_THROW(file.Commit(), FileSystemError);
	std::ifstream original(target);
	std::string content;
	original >> content;
	EXPECT_EQ("old", content);
	std::vector<std::string> files;
	DirectoryIterator(folder, "").GetAll(files);
	EXPECT_EQ(std::vector<std::string>{"existing.ass"}, files);
}

#if !defined(_WIN32) && GTEST_HAS_DEATH_TEST
TEST(lagi_fs, system_write_limit_leaves_the_original_and_removes_partial_output) {
	::testing::FLAGS_gtest_death_test_style = "threadsafe";
	path folder = "data/limited-save";
	CreateDirectory(folder);
	path target = folder / "document.ass";
	std::ofstream(target) << "complete";
	// Restrict only the child. Buffered output can fail on write or on flush.
	EXPECT_EXIT({
		struct rlimit limit;
		if (getrlimit(RLIMIT_FSIZE, &limit) != 0) std::_Exit(2);
		limit.rlim_cur = 1024;
		if (setrlimit(RLIMIT_FSIZE, &limit) != 0 || std::signal(SIGXFSZ, SIG_IGN) == SIG_ERR)
			std::_Exit(2);
		try {
			agi::io::Save output(target, true);
			output.Get() << std::string(8192, 'x');
			output.Commit();
		}
		catch (FileSystemError const&) { std::_Exit(0); }
		std::_Exit(1);
	}, ::testing::ExitedWithCode(0), "");
	std::ifstream original(target);
	EXPECT_EQ("complete", std::string(std::istreambuf_iterator<char>(original), {}));
	std::vector<std::string> files;
	DirectoryIterator(folder, "").GetAll(files);
	EXPECT_EQ(std::vector<std::string>{"document.ass"}, files);
}
#endif

#ifdef _WIN32
TEST(lagi_fs, a_windows_sharing_violation_keeps_the_original_and_removes_the_temporary_file) {
	path folder = "data/locked-save";
	CreateDirectory(folder);
	path target = folder / "document.ass";
	std::ofstream(target) << "original";
	std::unique_ptr<std::FILE, decltype(&std::fclose)> lock(
		_wfsopen(target.c_str(), L"rb", _SH_DENYRW), &std::fclose);
	ASSERT_NE(nullptr, lock);
	agi::io::Save replacement(target, true);
	replacement.Get() << "replacement";
	EXPECT_THROW(replacement.Commit(), FileSystemError);
	lock.reset();
	std::ifstream original(target);
	EXPECT_EQ("original", std::string(std::istreambuf_iterator<char>(original), {}));
	std::vector<std::string> files;
	DirectoryIterator(folder, "").GetAll(files);
	EXPECT_EQ(std::vector<std::string>{"document.ass"}, files);
}
#endif

TEST(lagi_fs, overlapping_saves_own_their_temporary_files) {
	path target = "data/concurrent.ass";
	path sibling = "data/concurrent.tmp.ass";
	std::ofstream(sibling) << "user file";
	agi::io::Save first(target, true);
	agi::io::Save second(target, true);
	first.Get() << "first";
	second.Get() << "second";
	first.Commit();
	{
		std::ifstream result(target);
		EXPECT_EQ("first", std::string(std::istreambuf_iterator<char>(result), {}));
	}
	EXPECT_EQ(std::streampos(6), second.Get().tellp());
	second.Get().seekp(0);
	second.Get().put('S');
	second.Commit();
	std::ifstream result(target), user_file(sibling);
	EXPECT_EQ("Second", std::string(std::istreambuf_iterator<char>(result), {}));
	EXPECT_EQ("user file", std::string(std::istreambuf_iterator<char>(user_file), {}));
}

TEST(lagi_fs, copy_can_publish_an_empty_file) {
	std::ofstream("data/empty-source", std::ios::binary).close();
	std::ofstream("data/empty-destination", std::ios::binary) << "old";
	ASSERT_NO_THROW(Copy("data/empty-source", "data/empty-destination"));
	EXPECT_EQ(0u, Size("data/empty-destination"));
}

TEST(lagi_fs, exists) {
	EXPECT_TRUE(Exists("data/file"));
	EXPECT_TRUE(Exists("data/dir"));
	EXPECT_FALSE(Exists("data/nonexistent"));
}

TEST(lagi_fs, exists_does_not_throw) {
	EXPECT_NO_THROW(Exists("data/file"));
	EXPECT_NO_THROW(Exists("data/dir"));
	EXPECT_NO_THROW(Exists("data/nonexistent"));
	EXPECT_NO_THROW(Exists("schema://host/file"));
}

TEST(lagi_fs, file_exists) {
	EXPECT_TRUE(FileExists("data/file"));
	EXPECT_FALSE(FileExists("data/dir"));
}

TEST(lagi_fs, dir_exists) {
	EXPECT_FALSE(DirectoryExists("data/file"));
	EXPECT_TRUE(DirectoryExists("data/dir"));
}

#ifdef _WIN32
#else
TEST(lagi_fs, short_name_is_a_no_op) {
	EXPECT_STREQ("a b c d", ShortName("a b c d").c_str());
}
#endif

TEST(lagi_fs, free_space_returns_a_value) {
	uintmax_t space = 0;
	ASSERT_NO_THROW(space = FreeSpace("."));
	EXPECT_LT(0, space);
}

TEST(lagi_fs, file_size) {
	EXPECT_EQ(0u, Size("data/file"));
	EXPECT_EQ(10u, Size("data/ten_bytes"));
	EXPECT_THROW(Size("data/dir"), NotAFile);
}

TEST(lagi_fs, file_mapping_rejects_invalid_ranges) {
	agi::read_file_mapping file("data/ten_bytes");
	EXPECT_THROW(file.read(-1, 0), agi::InternalError);
	EXPECT_THROW(file.read(-1, 1), agi::InternalError);
	EXPECT_THROW(file.read(9, 2), agi::InternalError);
	EXPECT_THROW(file.read(1, std::numeric_limits<uint64_t>::max()), agi::InternalError);
	EXPECT_NO_THROW(file.read(10, 0));
}

TEST(lagi_fs, touch_creates_file) {
	Remove("data/touch_tmp");
	ASSERT_FALSE(Exists("data/touch_tmp"));
	Touch("data/touch_tmp");
	EXPECT_TRUE(Exists("data/touch_tmp"));
	Remove("data/touch_tmp");
}

TEST(lagi_fs, touch_updates_modified_time) {
	auto mod_time = ModifiedTime("data/touch_mod_time");
	Touch("data/touch_mod_time");
	EXPECT_LT(mod_time, ModifiedTime("data/touch_mod_time"));
}

TEST(lagi_fs, touch_does_not_modify_contents) {
	int expected_value = util::write_rand("data/tmp");

	Touch("data/tmp");

	EXPECT_EQ(expected_value, util::read_written_rand("data/tmp"));
}

TEST(lagi_fs, rename) {
	ASSERT_NO_THROW(Touch("data/rename_in"));
	ASSERT_NO_THROW(Remove("data/rename_out"));
	ASSERT_NO_THROW(Rename("data/rename_in", "data/rename_out"));
	EXPECT_FALSE(FileExists("data/rename_in"));
	EXPECT_TRUE(FileExists("data/rename_out"));
}

TEST(lagi_fs, rename_overwrites) {
	int expected_value = util::write_rand("data/rename_in");
	ASSERT_NO_THROW(Remove("data/rename_out"));
	ASSERT_NO_THROW(Touch("data/rename_out"));

	ASSERT_NO_THROW(Rename("data/rename_in", "data/rename_out"));

	EXPECT_FALSE(FileExists("data/rename_in"));
	EXPECT_TRUE(FileExists("data/rename_out"));
	EXPECT_EQ(expected_value, util::read_written_rand("data/rename_out"));
}

TEST(lagi_fs, copy) {
	int expected_value = util::write_rand("data/copy_in");
	ASSERT_NO_THROW(Remove("data/copy_out"));

	ASSERT_NO_THROW(Copy("data/copy_in", "data/copy_out"));

	EXPECT_TRUE(FileExists("data/copy_in"));
	EXPECT_TRUE(FileExists("data/copy_out"));
	EXPECT_EQ(expected_value, util::read_written_rand("data/copy_out"));
}

TEST(lagi_fs, copy_overwrites) {
	int expected_value = util::write_rand("data/copy_in");
	ASSERT_NO_THROW(Remove("data/copy_out"));
	ASSERT_NO_THROW(Touch("data/copy_out"));

	ASSERT_NO_THROW(Copy("data/copy_in", "data/copy_out"));

	EXPECT_TRUE(FileExists("data/copy_in"));
	EXPECT_TRUE(FileExists("data/copy_out"));
	EXPECT_EQ(expected_value, util::read_written_rand("data/copy_out"));
}

TEST(lagi_fs, copy_creates_path) {
}

TEST(lagi_fs, has_extension) {
	EXPECT_TRUE(HasExtension("foo.txt", "txt"));
	EXPECT_TRUE(HasExtension("foo.TXT", "txt"));
	EXPECT_TRUE(HasExtension("foo.tar.gz", "gz"));
	EXPECT_TRUE(HasExtension("foo.tar.gz", "tar.gz"));
	EXPECT_TRUE(HasExtension("foo.\xC3\x9F", "\xC3\x9F")); // sharp s

	EXPECT_FALSE(HasExtension("foo.tx", "txt"));
	EXPECT_FALSE(HasExtension("footxt", "txt"));
	EXPECT_FALSE(HasExtension("foo.txt/", "txt"));
	EXPECT_FALSE(HasExtension("foo.tar.gz", "tar"));
}

TEST(lagi_fs, dir_iterator_bad_directory) {
	std::vector<std::string> files;
	ASSERT_NO_THROW(DirectoryIterator("data/nonexistent", "*.*").GetAll(files));
	EXPECT_TRUE(files.empty());
}

TEST(lagi_fs, dir_iterator_no_filter) {
	std::vector<std::string> files;
	ASSERT_NO_THROW(DirectoryIterator("data/dir_iterator", "").GetAll(files));
	ASSERT_EQ(4u, files.size());
	sort(begin(files), end(files)); // order is not guaranteed
	EXPECT_STREQ("1.a", files[0].c_str());
	EXPECT_STREQ("1.b", files[1].c_str());
	EXPECT_STREQ("2.a", files[2].c_str());
	EXPECT_STREQ("2.b", files[3].c_str());
}

TEST(lagi_fs, dir_iterator_ext_filter) {
	std::vector<std::string> files;
	ASSERT_NO_THROW(DirectoryIterator("data/dir_iterator", "*.a").GetAll(files));
	ASSERT_EQ(2u, files.size());
	sort(begin(files), end(files)); // order is not guaranteed
	EXPECT_STREQ("1.a", files[0].c_str());
	EXPECT_STREQ("2.a", files[1].c_str());
}

TEST(lagi_fs, dir_iterator_ext_filter_skipping_first_works) {
	std::vector<std::string> files;
	ASSERT_NO_THROW(DirectoryIterator("data/dir_iterator", "*.b").GetAll(files));
	ASSERT_EQ(2u, files.size());
	sort(begin(files), end(files)); // order is not guaranteed
	EXPECT_STREQ("1.b", files[0].c_str());
	EXPECT_STREQ("2.b", files[1].c_str());
}

TEST(lagi_fs, dir_iterator_fn_filter) {
	std::vector<std::string> files;
	ASSERT_NO_THROW(DirectoryIterator("data/dir_iterator", "1.*").GetAll(files));
	ASSERT_EQ(2u, files.size());
	sort(begin(files), end(files)); // order is not guaranteed
	EXPECT_STREQ("1.a", files[0].c_str());
	EXPECT_STREQ("1.b", files[1].c_str());
}

TEST(lagi_fs, dir_iterator_all_filtered_out) {
	std::vector<std::string> files;
	ASSERT_NO_THROW(DirectoryIterator("data/dir_iterator", "*.c").GetAll(files));
	EXPECT_TRUE(files.empty());
}
