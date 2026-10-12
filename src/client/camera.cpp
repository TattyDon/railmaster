#include "camera.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace railmaster::client {

namespace {

using Mat = std::array<float, 16>; // column-major, as OpenGL

Mat multiply(const Mat& a, const Mat& b) {
    Mat r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    return r;
}

Mat perspective(float fov_y, float aspect, float near_z, float far_z) {
    const float f = 1.0f / std::tan(fov_y / 2);
    Mat m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far_z + near_z) / (near_z - far_z);
    m[11] = -1;
    m[14] = 2 * far_z * near_z / (near_z - far_z);
    return m;
}

struct Vec {
    float x, y, z;
};
Vec sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec norm(Vec a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? Vec{a.x / l, a.y / l, a.z / l} : a;
}

Mat look_at(Vec eye, Vec at, Vec up) {
    const Vec f = norm(sub(at, eye));
    const Vec s = norm(cross(f, up));
    const Vec u = cross(s, f);
    Mat m{};
    m[0] = s.x;
    m[4] = s.y;
    m[8] = s.z;
    m[1] = u.x;
    m[5] = u.y;
    m[9] = u.z;
    m[2] = -f.x;
    m[6] = -f.y;
    m[10] = -f.z;
    m[12] = -dot(s, eye);
    m[13] = -dot(u, eye);
    m[14] = dot(f, eye);
    m[15] = 1;
    return m;
}

// Invert a 4x4 matrix (general case, by cofactors).
bool invert(const Mat& m, Mat& out) {
    Mat inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    for (std::size_t i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

} // namespace

float Camera::z_of(std::int64_t z_mm) const {
    if (!three_d) return 0.0f;
    return static_cast<float>(z_mm) / mm_per_tile * height_exaggeration;
}

float Camera::ground_z(float tx, float ty) const {
    if (!three_d || !terrain) return 0.0f;
    const auto x = static_cast<std::int64_t>(std::clamp(tx, 0.0f, static_cast<float>(terrain->width())) * mm_per_tile);
    const auto y = static_cast<std::int64_t>(std::clamp(ty, 0.0f, static_cast<float>(terrain->height())) * mm_per_tile);
    return z_of(terrain->height_at_mm({x, y}));
}

void Camera::apply() {
    glViewport(0, 0, drawable_w > 0 ? drawable_w : width_px, drawable_h > 0 ? drawable_h : height_px);
    if (!three_d) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width_px, height_px, 0, -1000, 1000);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glScalef(zoom, zoom, 1.0f);
        glTranslatef(-pan_x, -pan_y, 0.0f);
        return;
    }
    // Map tiles have y running south: flip it so the view maths is
    // right-handed (x east, y north, z up).
    const float gz = ground_z(target_x, target_y);
    const Vec at{target_x, -target_y, gz};
    const Vec eye{at.x - distance * std::cos(pitch) * std::sin(yaw), at.y - distance * std::cos(pitch) * std::cos(yaw),
                  at.z + distance * std::sin(pitch)};
    const float aspect = static_cast<float>(width_px) / static_cast<float>(std::max(1, height_px));
    const Mat proj = perspective(fov_y, aspect, std::max(0.05f, distance * 0.02f), distance * 20.0f + 500.0f);
    Mat flip{};
    flip[0] = 1;
    flip[5] = -1;
    flip[10] = 1;
    flip[15] = 1;
    const Mat view = multiply(look_at(eye, at, {0, 0, 1}), flip);
    view_proj_ = multiply(proj, view);
    invert(view, view_inverse_);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.data());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.data());
    // Pixels per tile at the point looked at, for things sized on screen.
    zoom = static_cast<float>(height_px) / (2.0f * std::tan(fov_y / 2) * distance);
}

void Camera::apply_screen() const {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width_px, height_px, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void Camera::project(float x, float y, float z, float& sx, float& sy, float& w) const {
    const Mat& m = view_proj_;
    const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
    const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    w = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (w <= 1e-6f) return;
    sx = (cx / w + 1) * 0.5f * static_cast<float>(width_px);
    sy = (1 - cy / w) * 0.5f * static_cast<float>(height_px);
}

bool Camera::to_screen_at(sim::MapPoint p, std::int64_t z_mm, float& sx, float& sy) const {
    const float x = static_cast<float>(p.x_mm) / mm_per_tile, y = static_cast<float>(p.y_mm) / mm_per_tile;
    if (!three_d) {
        sx = (x - pan_x) * zoom;
        sy = (y - pan_y) * zoom;
        return true;
    }
    float w = 0;
    project(x, y, z_of(z_mm), sx, sy, w);
    if (w <= 1e-6f) {
        sx = sy = -100000.0f;
        return false;
    }
    return true;
}

bool Camera::to_screen(sim::MapPoint p, float& sx, float& sy) const {
    const std::int64_t z = terrain && three_d ? terrain->height_at_mm(p) : 0;
    return to_screen_at(p, z, sx, sy);
}

sim::MapPoint Camera::to_map(float sx, float sy) const {
    if (!three_d) {
        return {static_cast<std::int64_t>((sx / zoom + pan_x) * mm_per_tile),
                static_cast<std::int64_t>((sy / zoom + pan_y) * mm_per_tile)};
    }
    Mat inv{};
    if (!invert(view_proj_, inv)) return {static_cast<std::int64_t>(target_x * mm_per_tile), static_cast<std::int64_t>(target_y * mm_per_tile)};
    const float nx = sx / static_cast<float>(width_px) * 2 - 1, ny = 1 - sy / static_cast<float>(height_px) * 2;
    const auto unproject = [&](float nz) {
        const float x = inv[0] * nx + inv[4] * ny + inv[8] * nz + inv[12];
        const float y = inv[1] * nx + inv[5] * ny + inv[9] * nz + inv[13];
        const float z = inv[2] * nx + inv[6] * ny + inv[10] * nz + inv[14];
        const float w = inv[3] * nx + inv[7] * ny + inv[11] * nz + inv[15];
        return Vec{x / w, y / w, z / w};
    };
    const Vec a = unproject(-1), b = unproject(1);
    const Vec d = norm(sub(b, a));
    // March along the ray until it goes under the ground, then narrow it down.
    const float step = std::max(0.1f, distance / 400.0f);
    const float reach = distance * 20.0f + 500.0f;
    Vec prev = a;
    for (float t = 0; t < reach; t += step) {
        const Vec p{a.x + d.x * t, a.y + d.y * t, a.z + d.z * t};
        if (p.z <= ground_z(p.x, p.y)) {
            Vec lo = prev, hi = p;
            for (int i = 0; i < 20; ++i) {
                const Vec mid{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
                if (mid.z <= ground_z(mid.x, mid.y)) hi = mid;
                else lo = mid;
            }
            return {static_cast<std::int64_t>(hi.x * mm_per_tile), static_cast<std::int64_t>(hi.y * mm_per_tile)};
        }
        prev = p;
    }
    // Off the edge of the map: where the ray meets sea level.
    const float t = d.z < 0 ? -a.z / d.z : 0;
    return {static_cast<std::int64_t>((a.x + d.x * t) * mm_per_tile), static_cast<std::int64_t>((a.y + d.y * t) * mm_per_tile)};
}

void Camera::zoom_at(float sx, float sy, float factor) {
    if (three_d) {
        distance = std::clamp(distance / factor, 3.0f, 600.0f);
        return;
    }
    const float tx = sx / zoom + pan_x, ty = sy / zoom + pan_y;
    zoom = std::clamp(zoom * factor, 1.0f, 200.0f);
    pan_x = tx - sx / zoom;
    pan_y = ty - sy / zoom;
}

void Camera::center_on(sim::MapPoint p) {
    const float x = static_cast<float>(p.x_mm) / mm_per_tile, y = static_cast<float>(p.y_mm) / mm_per_tile;
    target_x = x;
    target_y = y;
    pan_x = x - static_cast<float>(width_px) / (2 * zoom);
    pan_y = y - static_cast<float>(height_px) / (2 * zoom);
}

void Camera::pan(float dx, float dy) {
    if (!three_d) {
        pan_x += dx;
        pan_y += dy;
        return;
    }
    // Screen right and screen down (away from the camera's facing), as map
    // directions turned by the yaw; dx and dy are in tiles.
    const float c = std::cos(yaw), s = std::sin(yaw);
    target_x += dx * c - dy * s;
    target_y += dx * s + dy * c;
    if (terrain) {
        target_x = std::clamp(target_x, 0.0f, static_cast<float>(terrain->width()));
        target_y = std::clamp(target_y, 0.0f, static_cast<float>(terrain->height()));
    }
}

void Camera::orbit(float dyaw, float dpitch) {
    yaw += dyaw;
    pitch = std::clamp(pitch + dpitch, 0.25f, 1.5f);
}

} // namespace railmaster::client
