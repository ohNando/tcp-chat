// Chat backgrounds for the desktop app: the same built-in patterns as
// Web/src/theme/wallpapers.js, or an image file chosen by the user.
#pragma once

#include <map>
#include <string>

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

    // Decodes an image file (JPEG, PNG, BMP, GIF). On failure keeps the
    // old texture and sets *error.
    bool loadFile(const std::string& path, std::string* error);
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
    // Fills the rectangle with the selected background and the dim layer.
    // Returns false (draws nothing) for "none".
    bool draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Settings& s, const Palette& p);

    // Loads s.wallpaper_path if it changed since the last call.
    // Returns the error text of a failed load, or "".
    const std::string& syncCustom(const Settings& s);

private:
    void drawPattern(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const std::string& id, const Palette& p);
    void drawGradient(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const std::string& id, const Palette& p);

    Texture custom_;
    std::string custom_path_;     // path custom_ was loaded from
    std::string custom_error_;

    struct Gradient {
        Texture tex;
        std::string key;          // colors the texture was made with
    };
    std::map<std::string, Gradient> gradients_;   // by wallpaper id
};

// Asks for an image file with the desktop's file dialog (zenity or
// kdialog). Returns "" when cancelled or no dialog tool is installed.
std::string pick_image_file();
