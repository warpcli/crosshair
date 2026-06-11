#pragma once

static std::string color_hex(double r, double g, double b) {
    char color[16] = {0};
    std::snprintf(color,
                  sizeof(color),
                  "#%02x%02x%02x",
                  static_cast<int>(std::round(std::clamp(r, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(g, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(b, 0.0, 1.0) * 255.0)));
    return color;
}

static std::string current_color_hex() {
    return color_hex(g_color_r, g_color_g, g_color_b);
}

static std::string current_bg_hex() {
    return color_hex(g_bg_r, g_bg_g, g_bg_b);
}

static std::string current_shadow_hex() {
    return color_hex(g_shadow_r, g_shadow_g, g_shadow_b);
}

static bool parse_hex_color(const std::string& text, double& r, double& g, double& b) {
    const auto pos = text.find('#');
    if (pos == std::string::npos || pos + 7 > text.size())
        return false;

    unsigned int rv = 0;
    unsigned int gv = 0;
    unsigned int bv = 0;
    if (std::sscanf(text.c_str() + pos, "#%02x%02x%02x", &rv, &gv, &bv) != 3)
        return false;

    r = rv / 255.0;
    g = gv / 255.0;
    b = bv / 255.0;
    return true;
}

static bool read_color_from_file(const std::string& path, bool json, double& r, double& g, double& b) {
    std::ifstream file(path);
    if (!file.good())
        return false;

    std::string line;
    for (int i = 0; std::getline(file, line); ++i) {
        if (json && !line.contains("\"color1\""))
            continue;
        if (!json && i != 1)
            continue;
        if (parse_hex_color(line, r, g, b))
            return true;
    }

    return false;
}

static bool read_json_color_key(const std::string& path, const std::string& key, double& r, double& g, double& b) {
    std::ifstream file(path);
    if (!file.good())
        return false;

    const std::string needle = "\"" + key + "\"";
    std::string line;
    while (std::getline(file, line)) {
        if (!line.contains(needle))
            continue;
        if (parse_hex_color(line, r, g, b))
            return true;
    }

    return false;
}

static std::string trim_copy(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";

    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

static std::string command_action(std::string request) {
    request = trim_copy(std::move(request));
    if (request.starts_with("crosshair"))
        request = trim_copy(request.substr(std::strlen("crosshair")));
    if (request.starts_with("-j"))
        request = trim_copy(request.substr(2));

    const auto space = request.find_first_of(" \t\r\n");
    return space == std::string::npos ? request : request.substr(0, space);
}

static bool read_colors_line(const std::string& path, int target_line, double& r, double& g, double& b) {
    std::ifstream file(path);
    if (!file.good())
        return false;

    std::string line;
    for (int i = 0; std::getline(file, line); ++i) {
        if (i == target_line && parse_hex_color(line, r, g, b))
            return true;
    }

    return false;
}

static bool load_accent_color() {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double bg_r = 0.0;
    double bg_g = 0.0;
    double bg_b = 0.0;
    double shadow_r = 0.0;
    double shadow_g = 0.0;
    double shadow_b = 0.0;

    const char* home = std::getenv("HOME");
    const std::string base = home ? std::string(home) + "/.cache/wal/" : "";
    if (home) {
        if (!read_json_color_key(base + "colors.json", "background", bg_r, bg_g, bg_b)
            && !read_json_color_key(base + "colors.json", "color0", bg_r, bg_g, bg_b)
            && !read_colors_line(base + "colors", 0, bg_r, bg_g, bg_b)) {
            bg_r = 0.0;
            bg_g = 0.0;
            bg_b = 0.0;
        }

        if (!read_json_color_key(base + "colors.json", "color0", shadow_r, shadow_g, shadow_b)
            && !read_colors_line(base + "colors", 0, shadow_r, shadow_g, shadow_b)
            && !read_json_color_key(base + "colors.json", "background", shadow_r, shadow_g, shadow_b)) {
            shadow_r = bg_r;
            shadow_g = bg_g;
            shadow_b = bg_b;
        }
    }

    if (const char* env_color = std::getenv("CROSSHAIR_COLOR"); !env_color || !parse_hex_color(env_color, r, g, b)) {
        if (home)
            (void)(read_color_from_file(base + "colors.json", true, r, g, b) || read_color_from_file(base + "colors", false, r, g, b));
    }

    const bool changed = r != g_color_r || g != g_color_g || b != g_color_b || bg_r != g_bg_r || bg_g != g_bg_g || bg_b != g_bg_b
        || shadow_r != g_shadow_r || shadow_g != g_shadow_g || shadow_b != g_shadow_b;
    g_color_r  = r;
    g_color_g  = g;
    g_color_b  = b;
    g_bg_r     = bg_r;
    g_bg_g     = bg_g;
    g_bg_b     = bg_b;
    g_shadow_r = shadow_r;
    g_shadow_g = shadow_g;
    g_shadow_b = shadow_b;
    return changed;
}
