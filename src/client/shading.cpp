#include "shading.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace railmaster::client::shading {

namespace {

// OpenGL past 1.1, loaded at run time (Windows has no other way; it keeps
// Linux and macOS the same).
PFNGLCREATESHADERPROC p_create_shader = nullptr;
PFNGLSHADERSOURCEPROC p_shader_source = nullptr;
PFNGLCOMPILESHADERPROC p_compile_shader = nullptr;
PFNGLGETSHADERIVPROC p_get_shaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC p_get_shader_info_log = nullptr;
PFNGLCREATEPROGRAMPROC p_create_program = nullptr;
PFNGLATTACHSHADERPROC p_attach_shader = nullptr;
PFNGLLINKPROGRAMPROC p_link_program = nullptr;
PFNGLGETPROGRAMIVPROC p_get_programiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC p_get_program_info_log = nullptr;
PFNGLUSEPROGRAMPROC p_use_program = nullptr;
PFNGLGETUNIFORMLOCATIONPROC p_get_uniform_location = nullptr;
PFNGLUNIFORM1IPROC p_uniform1i = nullptr;
PFNGLUNIFORM1FPROC p_uniform1f = nullptr;
PFNGLUNIFORM3FPROC p_uniform3f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC p_uniform_matrix4fv = nullptr;
PFNGLACTIVETEXTUREPROC p_active_texture = nullptr;
PFNGLGENFRAMEBUFFERSPROC p_gen_framebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC p_bind_framebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC p_framebuffer_texture_2d = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC p_check_framebuffer_status = nullptr;

template <typename F> bool load(F& f, const char* name, const char* fallback = nullptr) {
    f = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
    if (!f && fallback) f = reinterpret_cast<F>(SDL_GL_GetProcAddress(fallback));
    return f != nullptr;
}

bool load_all() {
    bool ok = true;
    ok &= load(p_create_shader, "glCreateShader");
    ok &= load(p_shader_source, "glShaderSource");
    ok &= load(p_compile_shader, "glCompileShader");
    ok &= load(p_get_shaderiv, "glGetShaderiv");
    ok &= load(p_get_shader_info_log, "glGetShaderInfoLog");
    ok &= load(p_create_program, "glCreateProgram");
    ok &= load(p_attach_shader, "glAttachShader");
    ok &= load(p_link_program, "glLinkProgram");
    ok &= load(p_get_programiv, "glGetProgramiv");
    ok &= load(p_get_program_info_log, "glGetProgramInfoLog");
    ok &= load(p_use_program, "glUseProgram");
    ok &= load(p_get_uniform_location, "glGetUniformLocation");
    ok &= load(p_uniform1i, "glUniform1i");
    ok &= load(p_uniform1f, "glUniform1f");
    ok &= load(p_uniform3f, "glUniform3f");
    ok &= load(p_uniform_matrix4fv, "glUniformMatrix4fv");
    ok &= load(p_active_texture, "glActiveTexture", "glActiveTextureARB");
    ok &= load(p_gen_framebuffers, "glGenFramebuffers", "glGenFramebuffersEXT");
    ok &= load(p_bind_framebuffer, "glBindFramebuffer", "glBindFramebufferEXT");
    ok &= load(p_framebuffer_texture_2d, "glFramebufferTexture2D", "glFramebufferTexture2DEXT");
    ok &= load(p_check_framebuffer_status, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
    return ok;
}

// Sunlight from high in the north-west, as the fixed-function light (map
// axes: x east, y south, z up), pointing towards the sun.
constexpr float kSun[3] = {-0.45f, -0.55f, 0.75f};
constexpr float kHorizon[3] = {0.74f, 0.82f, 0.9f};
constexpr float kZenith[3] = {0.3f, 0.5f, 0.8f};
constexpr int kShadowSize = 2048;

const char* const kVertex = R"(#version 120
varying vec3 v_world;
varying vec3 v_normal;
varying vec4 v_color;
varying vec2 v_uv;
varying float v_depth;
uniform mat4 u_view_inv;
void main() {
    // Back from eye space to the map, so models drawn with their own
    // transforms are lit and shadowed where they stand.
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    v_world = (u_view_inv * eye).xyz;
    v_normal = mat3(u_view_inv) * (gl_NormalMatrix * gl_Normal);
    v_color = gl_Color;
    v_uv = gl_MultiTexCoord0.xy;
    v_depth = -eye.z;
    gl_Position = ftransform();
}
)";

const char* const kFragment = R"(#version 120
uniform sampler2D u_tex;
uniform sampler2D u_shadow;
uniform mat4 u_light;
uniform vec3 u_sun;
uniform int u_lit;
uniform int u_shadows;
uniform float u_texel;
varying vec3 v_world;
varying vec3 v_normal;
varying vec4 v_color;
varying vec2 v_uv;
varying float v_depth;

// How much sun reaches a point: 3x3 samples of the shadow map, fading out
// towards the map's edge so its end never shows as a line.
float sunlit(vec3 n) {
    vec4 lp = u_light * vec4(v_world + n * 0.015, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    float edge = max(abs(p.x - 0.5), abs(p.y - 0.5));
    if (edge >= 0.5 || p.z >= 1.0) return 1.0;
    float lit = 0.0;
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            lit += p.z - 0.0012 <= texture2D(u_shadow, p.xy + vec2(float(i), float(j)) * u_texel).r ? 1.0 : 0.0;
    return mix(lit / 9.0, 1.0, smoothstep(0.4, 0.5, edge));
}

void main() {
    vec4 base = v_color * texture2D(u_tex, v_uv);
    vec3 rgb = base.rgb;
    if (u_lit == 1) {
        vec3 n = length(v_normal) > 0.0001 ? normalize(v_normal) : vec3(0.0, 0.0, 1.0);
        float ndl = max(dot(n, u_sun), 0.0);
        float sun = (u_shadows == 1 && ndl > 0.0) ? sunlit(n) : 1.0;
        // Sky light from above, warmer light bounced off the ground below.
        vec3 ambient = mix(vec3(0.26, 0.25, 0.21), vec3(0.4, 0.44, 0.52), n.z * 0.5 + 0.5);
        rgb = base.rgb * (ambient + vec3(0.98, 0.92, 0.8) * 0.8 * ndl * sun);
    }
    float haze = clamp((gl_Fog.end - v_depth) / (gl_Fog.end - gl_Fog.start), 0.0, 1.0);
    gl_FragColor = vec4(mix(gl_Fog.color.rgb, rgb, haze), base.a);
}
)";

bool g_active = false;
bool g_in_world = false;
GLuint g_program = 0, g_white = 0, g_shadow_tex = 0, g_shadow_fbo = 0;
GLint u_view_inv = -1, u_tex = -1, u_shadow = -1, u_light = -1, u_sun = -1, u_lit = -1, u_shadows = -1, u_texel = -1;
std::array<float, 16> g_light{}; // map tiles to the shadow map's clip space
bool g_shadows_ready = false;

GLuint compile(GLenum kind, const char* src) {
    const GLuint s = p_create_shader(kind);
    p_shader_source(s, 1, &src, nullptr);
    p_compile_shader(s);
    GLint ok = 0;
    p_get_shaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        p_get_shader_info_log(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader compile failed: %s\n", log);
        return 0;
    }
    return s;
}

using Mat = std::array<float, 16>; // column-major

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

} // namespace

bool init(bool wanted) {
    // The white texture is used either way: the terrain list binds it back.
    glGenTextures(1, &g_white);
    glBindTexture(GL_TEXTURE_2D, g_white);
    const unsigned char white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (!wanted) return false;
    if (!load_all()) {
        std::fprintf(stderr, "shaders: OpenGL functions missing; using fixed-function lighting\n");
        return false;
    }
    const GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
    if (vs == 0 || fs == 0) return false;
    g_program = p_create_program();
    p_attach_shader(g_program, vs);
    p_attach_shader(g_program, fs);
    p_link_program(g_program);
    GLint ok = 0;
    p_get_programiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        p_get_program_info_log(g_program, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader link failed: %s\n", log);
        return false;
    }
    u_view_inv = p_get_uniform_location(g_program, "u_view_inv");
    u_tex = p_get_uniform_location(g_program, "u_tex");
    u_shadow = p_get_uniform_location(g_program, "u_shadow");
    u_light = p_get_uniform_location(g_program, "u_light");
    u_sun = p_get_uniform_location(g_program, "u_sun");
    u_lit = p_get_uniform_location(g_program, "u_lit");
    u_shadows = p_get_uniform_location(g_program, "u_shadows");
    u_texel = p_get_uniform_location(g_program, "u_texel");

    // The shadow map: a depth texture as a framebuffer's only attachment.
    glGenTextures(1, &g_shadow_tex);
    glBindTexture(GL_TEXTURE_2D, g_shadow_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_gen_framebuffers(1, &g_shadow_fbo);
    p_bind_framebuffer(GL_FRAMEBUFFER, g_shadow_fbo);
    p_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_shadow_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    g_shadows_ready = p_check_framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    p_bind_framebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, g_white);
    if (!g_shadows_ready) std::fprintf(stderr, "shaders: no shadow framebuffer; lighting without shadows\n");
    g_active = true;
    return true;
}

bool active() { return g_active; }
unsigned white_texture() { return g_white; }

void horizon_rgb(float& r, float& g, float& b) {
    r = kHorizon[0];
    g = kHorizon[1];
    b = kHorizon[2];
}

void begin_shadow_pass(Camera& cam) {
    if (!g_active || !g_shadows_ready) return;
    // An orthographic view down the sun's rays onto the area round the
    // target, wider as the camera pulls back.
    const float extent = std::clamp(cam.distance * 1.6f, 10.0f, 160.0f);
    const float tx = cam.target_x, ty = cam.target_y, tz = cam.ground_z(tx, ty);
    const float len = std::sqrt(kSun[0] * kSun[0] + kSun[1] * kSun[1] + kSun[2] * kSun[2]);
    const float lx = kSun[0] / len, ly = kSun[1] / len, lz = kSun[2] / len;
    const float reach = extent * 3.0f + 60.0f;
    // look_at from the sun's side towards the target, z up.
    const float fx = -lx, fy = -ly, fz = -lz; // forward
    float sx = fy * 1 - fz * 0, sy = fz * 0 - fx * 1, sz = fx * 0 - fy * 0; // forward x up(0,0,1)
    const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    const float ex = tx + lx * reach, ey = ty + ly * reach, ez = tz + lz * reach;
    Mat view{};
    view[0] = sx; view[4] = sy; view[8] = sz;
    view[1] = ux; view[5] = uy; view[9] = uz;
    view[2] = -fx; view[6] = -fy; view[10] = -fz;
    view[12] = -(sx * ex + sy * ey + sz * ez);
    view[13] = -(ux * ex + uy * ey + uz * ez);
    view[14] = fx * ex + fy * ey + fz * ez;
    view[15] = 1;
    const float n = 1.0f, f = reach * 2.0f;
    Mat proj{};
    proj[0] = 1.0f / extent;
    proj[5] = 1.0f / extent;
    proj[10] = -2.0f / (f - n);
    proj[14] = -(f + n) / (f - n);
    proj[15] = 1;
    g_light = multiply(proj, view);

    p_bind_framebuffer(GL_FRAMEBUFFER, g_shadow_fbo);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.data());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.data());
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
}

void end_shadow_pass(Camera& cam) {
    if (!g_active || !g_shadows_ready) return;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    p_bind_framebuffer(GL_FRAMEBUFFER, 0);
    cam.apply();
}

void draw_sky(Camera& cam) {
    cam.apply_screen();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glColor3fv(kZenith);
    glVertex2f(0, 0);
    glVertex2f(static_cast<float>(cam.width_px), 0);
    glColor3fv(kHorizon);
    glVertex2f(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px) * 0.6f);
    glVertex2f(0, static_cast<float>(cam.height_px) * 0.6f);
    glVertex2f(0, static_cast<float>(cam.height_px) * 0.6f);
    glVertex2f(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px) * 0.6f);
    glVertex2f(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px));
    glVertex2f(0, static_cast<float>(cam.height_px));
    glEnd();
    glDepthMask(GL_TRUE);
    cam.apply();
}

void begin_world(const Camera& cam) {
    if (!g_active) return;
    p_use_program(g_program);
    p_uniform_matrix4fv(u_view_inv, 1, GL_FALSE, cam.view_inverse().data());
    g_in_world = true;
    p_active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_shadow_tex);
    p_active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_white);
    p_uniform1i(u_tex, 0);
    p_uniform1i(u_shadow, 1);
    p_uniform_matrix4fv(u_light, 1, GL_FALSE, g_light.data());
    const float len = std::sqrt(kSun[0] * kSun[0] + kSun[1] * kSun[1] + kSun[2] * kSun[2]);
    p_uniform3f(u_sun, kSun[0] / len, kSun[1] / len, kSun[2] / len);
    p_uniform1i(u_lit, 0);
    p_uniform1i(u_shadows, g_shadows_ready ? 1 : 0);
    p_uniform1f(u_texel, 1.0f / kShadowSize);
}

void set_lit(bool lit) {
    if (g_active && g_in_world) p_uniform1i(u_lit, lit ? 1 : 0);
}

void end_world() {
    if (!g_active) return;
    p_use_program(0);
    g_in_world = false;
}

} // namespace railmaster::client::shading
