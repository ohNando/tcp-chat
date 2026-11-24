// Color emoji for Dear ImGui without FreeType: a font loader for bitmap
// color fonts (CBDT/CBLC tables with PNG glyphs, like Noto Color Emoji),
// decoded with stb_image. The web client shows the same Noto artwork.
#pragma once

#include "imgui.h"

// Merges the color emoji font at `path` into the font added last. `em_ratio`
// is the main font's ImGui size per CSS em (ascent + descent over units per
// em), so emoji come out as large as the browser draws them. Returns false
// when the file is missing or not a CBDT font.
bool merge_color_emoji(const char* path, float size, float em_ratio);
