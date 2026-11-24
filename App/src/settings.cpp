#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core.h"

std::string settings_path() {
    char path[CONFIG_PATH_MAX];
    if (config_path("desktop.ini", "TCPCHAT_DESKTOP_SETTINGS", path, sizeof(path)) < 0) return "";
    return path;
}

static int32_t parse_color(const std::string& v) {
    if (v.size() != 7 || v[0] != '#') return -1;
    char* end;
    long n = std::strtol(v.c_str() + 1, &end, 16);
    return *end ? -1 : static_cast<int32_t>(n);
}

static std::string color_str(int32_t c) {
    if (c < 0) return "";
    char buf[12];
    std::snprintf(buf, sizeof(buf), "#%06x", static_cast<unsigned>(c) & 0xFFFFFFu);
    return buf;
}

void settings_load(Settings& s, const std::string& path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);

        if (k == "theme") s.theme = v;
        else if (k == "accent") s.accent = parse_color(v);
        else if (k == "own_bubble") s.own_bubble = parse_color(v);
        else if (k == "other_bubble") s.other_bubble = parse_color(v);
        else if (k == "wallpaper") s.wallpaper = v;
        else if (k == "wallpaper_path") s.wallpaper_path = v;
        else if (k == "dim") s.dim = std::strtof(v.c_str(), nullptr);
        else if (k == "blur") s.blur = std::strtof(v.c_str(), nullptr);
        else if (k == "notifications") s.notifications = v == "1";
        else if (k == "font_size") s.font_size = v;
        else if (k == "compact") s.compact = v == "1";
        else if (k == "host") s.host = v;
        else if (k == "port") s.port = std::atoi(v.c_str());
        else if (k == "name") s.name = v;
    }
    if (s.dim < 0 || s.dim > 0.8f) s.dim = 0.25f;
    if (s.blur < 0 || s.blur > 20) s.blur = 0;
    if (s.port <= 0 || s.port > 65535) s.port = 8080;
}

bool settings_save(const Settings& s, const std::string& path) {
    if (path.empty() || config_make_parent_dirs(path.c_str()) < 0) return false;
    std::ostringstream out;
    out << "# TCP Chat desktop settings\n"
        << "theme=" << s.theme << "\n"
        << "accent=" << color_str(s.accent) << "\n"
        << "own_bubble=" << color_str(s.own_bubble) << "\n"
        << "other_bubble=" << color_str(s.other_bubble) << "\n"
        << "wallpaper=" << s.wallpaper << "\n"
        << "wallpaper_path=" << s.wallpaper_path << "\n"
        << "dim=" << s.dim << "\n"
        << "blur=" << s.blur << "\n"
        << "font_size=" << s.font_size << "\n"
        << "compact=" << (s.compact ? 1 : 0) << "\n"
        << "notifications=" << (s.notifications ? 1 : 0) << "\n"
        << "host=" << s.host << "\n"
        << "port=" << s.port << "\n"
        << "name=" << s.name << "\n";

    // Write to a temporary file and rename: a crash never leaves half a file.
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!(f << out.str())) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}
