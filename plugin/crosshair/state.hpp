#pragma once

static constexpr double CROSSHAIR_PI       = 3.14159265358979323846;
static constexpr double UPDATE_MS          = 16.0;
static constexpr double LINE_WIDTH         = 3.0;
static constexpr double SHADOW_OFFSET      = 2.0;
static constexpr double SHADOW_LINE_EXTRA  = 2.0;
static constexpr double SHADOW_ALPHA       = 0.72;
static constexpr double CENTER_GAP         = 52.0;
static constexpr double CENTER_DAMAGE      = 86.0;
static constexpr double DOT_RADIUS         = 3.0;
static constexpr double BEAM_HALF_LENGTH   = 12.0;
static constexpr double BEAM_CAP_LENGTH    = 4.0;
static constexpr double LABEL_FONT_SIZE    = 11.0;
static constexpr double LABEL_OFFSET       = 24.0;
static constexpr double DRAG_LABEL_OFFSET  = 70.0;
static constexpr double DRAG_LABEL_THRESHOLD = 10.0;
static constexpr double KEY_PULSE_DURATION   = 0.28;
static constexpr double MODIFIER_PULSE_DURATION = 0.65;
static constexpr double CTRL_TRIPLE_TAP_WINDOW = 1.0;
static constexpr double SUPER_TRIPLE_TAP_WINDOW = 1.0;
static constexpr double SCROLL_CHEVRON_DURATION = 0.24;
static constexpr uint32_t SCROLL_AXIS_COALESCE_MS = 18;
static constexpr double SCROLL_SPEED_FRAME_MS = 16.0;
static constexpr double SCROLL_SPEED_SCALE = 4.0;
static constexpr double CHEVRON_INERTIA_DECAY = 4.8;
static constexpr double CHEVRON_MIN_SPEED = 0.018;

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
static CHyprSignalListener g_mouse_move_listener;
static CHyprSignalListener g_mouse_button_listener;
static CHyprSignalListener g_mouse_axis_listener;
static CHyprSignalListener g_keyboard_key_listener;
static CFunctionHook*      g_renderer_name_hook       = nullptr;
static CFunctionHook*      g_cursor_manager_name_hook = nullptr;
static CFunctionHook*      g_software_cursor_hook     = nullptr;
static CFunctionHook*      g_pointer_move_hook        = nullptr;
static CFunctionHook*      g_input_mouse_moved_hook   = nullptr;
static SP<SHyprCtlCommand> g_status_command;
static SP<CEventLoopTimer> g_timer;
static std::string         g_last_shape                = "default";
static CursorIntent        g_cursor_intent             = INTENT_DEFAULT;
static Vector2D            g_last_position             = {-1, -1};
static bool                g_have_last_position        = false;
static Vector2D            g_drag_start                = {0, 0};
static int                 g_pressed_buttons           = 0;
static bool                g_drag_active               = false;
static double              g_key_pulse_started         = -10.0;
static double              g_modifier_pulse_started    = -10.0;
static double              g_scroll_chevron_started    = -10.0;
static Vector2D            g_scroll_chevron_direction  = {0, 0};
static double              g_scroll_chevron_speed      = 0.0;
static double              g_scroll_chevron_tick       = -10.0;
static Vector2D            g_scroll_axis_accumulator   = {0, 0};
static uint32_t            g_scroll_axis_time_ms       = 0;
static double              g_ctrl_tap_started          = -10.0;
static int                 g_ctrl_tap_count            = 0;
static double              g_super_tap_started         = -10.0;
static int                 g_super_tap_count           = 0;
static uint32_t            g_last_modifier_mask        = 0;
static bool                g_locked_software_cursor    = false;
static bool                g_crosshair_visible         = true;
static bool                g_minimal_mode              = false;
static bool                g_confine_enabled           = true;
static bool                g_confine_warping           = false;
static int                 g_super_held_count          = 0;
static double              g_color_r                   = 1.0;
static double              g_color_g                   = 1.0;
static double              g_color_b                   = 1.0;
static double              g_bg_r                      = 0.0;
static double              g_bg_g                      = 0.0;
static double              g_bg_b                      = 0.0;
static double              g_shadow_r                  = 0.0;
static double              g_shadow_g                  = 0.0;
static double              g_shadow_b                  = 0.0;
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
using PointerManagerMoveFn = void (*)(CPointerManager*, const Vector2D&);
using InputManagerOnMouseMovedFn = void (*)(CInputManager*, IPointer::SMotionEvent);
using RenderSoftwareCursorsForFn = void (*)(CPointerManager*, PHLMONITOR, const Time::steady_tp&, CRegion&, std::optional<Vector2D>, bool);

static void damage_crosshair_at(const Vector2D& global);
