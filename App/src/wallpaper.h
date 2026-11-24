// Chat backgrounds, rendered to match Web/src/theme/wallpapers.js: the SVG
// patterns are rasterized into tiles, the CSS gradients are computed with the
// same formulas, and blur/dim work like the web's filter and dim layer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "settings.h"
#include "theme.h"

struct WallpaperInfo {
    const char* id;
    const char* name;
};

// Built-in wallpapers in display order (without "none" and "custom").
const WallpaperInfo* wallpaper_list(int* count);

// An OpenGL texture made from an image file or generated pixels.
class Texture {
public:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture() { reset(); }

    void loadPixels(const unsigned char* rgba, int w, int h);
    void reset();

    bool valid() const { return id_ != 0; }
    ImTextureID id() const { return static_cast<ImTextureID>(id_); }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    unsigned id_ = 0;
    int w_ = 0, h_ = 0;
};

class WallpaperRenderer {
public:
    // Fills [p0, p1] with the background in s.wallpaper; with `effects`
    // also applies s.blur and the s.dim layer (off for settings swatches).
    // Returns false (draws nothing) for "none" or a missing image.
    bool draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Settings& s, const Palette& p, bool effects = true,
              float rounding = 0);

    // Loads s.wallpaper_path if it changed. Returns the error text of a
    // failed load, or "".
    const std::string& syncCustom(const Settings& s);
    bool hasCustom() const { return !image_.empty(); }

private:
    Texture& cached(const std::string& key);

    std::string custom_path_;
    std::string custom_error_;
    std::vector<unsigned char> image_; // decoded custom image, RGBA
    int image_w_ = 0, image_h_ = 0;

    std::map<std::string, Texture> cache_;   // tiles, gradients, blurred images
};

// Asks for an image file with the desktop's file dialog (zenity or
// kdialog). Returns "" when cancelled; sets *no_dialog when neither exists.
std::string pick_image_file(bool* no_dialog);
