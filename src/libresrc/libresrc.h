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

#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

struct LibresrcBlob {
	const unsigned char *data;
	size_t size;
	int scale;
};

#include "bitmap.h"
#include "default_config.h"

#include <wx/version.h>

class wxBitmap;
class wxBitmapBundle;
class wxIcon;
class wxIconBundle;

wxBitmap libresrc_getimage(const unsigned char *image, size_t size, int dir=0);
wxIcon libresrc_geticon(const unsigned char *image, size_t size);
#define GETIMAGE(a) libresrc_getimage(a, sizeof(a))

#define GET_DEFAULT_CONFIG(a) std::string_view(reinterpret_cast<const char *>(a), sizeof(a))

// height is the desired displayed height of the bitmap bundle in DIP.
// Having to specify this when constructing the bitmap is slightly awkward,
// but wxWidgets doesn't allow you to scale a wxBitmapBundle after creating it,
// so the scale has to be set for the individual bitmaps before constructing it.
// @p name is the icon's name: unless classic icons are chosen, one drawn in
// icons_modern.cpp replaces the bitmaps (docs/icons.md).
wxBitmapBundle libresrc_getbitmapbundle(const char *name, const LibresrcBlob *images, size_t count, int height, int dir=0);

// A window's icon: the monochrome one of @p name unless classic icons are
// chosen or it has none, else the coloured bitmaps
wxIconBundle libresrc_geticonbundle(const char *name, const LibresrcBlob *images, size_t count);

// Bitmap bundles made after this are adapted for a dark background: dark
// glyphs (bold, play, arrows...) and black parts reaching the icon's edge are
// lightened, so they stay visible; colours are kept (docs/icons.md).
void libresrc_set_dark(bool dark);
bool libresrc_dark();

// Classic: Aegisub's coloured bitmaps; otherwise the monochrome icons
// (App/Classic Icons). Like libresrc_set_dark, it affects bundles made after it.
void libresrc_set_classic(bool classic);
bool libresrc_classic();
// The monochrome icon's SVG body, or null if @p name has none
const char *libresrc_modern_icon(std::string_view name);
// A monochrome icon from @p body, in the colour for the current appearance;
// @p flyout adds the corner mark of a tool with a choice of shapes. Each size
// is drawn on its own, hinted to the pixel grid: a whole number of pixels for
// the stroke, lines on pixel centres or edges.
wxBitmapBundle libresrc_modern_bundle(const char *body, int height, int dir = 0, bool flyout = false);
// The SVG drawn for @p px pixels (for tests)
std::string libresrc_modern_svg(const char *body, int px, int dir = 0);

class wxImage;
// The adaptation itself. @p glyph: the icon is drawn mostly in dark colours,
// as libresrc_is_glyph says of its largest image; all its dark parts are lightened.
void libresrc_adapt_for_dark(wxImage& image, bool glyph);
bool libresrc_is_glyph(wxImage const& image);

#define GETBUNDLE(a, h) libresrc_getbitmapbundle(#a, a, sizeof(a) / sizeof(*a), h)
#define GETBUNDLEDIR(a, h, d) libresrc_getbitmapbundle(#a, a, sizeof(a) / sizeof(*a), h, d)
#define GETICONS(a) libresrc_geticonbundle(#a, a, sizeof(a) / sizeof(*a))
