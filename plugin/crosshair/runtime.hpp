#pragma once

static void on_mouse_move(const Vector2D&, Event::SCallbackInfo&) {
    if (!g_pPointerManager || !g_confine_enabled || g_confine_warping)
        return;

    confine_pointer_position(g_pPointerManager->position());
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

static void push_chevron_inertia(const Vector2D& direction, double speed) {
    const SInertia inertia = inertia_from_delta(direction, 0.01);
    if (!inertia.active)
        return;

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());

    const double impulse = std::clamp(speed, 0.0, 1.0);
    const Vector2D current = g_scroll_chevron_direction * g_scroll_chevron_speed;
    const Vector2D incoming = inertia.direction * impulse;
    const SInertia combined = inertia_from_delta(current * 0.62 + incoming, 0.01);
    if (combined.active)
        g_scroll_chevron_direction = combined.direction;

    const double now = steady_seconds();
    g_scroll_chevron_speed   = std::clamp(std::max(g_scroll_chevron_speed * 0.82, impulse), 0.0, 1.0);
    g_scroll_chevron_started = now;
    if (g_scroll_chevron_tick < 0.0)
        g_scroll_chevron_tick = now;

    if (g_pPointerManager)
        damage_crosshair_at(g_pPointerManager->position());
}

static void on_mouse_axis(const IPointer::SAxisEvent& event, Event::SCallbackInfo&) {
    if (event.axis != WL_POINTER_AXIS_VERTICAL_SCROLL && event.axis != WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        return;

    double delta = event.delta;
    if (std::abs(delta) < 0.01)
        delta = static_cast<double>(event.deltaDiscrete);
    if (std::abs(delta) < 0.01)
        return;

    const bool continuing_gesture = g_scroll_axis_time_ms != 0 && event.timeMs - g_scroll_axis_time_ms <= SCROLL_AXIS_COALESCE_MS;
    const uint32_t elapsed_ms = continuing_gesture ? event.timeMs - g_scroll_axis_time_ms : 0;
    if (!continuing_gesture)
        g_scroll_axis_accumulator = {0, 0};

    g_scroll_axis_time_ms = event.timeMs;
    if (event.axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        g_scroll_axis_accumulator.x += delta;
    else
        g_scroll_axis_accumulator.y += delta;

    const double interval_ms = std::max(1.0, static_cast<double>(elapsed_ms == 0 ? SCROLL_SPEED_FRAME_MS : elapsed_ms));
    const double frame_delta = std::abs(delta) * SCROLL_SPEED_FRAME_MS / interval_ms;
    const double speed = std::sqrt(std::clamp(frame_delta * SCROLL_SPEED_SCALE, 0.0, 1.0));
    const double blended_speed = continuing_gesture ? std::max(g_scroll_chevron_speed * 0.74, speed) : speed;

    push_chevron_inertia(g_scroll_axis_accumulator, blended_speed);
}

static void on_keyboard_key(const IKeyboard::SKeyEvent& event, Event::SCallbackInfo&) {
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    if (is_ctrl_keycode(event.keycode))
        register_ctrl_tap();
    else
        g_ctrl_tap_count = 0;

    if (is_super_keycode(event.keycode))
        register_super_tap();
    else
        g_super_tap_count = 0;

    trigger_key_pulse(is_pulse_modifier_keycode(event.keycode));
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

    if (g_pInputManager) {
        const uint32_t modifier_mask = pulse_modifier_mask(g_pInputManager->getModsFromAllKBs());
        const uint32_t newly_pressed = modifier_mask & ~g_last_modifier_mask;
        if (newly_pressed)
            trigger_key_pulse(true);
        g_last_modifier_mask = modifier_mask;
    }

    const Vector2D position = confine_pointer_position(g_pPointerManager->position());
    if (!g_crosshair_visible) {
        g_last_position      = position;
        g_have_last_position = true;
    } else if (!g_have_last_position) {
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

static void hk_pointer_manager_move(CPointerManager* manager, const Vector2D& delta) {
    if (!g_confine_enabled || g_confine_warping) {
        ((PointerManagerMoveFn)g_pointer_move_hook->m_original)(manager, delta);
        return;
    }

    const PHLWINDOW window = active_window();
    if (!window || window->m_size.x <= 2.0 || window->m_size.y <= 2.0) {
        ((PointerManagerMoveFn)g_pointer_move_hook->m_original)(manager, delta);
        return;
    }

    const Vector2D current = manager->position();
    const Vector2D target = current + delta;
    const Vector2D clamped = clamped_to_window(target, window);
    ((PointerManagerMoveFn)g_pointer_move_hook->m_original)(manager, clamped - current);
}

static void hk_input_manager_on_mouse_moved(CInputManager* manager, IPointer::SMotionEvent event) {
    if (g_confine_enabled && !g_confine_warping && g_pPointerManager) {
        const PHLWINDOW window = active_window();
        const CBox box = confine_window_box(window);
        if (window && box.w > 2.0 && box.h > 2.0) {
            const Vector2D current = g_pPointerManager->position();
            const Vector2D clamped = clamped_to_window(current + event.delta, window);
            event.delta = clamped - current;
        }
    }

    ((InputManagerOnMouseMovedFn)g_input_mouse_moved_hook->m_original)(manager, event);
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
    const std::string action = command_action(std::move(request));

    if (action == "hide" || action == "off" || action == "disable") {
        if (g_crosshair_visible && g_have_last_position)
            damage_crosshair_at(g_last_position);
        g_crosshair_visible = false;
        if (g_pPointerManager) {
            g_last_position      = g_pPointerManager->position();
            g_have_last_position = true;
        }
    } else if (action == "show" || action == "on" || action == "enable") {
        g_crosshair_visible = true;
        if (g_pPointerManager) {
            g_last_position      = g_pPointerManager->position();
            g_have_last_position = true;
            damage_crosshair_at(g_last_position);
        }
    } else if (action == "toggle") {
        if (g_crosshair_visible && g_have_last_position)
            damage_crosshair_at(g_last_position);
        g_crosshair_visible = !g_crosshair_visible;
        if (g_pPointerManager) {
            g_last_position      = g_pPointerManager->position();
            g_have_last_position = true;
            if (g_crosshair_visible)
                damage_crosshair_at(g_last_position);
        }
    } else if (action == "minimal" || action == "simple") {
        set_minimal_mode(true);
    } else if (action == "full" || action == "normal") {
        set_minimal_mode(false);
    } else if (action == "toggle-minimal" || action == "minimal-toggle") {
        toggle_minimal_mode();
    } else if (action == "confine" || action == "confine-on" || action == "confine-enable") {
        set_confine_enabled(true);
    } else if (action == "unconfine" || action == "confine-off" || action == "confine-disable") {
        set_confine_enabled(false);
    } else if (action == "toggle-confine" || action == "confine-toggle") {
        toggle_confine_enabled();
    }

    const bool ready = g_software_cursor_hook && g_locked_software_cursor;
    const bool key_pulse_active = steady_seconds() - g_key_pulse_started <= KEY_PULSE_DURATION;
    const bool modifier_pulse_active = steady_seconds() - g_modifier_pulse_started <= MODIFIER_PULSE_DURATION;
    const std::string color = current_color_hex();
    const std::string background = current_bg_hex();
    const std::string shadow = current_shadow_hex();
    if (format == FORMAT_JSON) {
        return "{"
               "\"loaded\":true,"
               "\"ready\":" + std::string(ready ? "true" : "false") + ","
               "\"intent\":\"" + intent_name(g_cursor_intent) + "\","
               "\"shape\":\"" + g_last_shape + "\","
               "\"visible\":" + (g_crosshair_visible ? "true" : "false") + ","
               "\"minimal_mode\":" + (g_minimal_mode ? "true" : "false") + ","
               "\"confine_enabled\":" + (g_confine_enabled ? "true" : "false") + ","
               "\"color\":\"" + color + "\","
               "\"background\":\"" + background + "\","
               "\"shadow\":\"" + shadow + "\","
               "\"software_cursor_hook\":" + (g_software_cursor_hook ? "true" : "false") + ","
               "\"pointer_move_hook\":" + (g_pointer_move_hook ? "true" : "false") + ","
               "\"input_mouse_moved_hook\":" + (g_input_mouse_moved_hook ? "true" : "false") + ","
               "\"renderer_hook\":" + (g_renderer_name_hook ? "true" : "false") + ","
               "\"cursor_manager_hook\":" + (g_cursor_manager_name_hook ? "true" : "false") + ","
               "\"software_cursor_locked\":" + (g_locked_software_cursor ? "true" : "false") + ","
               "\"drag_active\":" + (g_drag_active ? "true" : "false") + ","
               "\"key_pulse_active\":" + (key_pulse_active ? "true" : "false") + ","
               "\"modifier_pulse_active\":" + (modifier_pulse_active ? "true" : "false") + ","
               "\"render_calls\":" + std::to_string(g_render_calls) + ","
               "\"draw_calls\":" + std::to_string(g_draw_calls) +
               "}";
    }

    return "loaded: yes\n"
           "ready: " + std::string(ready ? "yes" : "no") + "\n"
           "intent: " + intent_name(g_cursor_intent) + "\n"
           "shape: " + g_last_shape + "\n"
           "visible: " + (g_crosshair_visible ? "yes" : "no") + "\n"
           "minimal_mode: " + (g_minimal_mode ? "yes" : "no") + "\n"
           "confine_enabled: " + (g_confine_enabled ? "yes" : "no") + "\n"
           "color: " + color + "\n"
           "background: " + background + "\n"
           "shadow: " + shadow + "\n"
           "software_cursor_hook: " + (g_software_cursor_hook ? "yes" : "no") + "\n"
           "pointer_move_hook: " + (g_pointer_move_hook ? "yes" : "no") + "\n"
           "input_mouse_moved_hook: " + (g_input_mouse_moved_hook ? "yes" : "no") + "\n"
           "renderer_hook: " + (g_renderer_name_hook ? "yes" : "no") + "\n"
           "cursor_manager_hook: " + (g_cursor_manager_name_hook ? "yes" : "no") + "\n"
           "software_cursor_locked: " + (g_locked_software_cursor ? "yes" : "no") + "\n"
           "drag_active: " + (g_drag_active ? "yes" : "no") + "\n"
           "key_pulse_active: " + (key_pulse_active ? "yes" : "no") + "\n"
           "modifier_pulse_active: " + (modifier_pulse_active ? "yes" : "no") + "\n"
           "render_calls: " + std::to_string(g_render_calls) + "\n"
           "draw_calls: " + std::to_string(g_draw_calls) + "\n";
}
