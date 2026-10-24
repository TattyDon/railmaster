#include "font.hpp"

#include <stb_truetype.h>

#include <SDL_opengl.h>

#include <array>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

namespace railmaster::client {

namespace {

struct Glyph {
    char c;
    std::array<const char*, kGlyphHeight> rows;
};

// clang-format off
constexpr Glyph kGlyphs[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
    {']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
    {'$', {"..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
    {'*', {"..#..", "..#..", "#####", ".###.", ".#.#.", "#...#", "....."}}, // a star, for town ratings
    {';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..."}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
};
// clang-format on

const Glyph* find_glyph(char c) {
    const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const Glyph& g : kGlyphs) {
        if (g.c == up) return &g;
    }
    return nullptr;
}

constexpr int kAdvance = kGlyphWidth + 1;

} // namespace

static int bitmap_width(std::string_view text, int scale) {
    return text.empty() ? 0 : (static_cast<int>(text.size()) * kAdvance - 1) * scale;
}

static void bitmap_draw(float x, float y, std::string_view text, int scale) {
    const auto s = static_cast<float>(scale);
    glBegin(GL_QUADS);
    float pen = x;
    for (char c : text) {
        if (c != ' ') {
            const Glyph* g = find_glyph(c);
            if (!g) g = find_glyph('?');
            for (int row = 0; row < kGlyphHeight; ++row) {
                for (int col = 0; col < kGlyphWidth; ++col) {
                    if (g->rows[static_cast<std::size_t>(row)][col] != '#') continue;
                    const float px = pen + static_cast<float>(col) * s;
                    const float py = y + static_cast<float>(row) * s;
                    glVertex2f(px, py);
                    glVertex2f(px + s, py);
                    glVertex2f(px + s, py + s);
                    glVertex2f(px, py + s);
                }
            }
        }
        pen += static_cast<float>(kAdvance) * s;
    }
    glEnd();
}

// The TrueType face: a font file baked into a glyph atlas per text size,
// at the screen's real pixel density so it stays sharp on Retina screens.
namespace {

struct Atlas {
    GLuint texture = 0;
    std::array<stbtt_bakedchar, 96> chars{}; // ' ' to '~'
    float px = 0;                            // baked size, real pixels
    float ratio = 1;                         // real pixels per window point
};

std::vector<unsigned char> g_font;
std::map<std::pair<int, int>, Atlas> g_atlases; // by (scale, ratio x 100)
float g_ratio = 1.0f;
bool g_ready = false;

// The size text of `scale` is drawn at, in window points: capitals about
// as tall as the old 7-pixel glyphs (7 x scale), lower case to match.
float em_points(int scale) { return 10.0f * static_cast<float>(scale); }

const Atlas& atlas(int scale) {
    const int r = static_cast<int>(std::lround(g_ratio * 100));
    auto it = g_atlases.find({scale, r});
    if (it != g_atlases.end()) return it->second;
    Atlas a;
    a.ratio = g_ratio;
    a.px = em_points(scale) * g_ratio;
    const int side = a.px > 40 ? 1024 : 512;
    std::vector<unsigned char> bitmap(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    stbtt_BakeFontBitmap(g_font.data(), 0, a.px, bitmap.data(), side, side, 32, 96, a.chars.data());
    glGenTextures(1, &a.texture);
    glBindTexture(GL_TEXTURE_2D, a.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, side, side, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return g_atlases.emplace(std::make_pair(scale, r), a).first->second;
}

int glyph_index(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 32 && u < 128 ? u - 32 : '?' - 32;
}

} // namespace

bool init_font(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "font: cannot read %s; using the built-in pixel font\n", path.c_str());
        return false;
    }
    g_font.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    stbtt_fontinfo info;
    g_ready = !g_font.empty() && stbtt_InitFont(&info, g_font.data(), stbtt_GetFontOffsetForIndex(g_font.data(), 0)) != 0;
    if (!g_ready) std::fprintf(stderr, "font: %s is not a font this can read; using the pixel font\n", path.c_str());
    return g_ready;
}

void set_pixel_ratio(float ratio) { g_ratio = std::clamp(ratio, 1.0f, 4.0f); }

int text_width(std::string_view text, int scale) {
    if (!g_ready) return bitmap_width(text, scale);
    const Atlas& a = atlas(scale);
    float w = 0;
    for (const char c : text) w += a.chars[static_cast<std::size_t>(glyph_index(c))].xadvance;
    return static_cast<int>(std::ceil(w / a.ratio));
}

void draw_text(float x, float y, std::string_view text, int scale) {
    if (!g_ready) {
        bitmap_draw(x, y, text, scale);
        return;
    }
    const Atlas& a = atlas(scale);
    // `y` is the top of the capitals: the baseline sits 7 x scale below.
    const float baseline = y + 7.0f * static_cast<float>(scale);
    const int side = a.px > 40 ? 1024 : 512;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, a.texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    float pen = 0; // real pixels from x
    for (const char c : text) {
        const stbtt_bakedchar& b = a.chars[static_cast<std::size_t>(glyph_index(c))];
        const float x0 = x + (pen + b.xoff) / a.ratio, y0 = baseline + b.yoff / a.ratio;
        const float x1 = x0 + static_cast<float>(b.x1 - b.x0) / a.ratio, y1 = y0 + static_cast<float>(b.y1 - b.y0) / a.ratio;
        const float s0 = static_cast<float>(b.x0) / static_cast<float>(side), s1 = static_cast<float>(b.x1) / static_cast<float>(side);
        const float t0 = static_cast<float>(b.y0) / static_cast<float>(side), t1 = static_cast<float>(b.y1) / static_cast<float>(side);
        glTexCoord2f(s0, t0);
        glVertex2f(x0, y0);
        glTexCoord2f(s1, t0);
        glVertex2f(x1, y0);
        glTexCoord2f(s1, t1);
        glVertex2f(x1, y1);
        glTexCoord2f(s0, t1);
        glVertex2f(x0, y1);
        pen += b.xadvance;
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

} // namespace railmaster::client
