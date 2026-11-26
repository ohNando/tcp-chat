// The message list, drawn the way the web client's MessageList renders it
// (Web/src/App.jsx, App.css): bubbles with tails, avatars, grouping and day
// separators, with the same CSS sizes.
#pragma once

#include <string>
#include <vector>

#include "chat_client.h"
#include "theme.h"

// What the top of the list shows (web: .history-top).
enum class HistoryTop { None, Load, Loading, Beginning };

// Draws the lines into the current (scrolling) window, starting at the
// cursor and using the available width. Returns true when "Load earlier
// messages" was clicked.
bool draw_chat_view(const std::vector<ChatLine>& lines, const Palette& p, bool compact, bool narrow,
                    HistoryTop top);

// "05:15 PM" or "17:15", following the system locale like the browser's
// toLocaleTimeString(); "Today", "Yesterday" or a date for separators.
std::string clock_text(uint64_t ms);
std::string day_label(uint64_t ms);
