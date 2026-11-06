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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
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
bool classic_icons = false;

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
void libresrc_set_classic(bool classic) { classic_icons = classic; }
bool libresrc_classic() { return classic_icons; }

namespace {
// Pixel hinting for the monochrome icons (docs/icons.md): drawn on a 16 grid
// with a 1.5 stroke, they would be soft at 16px, the 1.5 lines falling across
// two columns of pixels. So each size gets a whole number of pixels for the
// stroke and the drawing's coordinates moved by under a pixel to sit on the
// pixel grid: line centres on pixel centres for odd widths, on pixel edges for
// even ones.
struct Hinter {
	double scale; // pixels per unit
	int width;    // stroke width in pixels

	explicit Hinter(int px) : scale(px / 16.0), width(std::max(1, static_cast<int>(std::floor(scale * 1.25 + 0.5)))) { }

	// A stroke's centre
	double Line(double v) const {
		double d = v * scale;
		d = width % 2 ? std::floor(d + 1e-6) + 0.5 : std::floor(d + 0.5 + 1e-6);
		return d / scale;
	}
	// A length, or the edge of a filled area
	double Whole(double v) const { return std::round(v * scale) / scale; }
	double StrokeUnits() const { return width / scale; }

	static std::string Format(double v) {
		char buf[32];
		snprintf(buf, sizeof buf, "%.4f", v);
		std::string s = buf;
		s.erase(s.find_last_not_of('0') + 1);
		if (s.back() == '.') s.pop_back();
		return s == "-0" ? "0" : s;
	}

	std::string Path(std::string_view d) const {
		std::vector<std::string> tokens;
		for (size_t i = 0; i < d.size(); ) {
			char ch = d[i];
			if (isalpha(static_cast<unsigned char>(ch)) && ch != 'e') { tokens.emplace_back(1, ch); ++i; }
			else if (ch == '-' || ch == '.' || isdigit(static_cast<unsigned char>(ch))) {
				size_t j = i + 1;
				bool dot = ch == '.';
				while (j < d.size() && (isdigit(static_cast<unsigned char>(d[j])) || (d[j] == '.' && !dot) || d[j] == 'e' || (d[j] == '-' && d[j - 1] == 'e'))) {
					if (d[j] == '.') dot = true;
					++j;
				}
				tokens.emplace_back(d.substr(i, j - i));
				i = j;
			}
			else ++i;
		}
		std::string out;
		size_t i = 0;
		char cmd = 0;
		double moved_x = 0, moved_y = 0; // how far the last point moved
		auto num = [&] { return i < tokens.size() ? std::stod(tokens[i++]) : 0.0; };
		auto put = [&](double v) { if (!out.empty() && !isalpha(static_cast<unsigned char>(out.back()))) out += ' '; out += Format(v); };
		while (i < tokens.size()) {
			if (isalpha(static_cast<unsigned char>(tokens[i][0]))) { cmd = tokens[i++][0]; out += cmd; }
			switch (cmd) {
			case 'Z': case 'z': continue;
			case 'M': case 'L': {
				double x = num(), y = num(), nx = Line(x), ny = Line(y);
				put(nx); put(ny);
				moved_x = nx - x; moved_y = ny - y;
				if (cmd == 'M') cmd = 'L';
				break;
			}
			case 'H': put(Line(num())); break;
			case 'V': put(Line(num())); break;
			case 'm': case 'l':
				put(Whole(num())); put(Whole(num()));
				if (cmd == 'm') cmd = 'l';
				break;
			case 'A': case 'a': {
				for (int k = 0; k < 5; ++k) put(num());
				double x = num(), y = num();
				if (cmd == 'A') { put(Line(x)); put(Line(y)); }
				else { put(Whole(x)); put(Whole(y)); }
				break;
			}
			case 'C': {
				// Control points move with the end they belong to
				double x1 = num(), y1 = num(), x2 = num(), y2 = num(), x = num(), y = num();
				double nx = Line(x), ny = Line(y);
				put(x1 + moved_x); put(y1 + moved_y); put(x2 + nx - x); put(y2 + ny - y); put(nx); put(ny);
				moved_x = nx - x; moved_y = ny - y;
				break;
			}
			case 'S': {
				double x2 = num(), y2 = num(), x = num(), y = num(), nx = Line(x), ny = Line(y);
				put(x2 + nx - x); put(y2 + ny - y); put(nx); put(ny);
				moved_x = nx - x; moved_y = ny - y;
				break;
			}
			default: // Not used by the icons: kept as drawn
				put(num());
			}
		}
		return out;
	}

	static bool Attr(std::string const& el, const char *name, std::string *value) {
		std::string key = std::string(" ") + name + "='";
		auto pos = el.find(key);
		if (pos == std::string::npos) return false;
		auto end = el.find('\'', pos + key.size());
		if (value) *value = el.substr(pos + key.size(), end - pos - key.size());
		return true;
	}
	static double Num(std::string const& el, const char *name) {
		std::string v;
		return Attr(el, name, &v) ? std::stod(v) : 0;
	}
	static void Set(std::string& el, const char *name, std::string const& v) {
		std::string key = std::string(" ") + name + "='";
		auto pos = el.find(key);
		if (pos == std::string::npos) return;
		pos += key.size();
		el.replace(pos, el.find('\'', pos) - pos, v);
	}

	std::string Element(std::string el) const {
		if (Attr(el, "stroke-width", nullptr)) return el; // Its own weight: as drawn
		std::string stroke;
		bool stroked = !(Attr(el, "stroke", &stroke) && stroke == "none");
		if (el.starts_with("<path")) {
			std::string d;
			if (Attr(el, "d", &d)) Set(el, "d", Path(d));
		}
		else if (el.starts_with("<circle") || el.starts_with("<ellipse")) {
			if (!stroked) return el;
			bool circle = el.starts_with("<circle");
			double rx = Num(el, circle ? "r" : "rx"), ry = Num(el, circle ? "r" : "ry");
			// The outline's left and top edge on the grid
			Set(el, "cx", Format(Line(Num(el, "cx") - rx) + rx));
			Set(el, "cy", Format(Line(Num(el, "cy") - ry) + ry));
		}
		else if (el.starts_with("<rect")) {
			double x = Num(el, "x"), y = Num(el, "y"), w = Num(el, "width"), h = Num(el, "height");
			auto snap = [&](double v) { return stroked ? Line(v) : Whole(v); };
			double x0 = snap(x), y0 = snap(y), x1 = snap(x + w), y1 = snap(y + h);
			Set(el, "x", Format(x0)); Set(el, "y", Format(y0));
			Set(el, "width", Format(x1 - x0)); Set(el, "height", Format(y1 - y0));
		}
		return el;
	}

	std::string Body(std::string_view body) const {
		std::string out;
		size_t pos = 0;
		while (pos < body.size()) {
			auto start = body.find('<', pos);
			if (start == std::string_view::npos) { out += body.substr(pos); break; }
			out += body.substr(pos, start - pos);
			auto end = body.find('>', start);
			std::string el(body.substr(start, end - start + 1));
			for (auto tag : {"<path", "<circle", "<ellipse", "<rect"})
				if (el.starts_with(tag) && isspace(static_cast<unsigned char>(el[strlen(tag)]))) { el = Element(el); break; }
			out += el;
			pos = end + 1;
		}
		return out;
	}
};

std::string ModernSvg(std::string_view body, int px, int dir, bool flyout, bool dark) {
	// Near the toolbar text colours: dark grey on light, light grey on dark
	const std::string colour = dark ? "#d8d8d8" : "#2b2b2b";
	Hinter hint(px);
	std::string svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>";
	svg += dir == wxLayout_RightToLeft ? "<g transform='matrix(-1 0 0 1 16 0)'>" : "<g>";
	svg += "<g fill='none' stroke='@' stroke-width='" + Hinter::Format(hint.StrokeUnits()) + "' stroke-linecap='round' stroke-linejoin='round'>";
	svg += hint.Body(body);
	svg += "</g>";
	if (flyout) svg += "<path d='M15.5 12.5V15.5H12.5Z' fill='@'/>";
	svg += "</g></svg>";
	for (size_t pos; (pos = svg.find('@')) != std::string::npos; )
		svg.replace(pos, 1, colour);
	return svg;
}

// Drawn for each size asked for, hinted for that size
class ModernBundle final : public wxBitmapBundleImpl {
	std::string body;
	wxSize size;
	int dir;
	bool flyout, dark;
	std::map<std::pair<int, int>, wxBitmap> bitmaps;

public:
	ModernBundle(std::string body, int height, int dir, bool flyout, bool dark)
	: body(std::move(body)), size(height, height), dir(dir), flyout(flyout), dark(dark) { }

	wxSize GetDefaultSize() const override { return size; }
	wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
		return wxSize(std::lround(size.x * scale), std::lround(size.y * scale));
	}
	wxBitmap GetBitmap(wxSize const& want) override {
		auto& bitmap = bitmaps[{want.x, want.y}];
		if (!bitmap.IsOk()) {
			auto svg = ModernSvg(body, std::min(want.x, want.y), dir, flyout, dark);
			bitmap = wxBitmapBundle::FromSVG(svg.c_str(), want).GetBitmap(want);
		}
		return bitmap;
	}
};
}

std::string libresrc_modern_svg(const char *body, int px, int dir) {
	return ModernSvg(body, px, dir, false, dark_mode);
}

wxBitmapBundle libresrc_modern_bundle(const char *body, int height, int dir, bool flyout) {
	return wxBitmapBundle::FromImpl(new ModernBundle(body, height, dir, flyout, dark_mode));
}

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

wxBitmapBundle libresrc_getbitmapbundle(const char *name, const LibresrcBlob *images, size_t count, int height, int dir) {
	// This function should only ever be called on the GUI thread but declaring this thread_local is the safe way
	thread_local std::map<std::tuple<const LibresrcBlob *, int, int, bool, bool>, wxBitmapBundle> cache;
	auto key = std::make_tuple(images, height, dir, dark_mode, classic_icons);

	if (auto cached = cache.find(key); cached != cache.end()) {
		return cached->second;
	}

	if (!classic_icons) {
		if (auto body = libresrc_modern_icon(name)) {
			auto bundle = libresrc_modern_bundle(body, height, dir);
			cache[key] = bundle;
			return bundle;
		}
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
