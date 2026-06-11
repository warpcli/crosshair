#pragma once

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
