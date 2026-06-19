#pragma once

static double px(double value) {
    return value * g_render_scale;
}

static double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static double active_pulse(double started, double duration) {
    const double age = steady_seconds() - started;
    if (age < 0.0 || age > duration)
        return 0.0;

    const double t = std::clamp(age / duration, 0.0, 1.0);
    return std::sin(t * CROSSHAIR_PI);
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

static bool drag_label_visible_for(const Vector2D& current) {
    const Vector2D delta = current - g_drag_start;
    return std::abs(delta.x) >= DRAG_LABEL_THRESHOLD || std::abs(delta.y) >= DRAG_LABEL_THRESHOLD;
}

static Vector2D opposite_badge_offset(const SInertia& inertia, double fallback_y = 24.0) {
    if (!inertia.active)
        return {px(DRAG_LABEL_OFFSET), px(fallback_y)};

    return {-inertia.direction.x * px(DRAG_LABEL_OFFSET), -inertia.direction.y * px(DRAG_LABEL_OFFSET)};
}

static bool is_ctrl_keycode(uint32_t keycode) {
    return keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL;
}

static bool is_super_keycode(uint32_t keycode) {
    return keycode == KEY_LEFTMETA || keycode == KEY_RIGHTMETA;
}

static bool is_pulse_modifier_keycode(uint32_t keycode) {
    return is_ctrl_keycode(keycode)
        || keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT
        || is_super_keycode(keycode);
}

static uint32_t pulse_modifier_mask(uint32_t modifiers) {
    return modifiers & (HL_MODIFIER_CTRL | HL_MODIFIER_ALT | HL_MODIFIER_META);
}

static void trigger_key_pulse(bool outer_pulse) {
    const double now = steady_seconds();
    g_key_pulse_started = now;
    if (outer_pulse)
        g_modifier_pulse_started = now;

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());
}

static void set_minimal_mode(bool minimal) {
    if (g_minimal_mode == minimal)
        return;

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());

    g_minimal_mode = minimal;

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());
}

static void toggle_minimal_mode() {
    set_minimal_mode(!g_minimal_mode);
}

static void set_confine_enabled(bool enabled) {
    if (g_confine_enabled == enabled)
        return;

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());

    g_confine_enabled = enabled;
    trigger_key_pulse(true);

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());
}

static void toggle_confine_enabled() {
    set_confine_enabled(!g_confine_enabled);
}

static bool confine_effective_enabled() {
    return g_confine_enabled && g_super_held_count <= 0;
}

static void register_ctrl_tap() {
    const double now = steady_seconds();
    if (now - g_ctrl_tap_started > CTRL_TRIPLE_TAP_WINDOW) {
        g_ctrl_tap_started = now;
        g_ctrl_tap_count   = 1;
        return;
    }

    ++g_ctrl_tap_count;
    if (g_ctrl_tap_count >= 3) {
        g_ctrl_tap_started = -10.0;
        g_ctrl_tap_count   = 0;
        toggle_minimal_mode();
    }
}

static void register_super_tap() {
    const double now = steady_seconds();
    if (now - g_super_tap_started > SUPER_TRIPLE_TAP_WINDOW) {
        g_super_tap_started = now;
        g_super_tap_count   = 1;
        return;
    }

    ++g_super_tap_count;
    if (g_super_tap_count >= 3) {
        g_super_tap_started = -10.0;
        g_super_tap_count   = 0;
        toggle_confine_enabled();
    }
}

static PHLWINDOW active_window() {
    if (!g_pCompositor)
        return nullptr;

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || !window->m_isMapped)
            continue;
        if (g_pCompositor->isWindowActive(window))
            return window;
    }

    return nullptr;
}

static CBox confine_window_box(PHLWINDOW window) {
    if (!window)
        return CBox{0, 0, 0, 0};

    return window->getWindowMainSurfaceBox();
}

static CBox confine_target_box(const Vector2D& position) {
    const PHLWINDOW window = active_window();
    const CBox window_box = confine_window_box(window);
    if (window && window_box.w > 2.0 && window_box.h > 2.0)
        return window_box;

    if (!g_pCompositor)
        return CBox{0, 0, 0, 0};

    PHLMONITOR monitor = g_pCompositor->getMonitorFromVector(position);
    if (!monitor)
        monitor = g_pCompositor->getMonitorFromCursor();
    if (!monitor)
        return CBox{0, 0, 0, 0};

    return CBox{monitor->m_position.x, monitor->m_position.y, monitor->m_size.x, monitor->m_size.y};
}

static Vector2D clamped_to_box(const Vector2D& position, const CBox& box) {
    constexpr double INSET = 1.0;
    const double min_x = box.x + INSET;
    const double min_y = box.y + INSET;
    const double max_x = box.x + std::max(INSET, box.w - INSET);
    const double max_y = box.y + std::max(INSET, box.h - INSET);

    return {
        std::clamp(position.x, min_x, max_x),
        std::clamp(position.y, min_y, max_y),
    };
}

static Vector2D confine_pointer_position(const Vector2D& position) {
    if (!confine_effective_enabled() || g_confine_warping || !g_pPointerManager)
        return position;

    const CBox box = confine_target_box(position);
    if (box.w <= 2.0 || box.h <= 2.0)
        return position;

    const Vector2D clamped = clamped_to_box(position, box);
    if (clamped == position)
        return position;

    damage_crosshair_at(position);
    g_confine_warping = true;
    g_pPointerManager->warpTo(clamped);
    g_confine_warping = false;
    damage_crosshair_at(clamped);
    return clamped;
}
