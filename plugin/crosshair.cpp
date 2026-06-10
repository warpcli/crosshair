#include <src/Compositor.hpp>
#include <src/event/EventBus.hpp>
#include <src/managers/CursorManager.hpp>
#include <src/managers/PointerManager.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/eventLoop/EventLoopTimer.hpp>
#include <src/plugins/PluginAPI.hpp>
#include <src/protocols/CursorShape.hpp>
#include <src/render/Renderer.hpp>
#include <src/render/pass/TexPassElement.hpp>

#include <cairo/cairo.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>

static constexpr double CROSSHAIR_PI       = 3.14159265358979323846;
static constexpr double UPDATE_MS          = 16.0;
static constexpr double LINE_WIDTH         = 3.0;
static constexpr double CENTER_GAP         = 52.0;
static constexpr double CENTER_DAMAGE      = 86.0;
static constexpr double DOT_RADIUS         = 3.0;
static constexpr double HUD_SCALE          = 1.56;
static constexpr double RING_RADIUS        = 17.0;
static constexpr double TICK_INNER_RADIUS  = 23.0;
static constexpr double TICK_OUTER_RADIUS  = 30.0;
static constexpr double BEAM_HALF_LENGTH   = 12.0;
static constexpr double BEAM_CAP_LENGTH    = 4.0;
static constexpr double LABEL_FONT_SIZE    = 11.0;
static constexpr double LABEL_OFFSET       = 24.0;
static constexpr double DRAG_LABEL_OFFSET  = 70.0;

enum CursorIntent {
    INTENT_DEFAULT,
    INTENT_POINTER,
    INTENT_TEXT,
    INTENT_VERTICAL_TEXT,
    INTENT_MOVE,
    INTENT_GRAB,
    INTENT_RESIZE,
    INTENT_NOT_ALLOWED,
    INTENT_WAIT,
};

static HANDLE              g_handle                   = nullptr;
static CHyprSignalListener g_shape_listener;
static CHyprSignalListener g_cursor_changed_listener;
static CHyprSignalListener g_mouse_button_listener;
static CFunctionHook*      g_renderer_name_hook       = nullptr;
static CFunctionHook*      g_cursor_manager_name_hook = nullptr;
static CFunctionHook*      g_software_cursor_hook     = nullptr;
static SP<SHyprCtlCommand> g_status_command;
static SP<CEventLoopTimer> g_timer;
static std::string         g_last_shape                = "default";
static CursorIntent        g_cursor_intent             = INTENT_DEFAULT;
static Vector2D            g_last_position             = {-1, -1};
static bool                g_have_last_position        = false;
static Vector2D            g_drag_start                = {0, 0};
static int                 g_pressed_buttons           = 0;
static bool                g_drag_active               = false;
static bool                g_locked_software_cursor    = false;
static double              g_color_r                   = 1.0;
static double              g_color_g                   = 1.0;
static double              g_color_b                   = 1.0;
static double              g_bg_r                      = 0.0;
static double              g_bg_g                      = 0.0;
static double              g_bg_b                      = 0.0;
static double              g_render_scale              = 1.0;
static uint64_t            g_color_reload_ticks        = 0;
static uint64_t            g_render_calls              = 0;
static uint64_t            g_draw_calls                = 0;

struct SInertia {
    Vector2D vector    = {0, 0};
    Vector2D direction = {0, 0};
    double   magnitude = 0.0;
    bool     active    = false;
};

using RendererSetCursorFromNameFn = void (*)(Render::IHyprRenderer*, const std::string&, bool);
using CursorManagerSetCursorFromNameFn = void (*)(CCursorManager*, const std::string&);
using RenderSoftwareCursorsForFn = void (*)(CPointerManager*, PHLMONITOR, const Time::steady_tp&, CRegion&, std::optional<Vector2D>, bool);

static std::string normalize_shape_name(std::string shape) {
    std::replace(shape.begin(), shape.end(), '-', '_');
    return shape.empty() ? "default" : shape;
}

static CursorIntent intent_from_shape_name(std::string shape) {
    shape = normalize_shape_name(std::move(shape));

    if (shape == "pointer" || shape == "hand" || shape == "hand1" || shape == "hand2" || shape == "pointing_hand")
        return INTENT_POINTER;
    if (shape == "text" || shape == "xterm" || shape == "ibeam")
        return INTENT_TEXT;
    if (shape == "vertical_text" || shape == "vertical_xterm")
        return INTENT_VERTICAL_TEXT;
    if (shape == "move" || shape == "copy" || shape == "alias" || shape == "fleur" || shape == "all_scroll")
        return INTENT_MOVE;
    if (shape == "grab" || shape == "grabbing")
        return INTENT_GRAB;
    if (shape.contains("resize") || shape == "size_hor" || shape == "size_ver" || shape == "size_bdiag" || shape == "size_fdiag")
        return INTENT_RESIZE;
    if (shape == "not_allowed" || shape == "no_drop" || shape == "crossed_circle")
        return INTENT_NOT_ALLOWED;
    if (shape == "wait" || shape == "progress" || shape == "watch" || shape == "left_ptr_watch")
        return INTENT_WAIT;

    return INTENT_DEFAULT;
}

static const char* intent_name(CursorIntent intent) {
    switch (intent) {
        case INTENT_POINTER: return "pointer";
        case INTENT_TEXT: return "text";
        case INTENT_VERTICAL_TEXT: return "vertical_text";
        case INTENT_MOVE: return "move";
        case INTENT_GRAB: return "grab";
        case INTENT_RESIZE: return "resize";
        case INTENT_NOT_ALLOWED: return "not_allowed";
        case INTENT_WAIT: return "wait";
        default: return "default";
    }
}

static const char* shape_name_from_enum(wpCursorShapeDeviceV1Shape shape) {
    switch (shape) {
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER: return "pointer";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT: return "text";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT: return "vertical_text";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE: return "move";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY: return "copy";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS: return "alias";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB: return "grab";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING: return "grabbing";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED: return "not_allowed";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP: return "no_drop";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT: return "wait";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS: return "progress";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE: return "e_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE: return "n_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE: return "ne_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE: return "nw_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE: return "s_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE: return "se_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE: return "sw_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE: return "w_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE: return "ew_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE: return "ns_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE: return "nesw_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE: return "nwse_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE: return "col_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE: return "row_resize";
        case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE: return "all_resize";
        default: return "default";
    }
}

static void set_shape(std::string shape) {
    g_last_shape    = normalize_shape_name(std::move(shape));
    g_cursor_intent = intent_from_shape_name(g_last_shape);
}

static void set_renderer_shape() {
    if (!g_pHyprRenderer) {
        set_shape("default");
        return;
    }

    const auto& cursor = g_pHyprRenderer->m_lastCursorData;
    set_shape(cursor.name.empty() ? shape_name_from_enum(cursor.shape) : cursor.name);
}

static std::string current_color_hex() {
    char color[16] = {0};
    std::snprintf(color,
                  sizeof(color),
                  "#%02x%02x%02x",
                  static_cast<int>(std::round(std::clamp(g_color_r, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(g_color_g, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(g_color_b, 0.0, 1.0) * 255.0)));
    return color;
}

static std::string current_bg_hex() {
    char color[16] = {0};
    std::snprintf(color,
                  sizeof(color),
                  "#%02x%02x%02x",
                  static_cast<int>(std::round(std::clamp(g_bg_r, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(g_bg_g, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(g_bg_b, 0.0, 1.0) * 255.0)));
    return color;
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

    if (const char* env_color = std::getenv("CROSSHAIR_COLOR"); env_color && parse_hex_color(env_color, r, g, b)) {
        const bool changed = r != g_color_r || g != g_color_g || b != g_color_b;
        g_color_r = r;
        g_color_g = g;
        g_color_b = b;
        return changed;
    }

    const char* home = std::getenv("HOME");
    if (!home)
        return false;

    const std::string base = std::string(home) + "/.cache/wal/";
    if (read_color_from_file(base + "colors.json", true, r, g, b) || read_color_from_file(base + "colors", false, r, g, b)) {
        if (!read_json_color_key(base + "colors.json", "background", bg_r, bg_g, bg_b)
            && !read_json_color_key(base + "colors.json", "color0", bg_r, bg_g, bg_b)
            && !read_colors_line(base + "colors", 0, bg_r, bg_g, bg_b)) {
            bg_r = 0.0;
            bg_g = 0.0;
            bg_b = 0.0;
        }

        const bool changed = r != g_color_r || g != g_color_g || b != g_color_b || bg_r != g_bg_r || bg_g != g_bg_g || bg_b != g_bg_b;
        g_color_r = r;
        g_color_g = g;
        g_color_b = b;
        g_bg_r = bg_r;
        g_bg_g = bg_g;
        g_bg_b = bg_b;
        return changed;
    }

    return false;
}

static double px(double value) {
    return value * g_render_scale;
}

static double vector_length(const Vector2D& vector) {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

static SInertia inertia_from_delta(const Vector2D& delta, double deadzone = 1.0) {
    SInertia inertia;
    inertia.vector    = delta;
    inertia.magnitude = vector_length(delta);
    inertia.active    = inertia.magnitude >= deadzone;
    if (inertia.active)
        inertia.direction = {delta.x / inertia.magnitude, delta.y / inertia.magnitude};
    return inertia;
}

static SInertia pointer_inertia_for(const Vector2D& current) {
    if (!g_have_last_position)
        return {};
    return inertia_from_delta(current - g_last_position, 0.5);
}

static SInertia drag_inertia_for(const Vector2D& current) {
    SInertia inertia = inertia_from_delta(current - g_drag_start, 1.0);
    return inertia.active ? inertia : pointer_inertia_for(current);
}

static Vector2D opposite_badge_offset(const SInertia& inertia, double fallback_y = 24.0) {
    if (!inertia.active)
        return {px(DRAG_LABEL_OFFSET), px(fallback_y)};

    return {-inertia.direction.x * px(DRAG_LABEL_OFFSET), -inertia.direction.y * px(DRAG_LABEL_OFFSET)};
}

static void cairo_set_accent(cairo_t* cr, double alpha) {
    cairo_set_source_rgba(cr, g_color_r, g_color_g, g_color_b, alpha);
}

static void queue_cairo_texture(cairo_surface_t* surface, double x, double y, double w, double h, const CRegion& damage) {
    if (!surface || !g_pHyprRenderer)
        return;

    cairo_surface_flush(surface);
    auto texture = g_pHyprRenderer->createTexture(surface);
    if (!texture)
        return;

    CTexPassElement::SRenderData data;
    data.tex     = texture;
    data.box     = CBox{x, y, w, h};
    data.damage  = damage;
    data.overallA = 1.F;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
}

static cairo_surface_t* make_cairo_surface(int w, int h, cairo_t** cr_out) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t*         cr      = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    *cr_out = cr;
    return surface;
}

static void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -CROSSHAIR_PI / 2.0, 0);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, CROSSHAIR_PI / 2.0);
    cairo_arc(cr, x + radius, y + h - radius, radius, CROSSHAIR_PI / 2.0, CROSSHAIR_PI);
    cairo_arc(cr, x + radius, y + radius, radius, CROSSHAIR_PI, CROSSHAIR_PI * 1.5);
    cairo_close_path(cr);
}

static void cairo_draw_center_cross(cairo_t* cr, double cx, double cy, double size) {
    cairo_move_to(cr, cx - px(size), cy);
    cairo_line_to(cr, cx + px(size), cy);
    cairo_move_to(cr, cx, cy - px(size));
    cairo_line_to(cr, cx, cy + px(size));
    cairo_stroke(cr);
}

static void cairo_draw_arrowhead(cairo_t* cr, double tip_x, double tip_y, double dx, double dy) {
    const double wing = px(5.0);
    const double pxn  = -dy;
    const double pyn  = dx;
    const double back_x = tip_x - dx * wing;
    const double back_y = tip_y - dy * wing;

    cairo_move_to(cr, tip_x, tip_y);
    cairo_line_to(cr, back_x + pxn * wing * 0.72, back_y + pyn * wing * 0.72);
    cairo_move_to(cr, tip_x, tip_y);
    cairo_line_to(cr, back_x - pxn * wing * 0.72, back_y - pyn * wing * 0.72);
    cairo_stroke(cr);
}

static void cairo_draw_resize_arrow(cairo_t* cr, double cx, double cy, double dx, double dy, bool paired) {
    const double tip_len  = px(14.0);
    const double tail_len = px(paired ? 14.0 : 3.0);
    const double start_x  = cx - dx * tail_len;
    const double start_y  = cy - dy * tail_len;
    const double tip_x    = cx + dx * tip_len;
    const double tip_y    = cy + dy * tip_len;

    cairo_move_to(cr, start_x, start_y);
    cairo_line_to(cr, tip_x, tip_y);
    cairo_stroke(cr);
    cairo_draw_arrowhead(cr, tip_x, tip_y, dx, dy);
}

static void cairo_draw_resize_glyph(cairo_t* cr, double cx, double cy) {
    const std::string& shape = g_last_shape;
    bool n = false;
    bool e = false;
    bool s = false;
    bool w = false;

    if (shape == "e_resize") {
        e = true;
    } else if (shape == "w_resize") {
        w = true;
    } else if (shape == "n_resize") {
        n = true;
    } else if (shape == "s_resize") {
        s = true;
    } else if (shape == "ne_resize") {
        n = e = true;
    } else if (shape == "nw_resize") {
        n = w = true;
    } else if (shape == "se_resize") {
        s = e = true;
    } else if (shape == "sw_resize") {
        s = w = true;
    } else if (shape == "ew_resize" || shape == "col_resize" || shape == "size_hor") {
        e = w = true;
    } else if (shape == "ns_resize" || shape == "row_resize" || shape == "size_ver") {
        n = s = true;
    } else if (shape == "nesw_resize" || shape == "size_bdiag") {
        n = e = s = w = true;
        cairo_draw_resize_arrow(cr, cx, cy, 0.707, -0.707, true);
        cairo_draw_resize_arrow(cr, cx, cy, -0.707, 0.707, true);
        return;
    } else if (shape == "nwse_resize" || shape == "size_fdiag") {
        n = e = s = w = true;
        cairo_draw_resize_arrow(cr, cx, cy, -0.707, -0.707, true);
        cairo_draw_resize_arrow(cr, cx, cy, 0.707, 0.707, true);
        return;
    } else {
        e = w = true;
    }

    const int count = static_cast<int>(n) + static_cast<int>(e) + static_cast<int>(s) + static_cast<int>(w);
    const bool paired = count > 1;
    if (n)
        cairo_draw_resize_arrow(cr, cx, cy, 0.0, -1.0, paired);
    if (e)
        cairo_draw_resize_arrow(cr, cx, cy, 1.0, 0.0, paired);
    if (s)
        cairo_draw_resize_arrow(cr, cx, cy, 0.0, 1.0, paired);
    if (w)
        cairo_draw_resize_arrow(cr, cx, cy, -1.0, 0.0, paired);
}

static void queue_badge_label(const std::string& text, double anchor_x, double anchor_y, double align_x, double align_y, bool vertical, const CRegion& damage, bool inverted = false) {
    cairo_t* measure_cr = nullptr;
    cairo_surface_t* measure_surface = make_cairo_surface(1, 1, &measure_cr);
    cairo_select_font_face(measure_cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(measure_cr, px(LABEL_FONT_SIZE));

    cairo_text_extents_t extents;
    cairo_text_extents(measure_cr, text.c_str(), &extents);
    cairo_destroy(measure_cr);
    cairo_surface_destroy(measure_surface);

    const double pad_x = std::ceil(px(7.0));
    const double pad_y = std::ceil(px(4.0));
    const int text_width  = std::max(1, static_cast<int>(std::ceil(extents.width + pad_x * 2.0 + 2.0)));
    const int text_height = std::max(1, static_cast<int>(std::ceil(extents.height + pad_y * 2.0 + 2.0)));
    const int width       = vertical ? text_height : text_width;
    const int height      = vertical ? text_width : text_height;
    const double x   = std::round(anchor_x - width * align_x);
    const double y   = std::round(anchor_y - height * align_y);

    cairo_t* cr = nullptr;
    cairo_surface_t* surface = make_cairo_surface(width, height, &cr);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, px(LABEL_FONT_SIZE));

    rounded_rect(cr, 0.5, 0.5, width - 1.0, height - 1.0, px(4.0));
    if (inverted)
        cairo_set_source_rgba(cr, g_color_r, g_color_g, g_color_b, 0.92);
    else
        cairo_set_source_rgba(cr, g_bg_r, g_bg_g, g_bg_b, 0.92);
    cairo_fill(cr);

    const double tx = pad_x - extents.x_bearing + 1.0;
    const double ty = pad_y - extents.y_bearing + 1.0;

    if (inverted)
        cairo_set_source_rgba(cr, g_bg_r, g_bg_g, g_bg_b, 0.95);
    else
        cairo_set_source_rgba(cr, g_color_r, g_color_g, g_color_b, 0.95);
    if (vertical) {
        cairo_translate(cr, width, 0);
        cairo_rotate(cr, CROSSHAIR_PI / 2.0);
    }
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text.c_str());

    cairo_destroy(cr);
    queue_cairo_texture(surface, x, y, width, height, damage);
    cairo_surface_destroy(surface);
}

static void cairo_draw_center_glyph(cairo_t* cr, double cx, double cy) {
    cairo_set_line_width(cr, px(LINE_WIDTH));
    cairo_set_accent(cr, 1.0);

    if (g_cursor_intent == INTENT_TEXT || g_cursor_intent == INTENT_VERTICAL_TEXT) {
        if (g_cursor_intent == INTENT_TEXT) {
            cairo_move_to(cr, cx, cy - px(BEAM_HALF_LENGTH));
            cairo_line_to(cr, cx, cy + px(BEAM_HALF_LENGTH));
            cairo_move_to(cr, cx - px(BEAM_CAP_LENGTH), cy - px(BEAM_HALF_LENGTH));
            cairo_line_to(cr, cx + px(BEAM_CAP_LENGTH), cy - px(BEAM_HALF_LENGTH));
            cairo_move_to(cr, cx - px(BEAM_CAP_LENGTH), cy + px(BEAM_HALF_LENGTH));
            cairo_line_to(cr, cx + px(BEAM_CAP_LENGTH), cy + px(BEAM_HALF_LENGTH));
        } else {
            cairo_move_to(cr, cx - px(BEAM_HALF_LENGTH), cy);
            cairo_line_to(cr, cx + px(BEAM_HALF_LENGTH), cy);
            cairo_move_to(cr, cx - px(BEAM_HALF_LENGTH), cy - px(BEAM_CAP_LENGTH));
            cairo_line_to(cr, cx - px(BEAM_HALF_LENGTH), cy + px(BEAM_CAP_LENGTH));
            cairo_move_to(cr, cx + px(BEAM_HALF_LENGTH), cy - px(BEAM_CAP_LENGTH));
            cairo_line_to(cr, cx + px(BEAM_HALF_LENGTH), cy + px(BEAM_CAP_LENGTH));
        }
        cairo_stroke(cr);
    } else if (g_cursor_intent == INTENT_NOT_ALLOWED) {
        cairo_set_accent(cr, 0.75);
        cairo_move_to(cr, cx - px(8.0), cy - px(8.0));
        cairo_line_to(cr, cx + px(8.0), cy + px(8.0));
        cairo_move_to(cr, cx + px(8.0), cy - px(8.0));
        cairo_line_to(cr, cx - px(8.0), cy + px(8.0));
        cairo_stroke(cr);
    } else if (g_cursor_intent == INTENT_RESIZE) {
        cairo_set_accent(cr, 0.78);
        cairo_draw_resize_glyph(cr, cx, cy);
    } else if (g_cursor_intent == INTENT_MOVE || g_cursor_intent == INTENT_GRAB) {
        cairo_set_accent(cr, 0.62);
        cairo_draw_center_cross(cr, cx, cy, 9.0);
    } else if (g_cursor_intent == INTENT_WAIT) {
        cairo_set_accent(cr, 0.78);
        cairo_draw_center_cross(cr, cx, cy, 7.0);
        cairo_set_accent(cr, 0.68);
        cairo_move_to(cr, cx - px(7.0), cy);
        cairo_line_to(cr, cx + px(7.0), cy);
        cairo_stroke(cr);
    } else {
        const double t      = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double breath = g_cursor_intent == INTENT_POINTER ? (0.5 + 0.5 * std::sin(t * 5.2)) : 0.0;
        if (g_cursor_intent == INTENT_POINTER) {
            cairo_set_accent(cr, 0.24 + 0.16 * breath);
            cairo_arc(cr, cx, cy, px(8.0 + 2.0 * breath), 0, CROSSHAIR_PI * 2.0);
            cairo_stroke(cr);
            cairo_set_accent(cr, 0.95);
            cairo_arc(cr, cx, cy, px(DOT_RADIUS * (2.0 + 0.55 * breath)), 0, CROSSHAIR_PI * 2.0);
            cairo_fill(cr);
            return;
        }
        cairo_set_accent(cr, 0.95);
        cairo_draw_center_cross(cr, cx, cy, 7.0);
    }
}

static std::string pixel_label(double value) {
    return std::to_string(static_cast<int>(std::round(std::max(0.0, value)))) + "px";
}

static std::string drag_pixel_label(const Vector2D& current) {
    const int dx = static_cast<int>(std::round(std::abs(current.x - g_drag_start.x)));
    const int dy = static_cast<int>(std::round(std::abs(current.y - g_drag_start.y)));
    return std::to_string(dx) + " x " + std::to_string(dy) + "px";
}

static void draw_direction_labels(PHLMONITOR monitor, const Vector2D& local, double x, double y, const CRegion& damage) {
    const double w      = monitor->m_size.x * g_render_scale;
    const double offset = px(LABEL_OFFSET);

    queue_badge_label(pixel_label(monitor->m_size.x - local.x), w - offset, y - px(9.0), 1.0, 1.0, false, damage);
    queue_badge_label(pixel_label(local.y), x + px(9.0), offset, 0.0, 0.0, true, damage);
}

static void draw_drag_label(const Vector2D& global, double x, double y, const CRegion& damage) {
    if (!g_drag_active)
        return;

    const SInertia inertia = drag_inertia_for(global);
    const Vector2D offset  = opposite_badge_offset(inertia);
    const double align_x   = offset.x < 0.0 ? 1.0 : 0.0;
    const double align_y   = std::abs(offset.y) < px(8.0) ? 0.5 : (offset.y < 0.0 ? 1.0 : 0.0);

    queue_badge_label(drag_pixel_label(global), x + offset.x, y + offset.y, align_x, align_y, false, damage, true);
}

static void draw_projected_line_labels(PHLMONITOR monitor, const Vector2D& local, const CRegion& damage) {
    const double w      = monitor->m_size.x * g_render_scale;
    const double h      = monitor->m_size.y * g_render_scale;
    const double offset = px(LABEL_OFFSET);
    const bool draws_vertical = local.x >= 0 && local.x < monitor->m_size.x;
    const bool draws_horizontal = local.y >= 0 && local.y < monitor->m_size.y;
    const bool cursor_inside = draws_vertical && draws_horizontal;
    if (cursor_inside)
        return;

    if (draws_horizontal) {
        const double y = std::round(local.y * g_render_scale);
        queue_badge_label(pixel_label(-local.x), offset, y - px(9.0), 0.0, 1.0, false, damage);
        queue_badge_label(pixel_label(monitor->m_size.x - local.x), w - offset, y - px(9.0), 1.0, 1.0, false, damage);
    }

    if (draws_vertical) {
        const double x = std::round(local.x * g_render_scale);
        queue_badge_label(pixel_label(-local.y), x + px(9.0), offset, 0.0, 0.0, true, damage);
        queue_badge_label(pixel_label(monitor->m_size.y - local.y), x + px(9.0), h - offset, 0.0, 1.0, true, damage);
    }
}

static void draw_crosshair_lines(PHLMONITOR monitor, const Vector2D& local, const CRegion& damage) {
    const double w          = monitor->m_size.x * g_render_scale;
    const double h          = monitor->m_size.y * g_render_scale;
    const int    strip_size = std::max(12, static_cast<int>(std::ceil(px(LINE_WIDTH) + px(8.0))));
    const double strip_mid  = strip_size / 2.0;
    const double gap        = px(CENTER_GAP);

    cairo_t* cr = nullptr;
    const bool draws_vertical   = local.x >= 0 && local.x < monitor->m_size.x;
    const bool draws_horizontal = local.y >= 0 && local.y < monitor->m_size.y;
    const bool cursor_inside    = draws_vertical && draws_horizontal;
    const double x              = std::round(local.x * g_render_scale);
    const double y              = std::round(local.y * g_render_scale);
    const double vertical_gap_start   = cursor_inside ? std::max(0.0, y - gap) : h;
    const double vertical_gap_end     = cursor_inside ? std::min(h, y + gap) : h;
    const double horizontal_gap_start = cursor_inside ? std::max(0.0, x - gap) : w;
    const double horizontal_gap_end   = cursor_inside ? std::min(w, x + gap) : w;

    const double dash[] = {px(4.0), px(8.0)};

    if (draws_vertical) {
        cairo_surface_t* vertical = make_cairo_surface(strip_size, static_cast<int>(std::ceil(h)), &cr);
        cairo_set_line_width(cr, px(LINE_WIDTH));
        cairo_set_accent(cr, 0.42);
        cairo_set_dash(cr, dash, 2, 0);
        cairo_move_to(cr, strip_mid, 0);
        cairo_line_to(cr, strip_mid, vertical_gap_start);
        cairo_move_to(cr, strip_mid, vertical_gap_end);
        cairo_line_to(cr, strip_mid, h);
        cairo_stroke(cr);
        cairo_destroy(cr);
        queue_cairo_texture(vertical, x - strip_mid, 0, strip_size, h, damage);
        cairo_surface_destroy(vertical);
    }

    if (draws_horizontal) {
        cairo_surface_t* horizontal = make_cairo_surface(static_cast<int>(std::ceil(w)), strip_size, &cr);
        cairo_set_line_width(cr, px(LINE_WIDTH));
        cairo_set_accent(cr, 0.42);
        cairo_set_dash(cr, dash, 2, 0);
        cairo_move_to(cr, 0, strip_mid);
        cairo_line_to(cr, horizontal_gap_start, strip_mid);
        cairo_move_to(cr, horizontal_gap_end, strip_mid);
        cairo_line_to(cr, w, strip_mid);
        cairo_stroke(cr);
        cairo_destroy(cr);
        queue_cairo_texture(horizontal, 0, y - strip_mid, w, strip_size, damage);
        cairo_surface_destroy(horizontal);
    }

    draw_projected_line_labels(monitor, local, damage);
}

static void draw_center_hud(PHLMONITOR monitor, const Vector2D& local, double x, double y, const Time::steady_tp& now, const CRegion& damage) {
    const int    patch_size = static_cast<int>(std::ceil(px(CENTER_DAMAGE * 2.0)));
    const double center     = patch_size / 2.0;
    cairo_t* cr = nullptr;
    cairo_surface_t* patch  = make_cairo_surface(patch_size, patch_size, &cr);

    const double seconds     = std::chrono::duration<double>(now.time_since_epoch()).count();
    const bool   special_intent  = g_cursor_intent != INTENT_DEFAULT;
    const double rotation        = std::fmod(seconds * CROSSHAIR_PI * 0.72, CROSSHAIR_PI * 2.0);
    const double inner_rotation  = special_intent ? 0.0 : rotation;
    const double spike_rotation  = special_intent ? rotation * -0.72 : 0.0;
    const double special_blink   = special_intent && g_cursor_intent != INTENT_POINTER ? (0.5 + 0.5 * std::sin(seconds * 7.0)) : 0.0;
    const double ring_radius = px(RING_RADIUS * HUD_SCALE);

    cairo_set_line_width(cr, px(LINE_WIDTH));
    cairo_set_dash(cr, nullptr, 0, 0);
    cairo_set_accent(cr, special_intent && g_cursor_intent != INTENT_POINTER ? 0.34 + 0.42 * special_blink : 0.76);
    for (int i = 0; i < 3; ++i) {
        const double start = inner_rotation + i * (CROSSHAIR_PI * 2.0 / 3.0);
        cairo_arc(cr, center, center, ring_radius, start, start + CROSSHAIR_PI * 0.34);
        cairo_stroke(cr);
    }

    cairo_set_accent(cr, 0.48);
    for (int i = 0; i < 4; ++i) {
        const double angle = spike_rotation + i * (CROSSHAIR_PI / 2.0);
        cairo_move_to(cr, center + std::cos(angle) * px(TICK_INNER_RADIUS * HUD_SCALE), center + std::sin(angle) * px(TICK_INNER_RADIUS * HUD_SCALE));
        cairo_line_to(cr, center + std::cos(angle) * px(TICK_OUTER_RADIUS * HUD_SCALE), center + std::sin(angle) * px(TICK_OUTER_RADIUS * HUD_SCALE));
        cairo_stroke(cr);
    }

    cairo_set_accent(cr, 0.28);
    cairo_arc(cr, center, center, ring_radius + px(5.0), 0, CROSSHAIR_PI * 2.0);
    cairo_stroke(cr);

    cairo_draw_center_glyph(cr, center, center);
    cairo_destroy(cr);
    queue_cairo_texture(patch, x - center, y - center, patch_size, patch_size, damage);
    cairo_surface_destroy(patch);

    draw_direction_labels(monitor, local, x, y, damage);
    draw_drag_label(monitor->m_position + local, x, y, damage);
}

static void draw_crosshair(PHLMONITOR monitor, const Time::steady_tp& now, CRegion& damage) {
    if (!g_pPointerManager || !g_pHyprRenderer)
        return;

    ++g_draw_calls;

    g_render_scale = std::max(1.0, static_cast<double>(monitor->m_scale));

    const Vector2D global = g_pPointerManager->position();
    const Vector2D local  = global - monitor->m_position;
    const bool cursor_inside = local.x >= 0 && local.y >= 0 && local.x < monitor->m_size.x && local.y < monitor->m_size.y;
    const bool line_intersects = (local.x >= 0 && local.x < monitor->m_size.x) || (local.y >= 0 && local.y < monitor->m_size.y);
    if (!line_intersects)
        return;

    draw_crosshair_lines(monitor, local, damage);

    if (!cursor_inside)
        return;

    const double x = std::round(local.x * g_render_scale);
    const double y = std::round(local.y * g_render_scale);
    draw_center_hud(monitor, local, x, y, now, damage);
}

static void damage_crosshair_at(const Vector2D& global) {
    if (!g_pCompositor || !g_pHyprRenderer)
        return;

    for (const auto& monitor : g_pCompositor->m_monitors) {
        const Vector2D local = global - monitor->m_position;
        const bool draws_vertical = local.x >= 0 && local.x < monitor->m_size.x;
        const bool draws_horizontal = local.y >= 0 && local.y < monitor->m_size.y;
        if (!draws_vertical && !draws_horizontal)
            continue;

        const double x  = std::round(monitor->m_position.x + local.x);
        const double y  = std::round(monitor->m_position.y + local.y);
        const double mx = std::round(monitor->m_position.x);
        const double my = std::round(monitor->m_position.y);
        const double mw = std::round(monitor->m_size.x);
        const double mh = std::round(monitor->m_size.y);

        if (draws_vertical)
            g_pHyprRenderer->damageBox(CBox{x - 5.0, my, 10.0, mh});
        if (draws_horizontal) {
            g_pHyprRenderer->damageBox(CBox{mx, y - 5.0, mw, 10.0});
            g_pHyprRenderer->damageBox(CBox{mx, y - 32.0, 130.0, 64.0});
            g_pHyprRenderer->damageBox(CBox{mx + mw - 130.0, y - 32.0, 130.0, 64.0});
        }

        if (draws_vertical) {
            g_pHyprRenderer->damageBox(CBox{x - 40.0, my, 80.0, 80.0});
            g_pHyprRenderer->damageBox(CBox{x - 40.0, my + mh - 130.0, 80.0, 130.0});
        }

        if (draws_vertical && draws_horizontal) {
            g_pHyprRenderer->damageBox(CBox{x - CENTER_DAMAGE, y - CENTER_DAMAGE, CENTER_DAMAGE * 2.0, CENTER_DAMAGE * 2.0});
            g_pHyprRenderer->damageBox(CBox{mx + mw - 120.0, y - 24.0, 120.0, 48.0});
            g_pHyprRenderer->damageBox(CBox{x - 60.0, my, 120.0, 80.0});
            if (g_drag_active)
                g_pHyprRenderer->damageBox(CBox{x - 220.0, y - 70.0, 440.0, 140.0});
        }
    }
}

static void on_mouse_button(const IPointer::SButtonEvent& event, Event::SCallbackInfo&) {
    if (!g_pPointerManager)
        return;

    const Vector2D position = g_pPointerManager->position();
    damage_crosshair_at(position);

    if (event.state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (g_pressed_buttons == 0) {
            g_drag_start  = position;
            g_drag_active = true;
        }
        ++g_pressed_buttons;
    } else {
        g_pressed_buttons = std::max(0, g_pressed_buttons - 1);
        if (g_pressed_buttons == 0)
            g_drag_active = false;
    }

    damage_crosshair_at(position);
}

static void on_timer(SP<CEventLoopTimer> self, void*) {
    if (!g_pPointerManager) {
        self->updateTimeout(std::chrono::milliseconds(static_cast<int>(UPDATE_MS)));
        return;
    }

    if (++g_color_reload_ticks >= 30) {
        g_color_reload_ticks = 0;
        if (load_accent_color() && g_have_last_position)
            damage_crosshair_at(g_last_position);
    }

    const Vector2D position = g_pPointerManager->position();
    if (!g_have_last_position) {
        g_last_position      = position;
        g_have_last_position = true;
        damage_crosshair_at(position);
    } else if (position != g_last_position) {
        damage_crosshair_at(g_last_position);
        damage_crosshair_at(position);
        g_last_position = position;
    } else {
        damage_crosshair_at(position);
    }

    self->updateTimeout(std::chrono::milliseconds(static_cast<int>(UPDATE_MS)));
}

static void hk_render_software_cursors_for(CPointerManager* manager,
                                           PHLMONITOR pMonitor,
                                           const Time::steady_tp& now,
                                           CRegion& damage,
                                           std::optional<Vector2D> overridePos,
                                           bool forceRender) {
    (void)manager;
    (void)overridePos;
    (void)forceRender;
    ++g_render_calls;
    draw_crosshair(pMonitor, now, damage);
}

static void hk_renderer_set_cursor_from_name(Render::IHyprRenderer* renderer, const std::string& name, bool force) {
    set_shape(name.empty() ? "default" : name);
    ((RendererSetCursorFromNameFn)g_renderer_name_hook->m_original)(renderer, name, force);
}

static void hk_cursor_manager_set_cursor_from_name(CCursorManager* manager, const std::string& name) {
    set_shape(name.empty() ? "default" : name);
    ((CursorManagerSetCursorFromNameFn)g_cursor_manager_name_hook->m_original)(manager, name);
}

static CFunctionHook* hook_demangled_match(const std::string& query, const std::string& demangled_part, void* destination) {
    const auto matches = HyprlandAPI::findFunctionsByName(g_handle, query);
    for (const auto& match : matches) {
        if (!match.address || match.demangled.find(demangled_part) == std::string::npos)
            continue;

        CFunctionHook* hook = HyprlandAPI::createFunctionHook(g_handle, match.address, destination);
        if (!hook || !hook->hook())
            return nullptr;

        return hook;
    }

    return nullptr;
}

static std::string status_command(eHyprCtlOutputFormat format, std::string request) {
    (void)request;

    const bool ready = g_software_cursor_hook && g_locked_software_cursor;
    const std::string color = current_color_hex();
    const std::string background = current_bg_hex();
    if (format == FORMAT_JSON) {
        return "{"
               "\"loaded\":true,"
               "\"ready\":" + std::string(ready ? "true" : "false") + ","
               "\"intent\":\"" + intent_name(g_cursor_intent) + "\","
               "\"shape\":\"" + g_last_shape + "\","
               "\"color\":\"" + color + "\","
               "\"background\":\"" + background + "\","
               "\"software_cursor_hook\":" + (g_software_cursor_hook ? "true" : "false") + ","
               "\"renderer_hook\":" + (g_renderer_name_hook ? "true" : "false") + ","
               "\"cursor_manager_hook\":" + (g_cursor_manager_name_hook ? "true" : "false") + ","
               "\"software_cursor_locked\":" + (g_locked_software_cursor ? "true" : "false") + ","
               "\"drag_active\":" + (g_drag_active ? "true" : "false") + ","
               "\"render_calls\":" + std::to_string(g_render_calls) + ","
               "\"draw_calls\":" + std::to_string(g_draw_calls) +
               "}";
    }

    return "loaded: yes\n"
           "ready: " + std::string(ready ? "yes" : "no") + "\n"
           "intent: " + intent_name(g_cursor_intent) + "\n"
           "shape: " + g_last_shape + "\n"
           "color: " + color + "\n"
           "background: " + background + "\n"
           "software_cursor_hook: " + (g_software_cursor_hook ? "yes" : "no") + "\n"
           "renderer_hook: " + (g_renderer_name_hook ? "yes" : "no") + "\n"
           "cursor_manager_hook: " + (g_cursor_manager_name_hook ? "yes" : "no") + "\n"
           "software_cursor_locked: " + (g_locked_software_cursor ? "yes" : "no") + "\n"
           "drag_active: " + (g_drag_active ? "yes" : "no") + "\n"
           "render_calls: " + std::to_string(g_render_calls) + "\n"
           "draw_calls: " + std::to_string(g_draw_calls) + "\n";
}

APICALL EXPORT std::string pluginAPIVersion() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO pluginInit(HANDLE handle) {
    g_handle = handle;

    const std::string compositor_hash = __hyprland_api_get_hash();
    const std::string client_hash     = __hyprland_api_get_client_hash();
    if (compositor_hash != client_hash) {
        HyprlandAPI::addNotification(g_handle, "[crosshair] Header mismatch; refusing to load.", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[crosshair] Hyprland header mismatch");
    }

    load_accent_color();

    if (PROTO::cursorShape) {
        g_shape_listener = PROTO::cursorShape->m_events.setShape.listen([](const CCursorShapeProtocol::SSetShapeEvent& event) {
            set_shape(event.shapeName.empty() ? "default" : event.shapeName);
        });
    }

    if (Event::bus())
        g_mouse_button_listener = Event::bus()->m_events.input.mouse.button.listen(on_mouse_button);

    if (g_pPointerManager) {
        g_cursor_changed_listener = g_pPointerManager->m_events.cursorChanged.listen([]() { set_renderer_shape(); });
        g_pPointerManager->lockSoftwareAll();
        g_locked_software_cursor = true;
    }

    g_software_cursor_hook =
        hook_demangled_match("renderSoftwareCursorsFor", "CPointerManager::renderSoftwareCursorsFor", (void*)&hk_render_software_cursors_for);
    g_renderer_name_hook =
        hook_demangled_match("setCursorFromName", "Render::IHyprRenderer::setCursorFromName", (void*)&hk_renderer_set_cursor_from_name);
    g_cursor_manager_name_hook =
        hook_demangled_match("setCursorFromName", "CCursorManager::setCursorFromName", (void*)&hk_cursor_manager_set_cursor_from_name);

    if (g_pHyprRenderer) {
        g_pHyprRenderer->setCursorHidden(false);
        g_pHyprRenderer->setCursorFromName("left_ptr", true);
    }

    if (g_pEventLoopManager) {
        g_timer = makeShared<CEventLoopTimer>(std::chrono::milliseconds(static_cast<int>(UPDATE_MS)), on_timer, nullptr);
        g_pEventLoopManager->addTimer(g_timer);
    }

    g_status_command = HyprlandAPI::registerHyprCtlCommand(g_handle, SHyprCtlCommand{"crosshair", true, status_command});
    set_renderer_shape();

    return {
        "crosshair",
        "Draws a cursor-replacing crosshair directly inside Hyprland.",
        "bresilla",
        "0.2.0",
    };
}

APICALL EXPORT void pluginExit() {
    if (g_status_command) {
        HyprlandAPI::unregisterHyprCtlCommand(g_handle, g_status_command);
        g_status_command.reset();
    }

    if (g_timer && g_pEventLoopManager) {
        g_timer->cancel();
        g_pEventLoopManager->removeTimer(g_timer);
        g_timer.reset();
    }

    g_shape_listener.reset();
    g_cursor_changed_listener.reset();
    g_mouse_button_listener.reset();

    if (g_locked_software_cursor && g_pPointerManager) {
        g_pPointerManager->unlockSoftwareAll();
        g_locked_software_cursor = false;
    }

    if (g_software_cursor_hook) {
        HyprlandAPI::removeFunctionHook(g_handle, g_software_cursor_hook);
        g_software_cursor_hook = nullptr;
    }

    if (g_renderer_name_hook) {
        HyprlandAPI::removeFunctionHook(g_handle, g_renderer_name_hook);
        g_renderer_name_hook = nullptr;
    }

    if (g_cursor_manager_name_hook) {
        HyprlandAPI::removeFunctionHook(g_handle, g_cursor_manager_name_hook);
        g_cursor_manager_name_hook = nullptr;
    }

    if (g_have_last_position)
        damage_crosshair_at(g_last_position);

    g_handle            = nullptr;
    g_have_last_position = false;
}
