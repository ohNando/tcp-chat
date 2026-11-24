#include "wallpaper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <tuple>
#include <sys/wait.h>
#include <vector>

#include <GLFW/glfw3.h> // OpenGL 1.1 texture calls
#include "stb_image.h"

namespace {

const WallpaperInfo LIST[] = {
    { "dots", "Dots" },       { "grid", "Grid" },     { "waves", "Waves" },
    { "bubbles", "Bubbles" }, { "aurora", "Aurora" }, { "dusk", "Dusk" },
};

/* ------------------------------------------------------------------ */
/* A small float image with premultiplied alpha                       */
/* ------------------------------------------------------------------ */

struct Img {
    int w = 0, h = 0;
    std::vector<float> px; // r g b a, premultiplied

    Img(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_ * 4, 0.0f) {}
    float* at(int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; }

    void fill(const ImVec4& c) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float* p = at(x, y);
                p[0] = c.x, p[1] = c.y, p[2] = c.z, p[3] = 1;
            }
    }
    // Source-over with alpha a.
    void over(int x, int y, const ImVec4& c, float a) {
        float* p = at(x, y);
        p[0] = c.x * a + p[0] * (1 - a);
        p[1] = c.y * a + p[1] * (1 - a);
        p[2] = c.z * a + p[2] * (1 - a);
        p[3] = a + p[3] * (1 - a);
    }
    // Paints a shape given as an inside test, antialiased with 4x4 samples.
    void shape(const std::function<bool(float, float)>& inside, const ImVec4& c, float alpha) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int hits = 0;
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++)
                        hits += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
                if (hits) over(x, y, c, alpha * hits / 16.0f);
            }
    }
    std::vector<unsigned char> rgba8() const {
        std::vector<unsigned char> out(px.size());
        for (size_t i = 0; i < px.size(); i += 4) {
            float a = px[i + 3];
            for (int c = 0; c < 3; c++)
                out[i + c] = static_cast<unsigned char>(std::clamp(a > 0 ? px[i + c] / a : 0.0f, 0.0f, 1.0f) * 255 + 0.5f);
            out[i + 3] = static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255 + 0.5f);
        }
        return out;
    }
};

// Gaussian blur (CSS blur(sigma)) as three box blurs, per axis. `wrap` for
// repeating tiles; edges clamp otherwise.
void blur(Img& img, float sigma, bool wrap) {
    if (sigma <= 0.25f) return;
    // Box sizes for three passes (Ivan Kutskir's boxesForGauss).
    int wi = static_cast<int>(std::floor(std::sqrt(12 * sigma * sigma / 3 + 1)));
    if (wi % 2 == 0) wi--;
    int wu = wi + 2;
    int m = static_cast<int>(std::round((12 * sigma * sigma - 3 * wi * wi - 12 * wi - 9) / (-4.0f * wi - 4)));
    int boxes[3];
    for (int i = 0; i < 3; i++) boxes[i] = ((i < m ? wi : wu) - 1) / 2;

    std::vector<float> tmp(img.px.size());
    auto pass = [&](int r, bool horizontal) {
        int len = horizontal ? img.w : img.h, lines = horizontal ? img.h : img.w;
        auto idx = [&](int line, int i) {
            if (wrap) i = ((i % len) + len) % len;
            else i = std::clamp(i, 0, len - 1);
            return horizontal ? (static_cast<size_t>(line) * img.w + i) * 4 : (static_cast<size_t>(i) * img.w + line) * 4;
        };
        float inv = 1.0f / (2 * r + 1);
        for (int line = 0; line < lines; line++)
            for (int c = 0; c < 4; c++) {
                float acc = 0;
                for (int i = -r; i <= r; i++) acc += img.px[idx(line, i) + c];
                for (int i = 0; i < len; i++) {
                    tmp[idx(line, i) + c] = acc * inv;
                    acc += img.px[idx(line, i + r + 1) + c] - img.px[idx(line, i - r) + c];
                }
            }
        img.px.swap(tmp);
    };
    for (int r : boxes) {
        pass(r, true);
        pass(r, false);
    }
}

float seg_dist2(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = std::clamp((wx * vx + wy * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
    float dx = wx - t * vx, dy = wy - t * vy;
    return dx * dx + dy * dy;
}

// Inside test for a stroked quadratic Bezier (SVG "Q").
std::function<bool(float, float)> stroke_quad(ImVec2 a, ImVec2 c, ImVec2 b, float width) {
    std::vector<ImVec2> pts;
    for (int i = 0; i <= 24; i++) {
        float t = i / 24.0f, u = 1 - t;
        pts.push_back(ImVec2(u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y));
    }
    float r2 = width * width / 4;
    return [pts, r2](float x, float y) {
        for (size_t i = 1; i < pts.size(); i++)
            if (seg_dist2(x, y, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y) <= r2) return true;
        return false;
    };
}

std::function<bool(float, float)> circle(float cx, float cy, float r) {
    return [=](float x, float y) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; };
}

// The SVG tiles of wallpapers.js on the panel-alt color.
Img make_tile(const std::string& id, const Palette& t) {
    if (id == "dots") {
        Img img(22, 22);
        img.fill(t.panel_alt);
        img.shape(circle(4, 4, 1.6f), t.accent, 0.35f);
        return img;
    }
    if (id == "grid") { // path M32 0H0V32, 1px stroke on the tile edges
        Img img(32, 32);
        img.fill(t.panel_alt);
        img.shape([](float x, float y) { return y <= 0.5f || x <= 0.5f; }, t.muted, 0.18f);
        return img;
    }
    if (id == "waves") { // M0 20 Q15 8 30 20 T60 20 (and the same at 45)
        Img img(60, 60);
        img.fill(t.panel_alt);
        for (auto [dy, color, alpha] : { std::tuple<float, ImVec4, float>{ 20, t.accent, 0.22f }, { 45, t.dm, 0.18f } }) {
            auto first = stroke_quad(ImVec2(0, dy), ImVec2(15, dy - 12), ImVec2(30, dy), 2);
            auto second = stroke_quad(ImVec2(30, dy), ImVec2(45, dy + 12), ImVec2(60, dy), 2);
            img.shape([&](float x, float y) { return first(x, y) || second(x, y); }, color, alpha);
        }
        return img;
    }
    // bubbles
    Img img(120, 120);
    img.fill(t.panel_alt);
    img.shape(circle(20, 22, 10), t.accent, 0.1f);
    img.shape(circle(70, 60, 16), t.dm, 0.09f);
    img.shape(circle(95, 15, 6), t.own_bubble, 0.14f);
    img.shape(circle(35, 95, 7), t.accent, 0.12f);
    return img;
}

// CSS gradients of wallpapers.js for an area of w x h CSS px, sampled at
// `scale` pixels per CSS px.
Img make_gradient(const std::string& id, const Palette& t, float w, float h, float scale) {
    int iw = std::max(8, static_cast<int>(w * scale)), ih = std::max(8, static_cast<int>(h * scale));
    Img img(iw, ih);
    for (int y = 0; y < ih; y++)
        for (int x = 0; x < iw; x++) {
            float px = (x + 0.5f) / scale, py = (y + 0.5f) / scale;
            if (id == "dusk") {
                // linear-gradient(160deg, #2b1055 0%, #7597de 55%, #f6b38e 100%)
                const float ang = 160.0f * 3.14159265f / 180.0f;
                float dx = std::sin(ang), dy = -std::cos(ang);
                float len = std::fabs(w * dx) + std::fabs(h * dy);
                float tt = std::clamp(((px - w / 2) * dx + (py - h / 2) * dy) / len + 0.5f, 0.0f, 1.0f);
                ImVec4 a = rgb_to_vec(0x2b1055), m = rgb_to_vec(0x7597de), b = rgb_to_vec(0xf6b38e);
                auto mix = [](ImVec4 u, ImVec4 v, float k) {
                    return ImVec4(u.x + (v.x - u.x) * k, u.y + (v.y - u.y) * k, u.z + (v.z - u.z) * k, 1);
                };
                ImVec4 c = tt < 0.55f ? mix(a, m, tt / 0.55f) : mix(m, b, (tt - 0.55f) / 0.45f);
                float* p = img.at(x, y);
                p[0] = c.x, p[1] = c.y, p[2] = c.z, p[3] = 1;
                continue;
            }
            // aurora: three radial-gradient(ellipse at X Y, color, transparent N%)
            // layers over panel-alt; the first listed is on top.
            float* p = img.at(x, y);
            p[0] = t.panel_alt.x, p[1] = t.panel_alt.y, p[2] = t.panel_alt.z, p[3] = 1;
            struct Layer { float cx, cy; ImVec4 c; float a, stop; };
            const Layer layers[] = {
                { 0.50f, 1.00f, t.own_bubble, 0.30f, 0.60f },
                { 0.85f, 0.30f, t.dm, 0.30f, 0.55f },
                { 0.15f, 0.10f, t.accent, 0.35f, 0.55f },
            };
            for (const Layer& l : layers) {
                // ellipse, farthest-corner: the farthest-side ellipse times sqrt(2).
                float cx = l.cx * w, cy = l.cy * h;
                float rx = std::max(cx, w - cx) * 1.41421356f, ry = std::max(cy, h - cy) * 1.41421356f;
                float d = std::sqrt((px - cx) * (px - cx) / (rx * rx) + (py - cy) * (py - cy) / (ry * ry));
                float a = d >= l.stop ? 0 : l.a * (1 - d / l.stop);
                if (a > 0) img.over(x, y, l.c, a);
            }
        }
    return img;
}

std::string hex(const ImVec4& c) {
    char b[8];
    std::snprintf(b, sizeof(b), "%06x", static_cast<unsigned>(vec_to_rgb(c)));
    return b;
}

} // namespace

const WallpaperInfo* wallpaper_list(int* count) {
    *count = static_cast<int>(sizeof(LIST) / sizeof(LIST[0]));
    return LIST;
}

/* ------------------------------------------------------------------ */
/* Texture                                                             */
/* ------------------------------------------------------------------ */

void Texture::reset() {
    if (id_) {
        GLuint t = id_;
        glDeleteTextures(1, &t);
    }
    id_ = 0;
    w_ = h_ = 0;
}

void Texture::loadPixels(const unsigned char* px, int w, int h) {
    reset();
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    id_ = t;
    w_ = w;
    h_ = h;
}

/* ------------------------------------------------------------------ */
/* Renderer                                                            */
/* ------------------------------------------------------------------ */

const std::string& WallpaperRenderer::syncCustom(const Settings& s) {
    if (s.wallpaper_path == custom_path_) return custom_error_;
    custom_path_ = s.wallpaper_path;
    custom_error_.clear();
    image_.clear();
    cache_.clear();
    if (custom_path_.empty()) return custom_error_;

    int w, h, n;
    unsigned char* px = stbi_load(custom_path_.c_str(), &w, &h, &n, 4);
    if (!px) {
        custom_error_ = std::string("Cannot open this image: ") + stbi_failure_reason();
        return custom_error_;
    }
    // Like the web's shrinkImage(): at most 1920 px on the long side.
    float k = std::min(1.0f, 1920.0f / std::max(w, h));
    image_w_ = std::max(1, static_cast<int>(w * k));
    image_h_ = std::max(1, static_cast<int>(h * k));
    image_.resize(static_cast<size_t>(image_w_) * image_h_ * 4);
    for (int y = 0; y < image_h_; y++)
        for (int x = 0; x < image_w_; x++) {
            const unsigned char* s = px + (static_cast<size_t>(y * h / image_h_) * w + x * w / image_w_) * 4;
            std::copy(s, s + 4, &image_[(static_cast<size_t>(y) * image_w_ + x) * 4]);
        }
    stbi_image_free(px);
    return custom_error_;
}

Texture& WallpaperRenderer::cached(const std::string& key) {
    if (cache_.size() > 48 && !cache_.count(key)) cache_.clear(); // old sizes and colors
    return cache_[key];
}

bool WallpaperRenderer::draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Settings& s, const Palette& p, bool effects,
                             float rounding) {
    const std::string& id = s.wallpaper;
    if (id.empty() || id == "none") return false;
    if (id == "custom") {
        syncCustom(s);
        if (image_.empty()) return false;
    }
    const float w = p1.x - p0.x, h = p1.y - p0.y;
    const float blur_px = effects ? s.blur : 0;
    // filter: blur(); transform: scale(1.08) hides the blurred edges.
    const float zoom = blur_px > 0 ? 1.08f : 1.0f;
    ImVec2 center((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
    auto zoomed = [&](ImVec2 q) { return ImVec2(center.x + (q.x - center.x) * zoom, center.y + (q.y - center.y) * zoom); };

    dl->PushClipRect(p0, p1, true);
    char blur_key[16];
    std::snprintf(blur_key, sizeof(blur_key), "%.0f", blur_px);

    if (id == "custom") {
        // background: center / cover
        float k = std::max(w / image_w_, h / image_h_);
        float iw = image_w_ * k, ih = image_h_ * k;
        ImVec2 i0(p0.x + (w - iw) / 2, p0.y + (h - ih) / 2), i1(i0.x + iw, i0.y + ih);
        std::string key = "custom " + std::string(blur_key);
        if (blur_px > 0) key += " " + std::to_string(static_cast<int>(iw / 32));
        Texture& t = cached(key);
        if (!t.valid()) {
            if (blur_px <= 0) {
                t.loadPixels(image_.data(), image_w_, image_h_);
            } else {
                // Blur at half the displayed size: cheaper, same result.
                int bw = std::max(8, static_cast<int>(iw / 2)), bh = std::max(8, static_cast<int>(ih / 2));
                Img img(bw, bh);
                for (int y = 0; y < bh; y++)
                    for (int x = 0; x < bw; x++) {
                        const unsigned char* q =
                            &image_[(static_cast<size_t>(y * image_h_ / bh) * image_w_ + x * image_w_ / bw) * 4];
                        float* o = img.at(x, y);
                        o[0] = q[0] / 255.0f, o[1] = q[1] / 255.0f, o[2] = q[2] / 255.0f, o[3] = 1;
                    }
                blur(img, blur_px / 2, false);
                std::vector<unsigned char> px = img.rgba8();
                t.loadPixels(px.data(), bw, bh);
            }
        }
        if (rounding > 0) { // a swatch: crop to the box by UVs, then round it
            ImVec2 uv0((p0.x - i0.x) / iw, (p0.y - i0.y) / ih), uv1((p1.x - i0.x) / iw, (p1.y - i0.y) / ih);
            dl->AddImageRounded(t.id(), p0, p1, uv0, uv1, IM_COL32_WHITE, rounding);
        } else {
            dl->AddImage(t.id(), zoomed(i0), zoomed(i1));
        }
    } else if (id == "aurora" || id == "dusk") {
        char key[96];
        std::snprintf(key, sizeof(key), "%s %s%s%s%s %.0fx%.0f %s", id.c_str(), hex(p.panel_alt).c_str(),
                      hex(p.accent).c_str(), hex(p.dm).c_str(), hex(p.own_bubble).c_str(), w, h, blur_key);
        Texture& t = cached(key);
        if (!t.valid()) {
            const float scale = 0.5f;
            Img img = make_gradient(id, p, w, h, scale);
            blur(img, blur_px * scale, false);
            std::vector<unsigned char> px = img.rgba8();
            t.loadPixels(px.data(), img.w, img.h);
        }
        if (rounding > 0) dl->AddImageRounded(t.id(), p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
        else dl->AddImage(t.id(), zoomed(p0), zoomed(p1));
    } else {
        // The tile repeated over the whole area (background-position 0 0),
        // as one texture: the GL backend's sampler would clamp a repeating UV.
        char key[128];
        std::snprintf(key, sizeof(key), "%s %s%s%s%s%s %.0fx%.0f %s", id.c_str(), hex(p.panel_alt).c_str(),
                      hex(p.accent).c_str(), hex(p.muted).c_str(), hex(p.dm).c_str(), hex(p.own_bubble).c_str(), w,
                      h, blur_key);
        Texture& t = cached(key);
        if (!t.valid()) {
            Img tile = make_tile(id, p);
            blur(tile, blur_px, true);
            std::vector<unsigned char> px = tile.rgba8();
            int aw = std::max(1, static_cast<int>(std::ceil(w))), ah = std::max(1, static_cast<int>(std::ceil(h)));
            std::vector<unsigned char> area(static_cast<size_t>(aw) * ah * 4);
            for (int y = 0; y < ah; y++)
                for (int x = 0; x < aw; x++)
                    std::copy_n(&px[(static_cast<size_t>(y % tile.h) * tile.w + x % tile.w) * 4], 4,
                                &area[(static_cast<size_t>(y) * aw + x) * 4]);
            t.loadPixels(area.data(), aw, ah);
        }
        ImVec2 uv1(w / t.width(), h / t.height());
        if (rounding > 0) dl->AddImageRounded(t.id(), p0, p1, ImVec2(0, 0), uv1, IM_COL32_WHITE, rounding);
        else dl->AddImage(t.id(), zoomed(p0), zoomed(p1), ImVec2(0, 0), uv1);
    }
    if (effects) dl->AddRectFilled(p0, p1, to_u32(p.bg, s.dim), rounding); // .wallpaper-dim
    dl->PopClipRect();
    return true;
}

/* ------------------------------------------------------------------ */
/* File dialog                                                         */
/* ------------------------------------------------------------------ */

std::string pick_image_file(bool* no_dialog) {
    static const char* commands[] = {
        "zenity --file-selection --title='Choose a background image' "
        "--file-filter='Images | *.png *.jpg *.jpeg *.bmp *.gif' 2>/dev/null",
        "kdialog --getopenfilename ~ 'image/png image/jpeg image/bmp image/gif' 2>/dev/null",
    };
    *no_dialog = false;
    for (const char* cmd : commands) {
        FILE* f = popen(cmd, "r");
        if (!f) continue;
        char buf[4096];
        std::string out;
        while (fgets(buf, sizeof(buf), f)) out += buf;
        int rc = pclose(f);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (rc == 0) return out;                               // chosen
        if (WIFEXITED(rc) && WEXITSTATUS(rc) == 1) return "";  // cancelled
        // 127 or similar: tool not installed, try the next one
    }
    *no_dialog = true;
    return "";
}
