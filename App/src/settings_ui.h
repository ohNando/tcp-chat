// The desktop settings window: theme, colors, background, text and layout.
#pragma once

#include "settings.h"
#include "theme.h"
#include "wallpaper.h"

// Window state shared with the app.
struct SettingsUi {
    bool open = false;
    bool pick_image = false;   // set by the window: user asked for a file dialog
    bool picking = false;      // set by the app while the dialog is open
    char path[1024] = "";      // typed image path (when no dialog tool exists)
};

// Draws the window while ui.open is true. Returns true when a setting
// changed (the caller re-applies the look and saves).
bool draw_settings_window(SettingsUi& ui, Settings& s, const Palette& p, WallpaperRenderer& wr);
