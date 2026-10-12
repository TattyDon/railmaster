#include "ui.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace railmaster::client::ui {

namespace {

constexpr float kBrass[3] = {0.78f, 0.62f, 0.33f};
constexpr float kBrassDark[3] = {0.42f, 0.32f, 0.16f};

void quad(float x0, float y0, float x1, float y1) {
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
}

// A vertical gradient from `top` to `bottom` colour.
void gradient(float x0, float y0, float x1, float y1, const float* top, const float* bottom, float alpha) {
    glBegin(GL_QUADS);
    glColor4f(top[0], top[1], top[2], alpha);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glColor4f(bottom[0], bottom[1], bottom[2], alpha);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

void outline(float x0, float y0, float x1, float y1, const float* colour, float alpha, float width) {
    glColor4f(colour[0], colour[1], colour[2], alpha);
    glBegin(GL_QUADS);
    quad(x0, y0, x1, y0 + width);
    quad(x0, y1 - width, x1, y1);
    quad(x0, y0, x0 + width, y1);
    quad(x1 - width, y0, x1, y1);
    glEnd();
}

// A soft shadow: a few widening, fainter rectangles.
void shadow(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
    for (int i = 1; i <= 4; ++i) {
        const auto d = static_cast<float>(i) * 2.0f;
        glColor4f(0, 0, 0, 0.09f);
        quad(x0 + 3 - d * 0.5f, y0 + 5 - d * 0.5f, x1 + 3 + d, y1 + 5 + d);
    }
    glEnd();
}

// A rectangle with its corners cut by `c`.
void chamfered(float x0, float y0, float x1, float y1, float c) {
    glBegin(GL_POLYGON);
    glVertex2f(x0 + c, y0);
    glVertex2f(x1 - c, y0);
    glVertex2f(x1, y0 + c);
    glVertex2f(x1, y1 - c);
    glVertex2f(x1 - c, y1);
    glVertex2f(x0 + c, y1);
    glVertex2f(x0, y1 - c);
    glVertex2f(x0, y0 + c);
    glEnd();
}

// Leave the colour opaque white: some drivers carry the last colour's
// alpha into the next thing drawn (text after a bar came out dim).
void reset() { glColor4f(1, 1, 1, 1); }

} // namespace

void panel(float x0, float y0, float x1, float y1) {
    glDisable(GL_TEXTURE_2D);
    shadow(x0, y0, x1, y1);
    const float top[3] = {0.16f, 0.13f, 0.11f}, bottom[3] = {0.07f, 0.065f, 0.06f};
    gradient(x0, y0, x1, y1, top, bottom, 0.95f);
    // A brass frame, a dark gap, a fine inner line.
    outline(x0, y0, x1, y1, kBrass, 0.95f, 2.0f);
    outline(x0 + 2, y0 + 2, x1 - 2, y1 - 2, kBrassDark, 0.9f, 1.0f);
    outline(x0 + 4, y0 + 4, x1 - 4, y1 - 4, kBrass, 0.35f, 1.0f);
    reset();
}

void rule(float x0, float x1, float y) {
    glBegin(GL_QUADS);
    glColor4f(kBrass[0], kBrass[1], kBrass[2], 0.7f);
    quad(x0, y, x1, y + 1);
    glColor4f(0, 0, 0, 0.5f);
    quad(x0, y + 1, x1, y + 2);
    glEnd();
    reset();
}

void bar(float x0, float y0, float x1, float y1, bool top) {
    glDisable(GL_TEXTURE_2D);
    const float a[3] = {0.17f, 0.14f, 0.11f}, b[3] = {0.08f, 0.07f, 0.06f};
    gradient(x0, y0, x1, y1, top ? a : b, top ? b : a, 0.92f);
    glBegin(GL_QUADS);
    glColor4f(kBrass[0], kBrass[1], kBrass[2], 0.85f);
    if (top) quad(x0, y1 - 1, x1, y1);
    else quad(x0, y0, x1, y0 + 1);
    glColor4f(0, 0, 0, 0.35f);
    if (top) quad(x0, y1, x1, y1 + 2);
    else quad(x0, y0 - 2, x1, y0);
    glEnd();
    reset();
}

void tab(float x0, float y0, float x1, float y1, bool chosen) {
    glDisable(GL_TEXTURE_2D);
    if (chosen) {
        glColor4f(0.95f, 0.8f, 0.45f, 1.0f);
        chamfered(x0, y0, x1, y1, 3);
        const float hi[3] = {0.98f, 0.86f, 0.52f}, lo[3] = {0.74f, 0.56f, 0.26f};
        gradient(x0 + 1, y0 + 1, x1 - 1, y1 - 1, hi, lo, 1.0f);
    } else {
        glColor4f(kBrassDark[0], kBrassDark[1], kBrassDark[2], 0.95f);
        chamfered(x0, y0, x1, y1, 3);
        const float hi[3] = {0.22f, 0.19f, 0.16f}, lo[3] = {0.12f, 0.11f, 0.1f};
        gradient(x0 + 1, y0 + 1, x1 - 1, y1 - 1, hi, lo, 0.95f);
    }
    reset();
}

void chip(float x0, float y0, float x1, float y1, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.05f, 0.045f, 0.04f, alpha);
    chamfered(x0, y0, x1, y1, std::min(4.0f, (y1 - y0) / 3));
    glColor4f(kBrass[0], kBrass[1], kBrass[2], alpha * 0.6f);
    glBegin(GL_QUADS);
    quad(x0 + 4, y1 - 1, x1 - 4, y1);
    glEnd();
    reset();
}

void selection(float x0, float y0, float x1, float y1) {
    const float hi[3] = {0.42f, 0.32f, 0.14f}, lo[3] = {0.28f, 0.21f, 0.09f};
    gradient(x0, y0, x1, y1, hi, lo, 0.9f);
    outline(x0, y0, x1, y1, kBrass, 0.5f, 1.0f);
    reset();
}

} // namespace railmaster::client::ui
