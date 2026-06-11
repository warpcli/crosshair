#pragma once

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
    g_last_modifier_mask = g_pInputManager ? pulse_modifier_mask(g_pInputManager->getModsFromAllKBs()) : 0;

    if (PROTO::cursorShape) {
        g_shape_listener = PROTO::cursorShape->m_events.setShape.listen([](const CCursorShapeProtocol::SSetShapeEvent& event) {
            set_shape(event.shapeName.empty() ? "default" : event.shapeName);
        });
    }

    if (Event::bus()) {
        g_mouse_button_listener = Event::bus()->m_events.input.mouse.button.listen(on_mouse_button);
        g_mouse_axis_listener = Event::bus()->m_events.input.mouse.axis.listen(on_mouse_axis);
        g_keyboard_key_listener = Event::bus()->m_events.input.keyboard.key.listen(on_keyboard_key);
    }

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

    g_status_command = HyprlandAPI::registerHyprCtlCommand(g_handle, SHyprCtlCommand{"crosshair", false, status_command});
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
    g_mouse_axis_listener.reset();
    g_keyboard_key_listener.reset();

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
