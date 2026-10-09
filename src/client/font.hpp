#pragma once

#include <string_view>

namespace railmaster::client {

// A tiny built-in 5x7 pixel font, so the placeholder client needs no font
// library. Covers capitals, digits and common punctuation; lower case is
// drawn as upper case. Coordinates are window pixels, origin top-left.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

// Width in pixels of `text` drawn at `scale`.
int text_width(std::string_view text, int scale);

// Draw `text` with its top-left corner at (x, y), in the current GL colour.
void draw_text(float x, float y, std::string_view text, int scale);

} // namespace railmaster::client
