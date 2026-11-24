#include "emoji_font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "imgui_internal.h"
#include "stb_image.h"

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// Main font's ImGui size per CSS em; one value for the whole app.
float g_em_ratio = 1.0f;

struct Glyph {
    uint32_t offset, length;  // image data (format 17) inside the font file
};

struct EmojiFont {
    const uint8_t* data = nullptr;
    size_t size = 0;
    float ppem = 0;           // size the bitmaps were drawn for
    float em_ratio = 1;
    struct Group { uint32_t first, last, glyph; };
    std::vector<Group> cmap;  // format 12 groups
    std::unordered_map<uint32_t, Glyph> glyphs;

    uint32_t glyphFor(uint32_t cp) const {
        size_t lo = 0, hi = cmap.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (cp < cmap[mid].first) hi = mid;
            else if (cp > cmap[mid].last) lo = mid + 1;
            else return cmap[mid].glyph + (cp - cmap[mid].first);
        }
        return 0;
    }
};

// Joiners and variation selectors have no picture: draw them as nothing
// (an unknown glyph would show "?").
bool invisible(ImWchar cp) {
    return cp == 0x200D || cp == 0xFE0E || cp == 0xFE0F;
}

bool parse(EmojiFont& f) {
    const uint8_t* d = f.data;
    if (f.size < 12) return false;
    uint32_t cmap = 0, cblc = 0, cbdt = 0;
    uint16_t n = u16(d + 4);
    for (uint16_t i = 0; i < n && 12 + 16 * (static_cast<size_t>(i) + 1) <= f.size; i++) {
        const uint8_t* rec = d + 12 + 16 * i;
        uint32_t off = u32(rec + 8);
        if (!memcmp(rec, "cmap", 4)) cmap = off;
        else if (!memcmp(rec, "CBLC", 4)) cblc = off;
        else if (!memcmp(rec, "CBDT", 4)) cbdt = off;
    }
    if (!cmap || !cblc || !cbdt) return false;

    // cmap: the (3, 10) subtable in format 12 covers code points above U+FFFF.
    for (uint16_t i = 0, nt = u16(d + cmap + 2); i < nt; i++) {
        const uint8_t* rec = d + cmap + 4 + 8 * i;
        if (u16(rec) != 3 || u16(rec + 2) != 10) continue;
        const uint8_t* sub = d + cmap + u32(rec + 4);
        if (u16(sub) != 12) continue;
        uint32_t groups = u32(sub + 12);
        for (uint32_t g = 0; g < groups; g++) {
            const uint8_t* gr = sub + 16 + 12 * g;
            f.cmap.push_back({ u32(gr), u32(gr + 4), u32(gr + 8) });
        }
    }

    // CBLC: one strike; index subtables of format 1 point at format 17 images.
    if (u32(d + cblc + 4) < 1) return false;
    const uint8_t* size_rec = d + cblc + 8;
    uint32_t array = cblc + u32(size_rec);
    uint32_t subtables = u32(size_rec + 8);
    f.ppem = size_rec[44];
    for (uint32_t i = 0; i < subtables; i++) {
        const uint8_t* e = d + array + 8 * i;
        uint16_t first = u16(e), last = u16(e + 2);
        const uint8_t* sub = d + array + u32(e + 4);
        if (u16(sub) != 1 || u16(sub + 2) != 17) continue;
        uint32_t image_base = cbdt + u32(sub + 4);
        for (uint32_t g = first; g <= last; g++) {
            uint32_t a = u32(sub + 8 + 4 * (g - first)), b = u32(sub + 8 + 4 * (g - first + 1));
            if (b > a) f.glyphs[g] = { image_base + a, b - a };
        }
    }
    return !f.cmap.empty() && !f.glyphs.empty() && f.ppem > 0;
}

/* ------------------------------------------------------------------ */
/* ImFontLoader callbacks                                              */
/* ------------------------------------------------------------------ */

bool src_init(ImFontAtlas*, ImFontConfig* src) {
    auto* f = new EmojiFont();
    f->data = static_cast<const uint8_t*>(src->FontData);
    f->size = static_cast<size_t>(src->FontDataSize);
    f->em_ratio = g_em_ratio;
    if (!parse(*f)) {
        delete f;
        return false;
    }
    src->FontLoaderData = f;
    return true;
}

void src_destroy(ImFontAtlas*, ImFontConfig* src) {
    delete static_cast<EmojiFont*>(src->FontLoaderData);
    src->FontLoaderData = nullptr;
}

bool contains(ImFontAtlas*, ImFontConfig* src, ImWchar cp) {
    auto* f = static_cast<EmojiFont*>(src->FontLoaderData);
    return invisible(cp) || (f->glyphFor(cp) && f->glyphs.count(f->glyphFor(cp)));
}

bool baked_init(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) { return true; }

bool load_glyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, ImWchar cp, ImFontGlyph* out,
                float* out_advance_x) {
    auto* f = static_cast<EmojiFont*>(src->FontLoaderData);
    if (invisible(cp)) {
        if (out_advance_x) *out_advance_x = 0;
        else {
            out->Codepoint = cp;
            out->AdvanceX = 0;
            out->Visible = false;
        }
        return true;
    }
    auto it = f->glyphs.find(f->glyphFor(cp));
    if (it == f->glyphs.end()) return false;

    // smallGlyphMetrics: height, width, bearingX, bearingY, advance (pixels
    // at ppem), then the PNG length and the PNG itself.
    const uint8_t* g = f->data + it->second.offset;
    const int bm_h = g[0], bm_w = g[1];
    const float bearing_x = static_cast<int8_t>(g[2]), bearing_y = static_cast<int8_t>(g[3]);
    const float em = baked->Size / f->em_ratio;  // CSS em in pixels
    const float k = em / f->ppem;
    const float advance = g[4] * k;
    if (out_advance_x) {
        *out_advance_x = advance;
        return true;
    }

    out->Codepoint = cp;
    out->AdvanceX = advance;
    const float density = src->RasterizerDensity * baked->RasterizerDensity;
    int w = std::max(1, static_cast<int>(std::lround(bm_w * k * density)));
    int h = std::max(1, static_cast<int>(std::lround(bm_h * k * density)));

    uint32_t png_len = u32(g + 5);
    int pw, ph, comp;
    unsigned char* png = stbi_load_from_memory(g + 9, static_cast<int>(png_len), &pw, &ph, &comp, 4);
    if (!png) return false;

    // Area-average down to the target size, in premultiplied alpha so the
    // transparent edges do not turn dark.
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; y++) {
        float sy0 = static_cast<float>(y) * ph / h, sy1 = static_cast<float>(y + 1) * ph / h;
        for (int x = 0; x < w; x++) {
            float sx0 = static_cast<float>(x) * pw / w, sx1 = static_cast<float>(x + 1) * pw / w;
            float acc[4] = { 0, 0, 0, 0 }, area = 0;
            for (int sy = static_cast<int>(sy0); sy < std::min(ph, static_cast<int>(std::ceil(sy1))); sy++) {
                float wy = std::min(sy1, sy + 1.0f) - std::max(sy0, static_cast<float>(sy));
                for (int sx = static_cast<int>(sx0); sx < std::min(pw, static_cast<int>(std::ceil(sx1))); sx++) {
                    float wx = std::min(sx1, sx + 1.0f) - std::max(sx0, static_cast<float>(sx));
                    const unsigned char* s = png + (static_cast<size_t>(sy) * pw + sx) * 4;
                    float a = s[3] / 255.0f * wx * wy;
                    acc[0] += s[0] * a;
                    acc[1] += s[1] * a;
                    acc[2] += s[2] * a;
                    acc[3] += a;
                    area += wx * wy;
                }
            }
            unsigned char* o = &px[(static_cast<size_t>(y) * w + x) * 4];
            float alpha = area > 0 ? acc[3] / area : 0;
            for (int c = 0; c < 3; c++)
                o[c] = acc[3] > 0 ? static_cast<unsigned char>(std::min(255.0f, acc[c] / acc[3])) : 0;
            o[3] = static_cast<unsigned char>(std::lround(alpha * 255));
        }
    }
    stbi_image_free(png);

    ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
    if (id == ImFontAtlasRectId_Invalid) return false;
    ImTextureRect* r = ImFontAtlasPackGetRect(atlas, id);
    const float x0 = bearing_x * k, y0 = IM_ROUND(baked->Ascent) - bearing_y * k;
    out->X0 = x0;
    out->Y0 = y0;
    out->X1 = x0 + w / density;
    out->Y1 = y0 + h / density;
    out->Visible = true;
    out->Colored = true;
    out->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, r, px.data(), ImTextureFormat_RGBA32, w * 4);
    return true;
}

const ImFontLoader* loader() {
    static ImFontLoader l;
    l.Name = "cbdt-color-emoji";
    l.FontSrcInit = src_init;
    l.FontSrcDestroy = src_destroy;
    l.FontSrcContainsGlyph = contains;
    l.FontBakedInit = baked_init;
    l.FontBakedLoadGlyph = load_glyph;
    return &l;
}

} // namespace

bool merge_color_emoji(const char* path, float size, float em_ratio) {
    g_em_ratio = em_ratio;
    // The file stays in memory for the whole run; all merged copies share it.
    static std::vector<uint8_t> file;
    if (file.empty()) {
        FILE* fp = std::fopen(path, "rb");
        if (!fp) return false;
        std::fseek(fp, 0, SEEK_END);
        long n = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        file.resize(n > 0 ? static_cast<size_t>(n) : 0);
        bool ok = n > 0 && std::fread(file.data(), 1, file.size(), fp) == file.size();
        std::fclose(fp);
        if (!ok) {
            file.clear();
            return false;
        }
        EmojiFont probe;
        probe.data = file.data();
        probe.size = file.size();
        if (!parse(probe)) {
            file.clear();
            return false;
        }
    }
    ImFontConfig cfg;
    cfg.FontData = file.data();
    cfg.FontDataSize = static_cast<int>(file.size());
    cfg.FontDataOwnedByAtlas = false;
    cfg.MergeMode = true;
    cfg.SizePixels = size;
    cfg.FontLoader = loader();
    std::snprintf(cfg.Name, sizeof(cfg.Name), "Noto Color Emoji");
    return ImGui::GetIO().Fonts->AddFont(&cfg) != nullptr;
}
