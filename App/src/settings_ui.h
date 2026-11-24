// The settings panel, laid out like Web/src/components/SettingsPanel.jsx:
// a 380 px panel sliding over the right edge with a dimmed backdrop.
#pragma once

#include "settings.h"
#include "theme.h"
#include "wallpaper.h"

// Panel state shared with the app.
struct SettingsUi {
    bool open = false;
    bool pick_image = false;   // set by the panel: user asked for a file dialog
    bool picking = false;      // set by the app while the dialog is open
    bool no_dialog = false;    // no zenity/kdialog: offer a path field instead
    char path[1024] = "";
};

// Draws the panel while ui.open is true. Returns true when a setting
// changed (the caller re-applies the look and saves). `server_fp` is shown
// in the Connection section when known.
bool draw_settings_panel(SettingsUi& ui, Settings& s, const Palette& p, WallpaperRenderer& wr,
                         const std::string& server_fp);
