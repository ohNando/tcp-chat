// Messaging-app style chat rendering with ImDrawList: bubbles with tails,
// avatars, grouping and day separators. Mirrors Web/src/chat/groupMessages.js.
#pragma once

#include <string>
#include <vector>

#include "chat_client.h"
#include "theme.h"

// Draws the chat lines into the current window (call inside a child) and
// advances the cursor past them.
void draw_chat_view(const std::vector<ChatLine>& lines, const Palette& p, bool compact);

// Round avatar with the name's first letter, in a color derived from the name.
void draw_avatar(ImDrawList* dl, ImVec2 center, float radius, const std::string& name);

// A name's stable color (same hash as the web client).
ImU32 name_color(const std::string& name, float lightness = 0.62f);
