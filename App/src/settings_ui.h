// The desktop settings window: theme, colors, background, text and layout.
#pragma once

#include "settings.h"
#include "theme.h"

// Draws the window while *open is true. Returns true when a setting
// changed (the caller re-applies the look and saves).
bool draw_settings_window(bool* open, Settings& s, const Palette& p);
