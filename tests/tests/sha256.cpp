// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include <libaegisub/fs.h>
#include <libaegisub/sha256.h>
#include <libaegisub/util.h>

#include <main.h>

#include <fstream>
#include <string>
#include <vector>

namespace {
std::string Hash(std::string const& s) {
	agi::Sha256 h;
	h.Update(s);
	return h.HexDigest();
}
}

TEST(lagi_sha256, the_standards_test_vectors) {
	EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", Hash(""));
	EXPECT_EQ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", Hash("abc"));
	EXPECT_EQ("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
		Hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
	EXPECT_EQ("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
		Hash("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"));
	EXPECT_EQ("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", Hash(std::string(1000000, 'a')));
}

TEST(lagi_sha256, fed_in_pieces_of_any_size_it_is_the_same) {
	std::string data;
	for (int i = 0; i < 1000; ++i) data += static_cast<char>(i * 7 + 3);
	std::string const whole = Hash(data);
	for (size_t piece : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 127u, 999u}) {
		agi::Sha256 h;
		for (size_t at = 0; at < data.size(); at += piece)
			h.Update(data.data() + at, std::min(piece, data.size() - at));
		EXPECT_EQ(whole, h.HexDigest()) << piece;
	}
	// The padding at each length about a block's end
	for (size_t n : {55u, 56u, 57u, 63u, 64u, 119u, 120u}) {
		agi::Sha256 a, b;
		a.Update(data.substr(0, n));
		b.Update(data.data(), n / 2);
		b.Update(data.data() + n / 2, n - n / 2);
		EXPECT_EQ(a.HexDigest(), b.HexDigest()) << n;
	}
}

// A real model, as published: only with one named by AEGISUB_WHISPER_MODEL,
// which must be ggml-tiny.bin
TEST(lagi_sha256, the_published_hash_of_a_whisper_model) {
	auto model = agi::util::GetEnv("AEGISUB_WHISPER_MODEL");
	if (!model || model->empty()) GTEST_SKIP() << "AEGISUB_WHISPER_MODEL is not set";
	std::ifstream in(agi::fs::path(*model), std::ios::binary);
	ASSERT_TRUE(in);
	agi::Sha256 h;
	std::vector<char> buf(1 << 20);
	while (in) {
		in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
		h.Update(buf.data(), static_cast<size_t>(in.gcount()));
	}
	EXPECT_EQ("be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21", h.HexDigest());
}
