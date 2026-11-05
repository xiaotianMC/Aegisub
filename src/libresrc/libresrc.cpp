// Copyright (c) 2009, Amar Takhar <verm@aegisub.org>
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

#include "libresrc.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include <wx/bitmap.h>
#include <wx/bmpbndl.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/mstream.h>

wxBitmap libresrc_getimage(const unsigned char *buff, size_t size, int dir) {
	wxMemoryInputStream mem(buff, size);
	if (dir != wxLayout_RightToLeft)
		return wxBitmap(wxImage(mem));
	return wxBitmap(wxImage(mem).Mirror());
}

wxIcon libresrc_geticon(const unsigned char *buff, size_t size) {
	wxMemoryInputStream mem(buff, size);
	wxIcon icon;
	icon.CopyFromBitmap(wxBitmap(wxImage(mem)));
	return icon;
}

namespace {
bool dark_mode = false;

struct Hsl { double h, s, l; };

Hsl ToHsl(unsigned char r8, unsigned char g8, unsigned char b8) {
	double r = r8 / 255.0, g = g8 / 255.0, b = b8 / 255.0;
	double hi = std::max({r, g, b}), lo = std::min({r, g, b});
	double l = (hi + lo) / 2, d = hi - lo;
	if (d == 0) return {0, 0, l};
	double s = l > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);
	double h = hi == r ? (g - b) / d + (g < b ? 6 : 0) : hi == g ? (b - r) / d + 2 : (r - g) / d + 4;
	return {h / 6, s, l};
}

double HueToRgb(double p, double q, double t) {
	if (t < 0) t += 1;
	if (t > 1) t -= 1;
	if (t < 1.0 / 6) return p + (q - p) * 6 * t;
	if (t < 1.0 / 2) return q;
	if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
	return p;
}

void FromHsl(Hsl c, unsigned char *rgb) {
	double r = c.l, g = c.l, b = c.l;
	if (c.s > 0) {
		double q = c.l < 0.5 ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s, p = 2 * c.l - q;
		r = HueToRgb(p, q, c.h + 1.0 / 3);
		g = HueToRgb(p, q, c.h);
		b = HueToRgb(p, q, c.h - 1.0 / 3);
	}
	rgb[0] = static_cast<unsigned char>(std::lround(r * 255));
	rgb[1] = static_cast<unsigned char>(std::lround(g * 255));
	rgb[2] = static_cast<unsigned char>(std::lround(b * 255));
}

// Dark on light becomes light on dark: 0 -> 0.86, 0.3 -> 0.62.
void Lighten(unsigned char *rgb) {
	auto c = ToHsl(rgb[0], rgb[1], rgb[2]);
	c.l = 0.86 - 0.8 * c.l;
	FromHsl(c, rgb);
}

void EnsureAlpha(wxImage& image) {
	if (image.HasAlpha()) return;
	if (image.HasMask()) image.InitAlpha();
	else image.SetAlpha();
}
}

void libresrc_set_dark(bool dark) { dark_mode = dark; }
bool libresrc_dark() { return dark_mode; }

bool libresrc_is_glyph(wxImage const& image) {
	if (!image.IsOk()) return false;
	const unsigned char *rgb = image.GetData(), *alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
	double total = 0, dark = 0;
	for (int i = 0, n = image.GetWidth() * image.GetHeight(); i < n; ++i) {
		int a = alpha ? alpha[i] : 255;
		if (a < 32) continue;
		total += a;
		if (ToHsl(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]).l < 0.3) dark += a;
	}
	return total > 0 && dark / total >= 0.85;
}

void libresrc_adapt_for_dark(wxImage& image, bool glyph) {
	if (!image.IsOk()) return;
	EnsureAlpha(image);
	int w = image.GetWidth(), h = image.GetHeight();
	unsigned char *rgb = image.GetData(), *alpha = image.GetAlpha();
	auto lightness = [&](int i) { return ToHsl(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]); };
	// A glyph: every dark pixel. Otherwise only near-black, nearly grey ones
	// (outlines and black parts; coloured outlines read well on dark).
	auto dark = [&](int i) {
		auto c = lightness(i);
		return glyph ? c.l < 0.3 : c.l < 0.22 && c.s < 0.3;
	};

	// Of the others, only dark areas that reach the icon's edge: a black mark
	// drawn on a light part of the icon (a plus on a lens) keeps its contrast.
	std::vector<unsigned char> state(w * h, 0); // 1 candidate, 2 visited
	for (int i = 0; i < w * h; ++i)
		if (alpha[i] >= 128 && dark(i)) state[i] = 1;
	std::vector<int> region, stack;
	for (int start = 0; start < w * h; ++start) {
		if (state[start] != 1) continue;
		region.clear();
		stack.assign(1, start);
		state[start] = 2;
		bool edge = glyph;
		while (!stack.empty()) {
			int i = stack.back();
			stack.pop_back();
			region.push_back(i);
			int x = i % w, y = i / w;
			for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) { edge = true; continue; }
				int j = ny * w + nx;
				if (alpha[j] < 128) edge = true;
				else if (state[j] == 1) { state[j] = 2; stack.push_back(j); }
			}
		}
		if (edge)
			for (int i : region) Lighten(rgb + i * 3);
	}
	// Antialiased edge pixels of the same colours.
	for (int i = 0; i < w * h; ++i)
		if (alpha[i] > 0 && alpha[i] < 128 && dark(i)) Lighten(rgb + i * 3);
}

wxBitmapBundle libresrc_getbitmapbundle(const LibresrcBlob *images, size_t count, int height, int dir) {
	// This function should only ever be called on the GUI thread but declaring this thread_local is the safe way
	thread_local std::map<std::tuple<const LibresrcBlob *, int, int, bool>, wxBitmapBundle> cache;
	auto key = std::make_tuple(images, height, dir, dark_mode);

	if (auto cached = cache.find(key); cached != cache.end()) {
		return cached->second;
	}

	std::vector<wxImage> decoded;
	decoded.reserve(count);
	size_t largest = 0;
	for (size_t i = 0; i < count; i++) {
		wxMemoryInputStream mem(images[i].data, images[i].size);
		decoded.emplace_back(mem);
		if (dir == wxLayout_RightToLeft) decoded.back() = decoded.back().Mirror();
		if (images[i].scale > images[largest].scale) largest = i;
	}
	if (dark_mode && count) {
		// One decision for all sizes, from the most detailed one.
		bool glyph = libresrc_is_glyph(decoded[largest]);
		for (auto& image : decoded) libresrc_adapt_for_dark(image, glyph);
	}

	wxVector<wxBitmap> bitmaps;
	bitmaps.reserve(count);
	for (size_t i = 0; i < count; i++) {
		bitmaps.push_back(wxBitmap(decoded[i]));
		bitmaps.back().SetScaleFactor(double(images[i].scale) / height);
	}

	auto bundle = wxBitmapBundle::FromBitmaps(bitmaps);
	cache[key] = bundle;

	return bundle;
}

wxIconBundle libresrc_geticonbundle(const LibresrcBlob *images, size_t count) {
	thread_local std::map<const LibresrcBlob *, wxIconBundle> cache;

	if (auto cached = cache.find(images); cached != cache.end()) {
		return cached->second;
	}

	wxIconBundle bundle;
	for (size_t i = 0; i < count; i++) {
		bundle.AddIcon(libresrc_geticon(images[i].data, images[i].size));
	}

	cache[images] = bundle;

	return bundle;
}
