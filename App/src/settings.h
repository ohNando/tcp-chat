// Desktop preferences, saved as "key=value" lines in
// ~/.config/tcp-chat/desktop.ini (or $TCPCHAT_DESKTOP_SETTINGS).
#pragma once

#include <cstdint>
#include <string>

struct Settings {
    // Look
    std::string theme = "midnight";
    int32_t accent = -1;        // 0xRRGGBB, -1 = the theme's own color
    int32_t own_bubble = -1;
    int32_t other_bubble = -1;
    std::string wallpaper = "none";   // none | dots | grid | waves | bubbles | aurora | dusk | custom
    std::string wallpaper_path;       // image file for "custom"
    float dim = 0.25f;                // 0..0.8 darkening over the wallpaper
    std::string font_size = "medium"; // small | medium | large
    bool compact = false;

    // Last connection, to prefill the connect screen
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string name;
};

std::string settings_path();
// Missing or unreadable file: keeps the defaults.
void settings_load(Settings& s, const std::string& path);
bool settings_save(const Settings& s, const std::string& path);
