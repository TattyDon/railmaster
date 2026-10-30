#pragma once

#include <string>
#include <string_view>

namespace railmaster::client {

// Text for the interface: a TrueType font (data/fonts, read with
// stb_truetype) baked into a glyph atlas per size at the screen's pixel
// density; until init_font succeeds, a built-in 5x7 pixel font (capitals,
// digits and punctuation) stands in. Coordinates are window points,
// origin top-left; `scale` 1 is about 7 points to the top of a capital.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

// Load the TrueType font; false (and the pixel font kept) if it cannot.
bool init_font(const std::string& path);
// Real pixels per window point (2 on Retina screens), for sharp text.
void set_pixel_ratio(float ratio);

// Width in pixels of `text` drawn at `scale`.
int text_width(std::string_view text, int scale);

// Draw `text` with the top of its capitals at y, from x, in the current GL
// colour (and alpha). Not inside glBegin/glEnd.
void draw_text(float x, float y, std::string_view text, int scale);

} // namespace railmaster::client
