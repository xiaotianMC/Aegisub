// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace agi {
/// SHA-256, to check that a file downloaded is the one meant: its hash fed
/// a piece at a time, as the file is read
class Sha256 {
	std::array<uint32_t, 8> state;
	std::array<unsigned char, 64> block{};
	size_t filled = 0;
	uint64_t length = 0;

	void Compress(unsigned char const *chunk);

public:
	Sha256();
	void Update(void const *data, size_t size);
	void Update(std::string_view data) { Update(data.data(), data.size()); }
	/// The hash in lowercase hex. It is done with after this.
	std::string HexDigest();
};
}
