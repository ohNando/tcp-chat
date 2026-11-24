#include "wallpaper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/wait.h>
#include <vector>

#include <GLFW/glfw3.h> // OpenGL 1.1 texture calls
#include "stb_image.h"

namespace {

const WallpaperInfo LIST[] = {
    { "dots", "Dots" },       { "grid", "Grid" },     { "waves", "Waves" },
    { "bubbles", "Bubbles" }, { "aurora", "Aurora" }, { "dusk", "Dusk" },
};

ImU32 rgba(const ImVec4& c, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a));
}

// Soft radial spot for the aurora gradient: 1 at the center, 0 at radius.
float spot(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    float d = std::sqrt(dx * dx + dy * dy);
    return std::max(0.0f, 1.0f - d);
}

ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1);
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

bool Texture::loadFile(const std::string& path, std::string* error) {
    int w, h, n;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) {
        if (error) *error = std::string("Cannot open image: ") + stbi_failure_reason();
        return false;
    }
    // Keep GPU memory reasonable for huge photos: sample down to 2048 px.
    const int max = 2048;
    if (w > max || h > max) {
        float k = static_cast<float>(max) / std::max(w, h);
        int nw = std::max(1, static_cast<int>(w * k)), nh = std::max(1, static_cast<int>(h * k));
        std::vector<unsigned char> small(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const unsigned char* s = px + (static_cast<size_t>(y * h / nh) * w + x * w / nw) * 4;
                std::copy(s, s + 4, &small[(static_cast<size_t>(y) * nw + x) * 4]);
            }
        loadPixels(small.data(), nw, nh);
    } else {
        loadPixels(px, w, h);
    }
    stbi_image_free(px);
    return true;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

const std::string& WallpaperRenderer::syncCustom(const Settings& s) {
    if (s.wallpaper_path != custom_path_) {
        custom_path_ = s.wallpaper_path;
        custom_error_.clear();
        custom_.reset();
        if (!custom_path_.empty()) custom_.loadFile(custom_path_, &custom_error_);
    }
    return custom_error_;
}

void WallpaperRenderer::drawPattern(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const std::string& id, const Palette& p) {
    // Tiles start at the rectangle's corner, like CSS background tiling.
    if (id == "dots") {
        const float step = 22;
        for (float y = p0.y; y < p1.y; y += step)
            for (float x = p0.x; x < p1.x; x += step)
                dl->AddCircleFilled(ImVec2(x + 4, y + 4), 1.6f, rgba(p.accent, 0.35f), 8);
    } else if (id == "grid") {
        const float step = 32;
        ImU32 c = rgba(p.muted, 0.18f);
        for (float x = p0.x + 0.5f; x < p1.x; x += step) dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), c);
        for (float y = p0.y + 0.5f; y < p1.y; y += step) dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), c);
    } else if (id == "waves") {
        const float step = 60;
        ImU32 a = rgba(p.accent, 0.22f), b = rgba(p.dm, 0.18f);
        for (float y = p0.y; y < p1.y; y += step)
            for (float x = p0.x; x < p1.x; x += step) {
                // Two half-waves per tile: up then down (the SVG's Q + T).
                for (auto [dy, c] : { std::pair<float, ImU32>{ 20, a }, { 45, b } }) {
                    dl->PathLineTo(ImVec2(x, y + dy));
                    dl->PathBezierQuadraticCurveTo(ImVec2(x + 15, y + dy - 12), ImVec2(x + 30, y + dy), 10);
                    dl->PathBezierQuadraticCurveTo(ImVec2(x + 45, y + dy + 12), ImVec2(x + 60, y + dy), 10);
                    dl->PathStroke(c, 0, 2.0f);
                }
            }
    } else if (id == "bubbles") {
        const float step = 120;
        for (float y = p0.y; y < p1.y; y += step)
            for (float x = p0.x; x < p1.x; x += step) {
                dl->AddCircleFilled(ImVec2(x + 20, y + 22), 10, rgba(p.accent, 0.1f));
                dl->AddCircleFilled(ImVec2(x + 70, y + 60), 16, rgba(p.dm, 0.09f));
                dl->AddCircleFilled(ImVec2(x + 95, y + 15), 6, rgba(p.own_bubble, 0.14f));
                dl->AddCircleFilled(ImVec2(x + 35, y + 95), 7, rgba(p.accent, 0.12f));
            }
    }
}

void WallpaperRenderer::drawGradient(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const std::string& id, const Palette& p) {
    // Rendered once into a small texture and stretched: smooth and cheap.
    char key[64];
    std::snprintf(key, sizeof(key), "%06x %06x %06x %06x", vec_to_rgb(p.panel_alt), vec_to_rgb(p.accent),
                  vec_to_rgb(p.dm), vec_to_rgb(p.own_bubble));
    Gradient& g = gradients_[id];
    if (key != g.key || !g.tex.valid()) {
        const int w = 160, h = 120;
        std::vector<unsigned char> px(w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
                ImVec4 c;
                if (id == "dusk") {
                    // linear-gradient(160deg, #2b1055, #7597de 55%, #f6b38e)
                    float t = std::clamp(0.34f * u + 0.94f * v - 0.14f, 0.0f, 1.0f);
                    c = t < 0.55f ? lerp(rgb_to_vec(0x2b1055), rgb_to_vec(0x7597de), t / 0.55f)
                                  : lerp(rgb_to_vec(0x7597de), rgb_to_vec(0xf6b38e), (t - 0.55f) / 0.45f);
                } else { // aurora: three soft color spots over the panel color
                    c = p.panel_alt;
                    c = lerp(c, p.accent, 0.35f * spot(u, v, 0.15f, 0.10f, 0.6f, 0.6f));
                    c = lerp(c, p.dm, 0.30f * spot(u, v, 0.85f, 0.30f, 0.6f, 0.6f));
                    c = lerp(c, p.own_bubble, 0.30f * spot(u, v, 0.50f, 1.00f, 0.7f, 0.7f));
                }
                unsigned char* o = &px[(y * w + x) * 4];
                o[0] = static_cast<unsigned char>(c.x * 255);
                o[1] = static_cast<unsigned char>(c.y * 255);
                o[2] = static_cast<unsigned char>(c.z * 255);
                o[3] = 255;
            }
        g.tex.loadPixels(px.data(), w, h);
        g.key = key;
    }
    dl->AddImage(g.tex.id(), p0, p1);
}

bool WallpaperRenderer::draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Settings& s, const Palette& p) {
    const std::string& id = s.wallpaper;
    if (id == "none" || id.empty()) return false;

    dl->PushClipRect(p0, p1, true);
    if (id == "custom") {
        syncCustom(s);
        if (!custom_.valid()) {
            dl->PopClipRect();
            return false;
        }
        // "cover": fill the area, crop the overflow, keep the aspect ratio.
        float aw = p1.x - p0.x, ah = p1.y - p0.y;
        float k = std::max(aw / custom_.width(), ah / custom_.height());
        float iw = custom_.width() * k, ih = custom_.height() * k;
        ImVec2 i0(p0.x + (aw - iw) / 2, p0.y + (ah - ih) / 2);
        dl->AddImage(custom_.id(), i0, ImVec2(i0.x + iw, i0.y + ih));
    } else if (id == "aurora" || id == "dusk") {
        drawGradient(dl, p0, p1, id, p);
    } else {
        dl->AddRectFilled(p0, p1, rgba(p.panel_alt, 1));
        drawPattern(dl, p0, p1, id, p);
    }
    dl->AddRectFilled(p0, p1, rgba(p.bg, s.dim)); // dim for readability
    dl->PopClipRect();
    return true;
}

/* ------------------------------------------------------------------ */
/* File dialog                                                         */
/* ------------------------------------------------------------------ */

std::string pick_image_file() {
    static const char* commands[] = {
        "zenity --file-selection --title='Choose a background image' "
        "--file-filter='Images | *.png *.jpg *.jpeg *.bmp *.gif' 2>/dev/null",
        "kdialog --getopenfilename ~ 'image/png image/jpeg image/bmp image/gif' 2>/dev/null",
    };
    for (const char* cmd : commands) {
        FILE* f = popen(cmd, "r");
        if (!f) continue;
        char buf[4096];
        std::string out;
        while (fgets(buf, sizeof(buf), f)) out += buf;
        int rc = pclose(f);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (rc == 0) return out;   // chosen
        if (WIFEXITED(rc) && WEXITSTATUS(rc) == 1) return ""; // cancelled
        // 127 or similar: tool not installed, try the next one
    }
    return "";
}
