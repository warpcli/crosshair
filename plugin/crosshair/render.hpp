#pragma once

static void cairo_set_accent(cairo_t* cr, double alpha) {
    cairo_set_source_rgba(cr, g_color_r, g_color_g, g_color_b, alpha);
}

static void cairo_set_shadow(cairo_t* cr, double alpha) {
    cairo_set_source_rgba(cr, g_shadow_r, g_shadow_g, g_shadow_b, alpha);
}

static void cairo_stroke_shadowed(cairo_t* cr, double accent_alpha) {
    const double line_width = cairo_get_line_width(cr);
    cairo_save(cr);
    cairo_translate(cr, px(SHADOW_OFFSET), px(SHADOW_OFFSET));
    cairo_set_line_width(cr, line_width + px(SHADOW_LINE_EXTRA));
    cairo_set_shadow(cr, SHADOW_ALPHA * accent_alpha);
    cairo_stroke_preserve(cr);
    cairo_restore(cr);

    cairo_set_line_width(cr, line_width);
    cairo_set_accent(cr, accent_alpha);
    cairo_stroke(cr);
}

static void cairo_fill_shadowed(cairo_t* cr, double accent_alpha) {
    cairo_save(cr);
    cairo_translate(cr, px(SHADOW_OFFSET), px(SHADOW_OFFSET));
    cairo_set_shadow(cr, SHADOW_ALPHA * accent_alpha);
    cairo_fill_preserve(cr);
    cairo_restore(cr);

    cairo_set_accent(cr, accent_alpha);
    cairo_fill(cr);
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

static void cairo_draw_center_cross(cairo_t* cr, double cx, double cy, double size, double alpha) {
    cairo_move_to(cr, cx - px(size), cy);
    cairo_line_to(cr, cx + px(size), cy);
    cairo_move_to(cr, cx, cy - px(size));
    cairo_line_to(cr, cx, cy + px(size));
    cairo_stroke_shadowed(cr, alpha);
}

static void advance_chevron_inertia() {
    const double now = steady_seconds();
    if (g_scroll_chevron_tick < 0.0) {
        g_scroll_chevron_tick = now;
        return;
    }

    const double dt = std::clamp(now - g_scroll_chevron_tick, 0.0, 0.08);
    g_scroll_chevron_tick = now;

    if (g_scroll_chevron_speed <= 0.0)
        return;

    g_scroll_chevron_speed *= std::exp(-CHEVRON_INERTIA_DECAY * dt);
    if (g_scroll_chevron_speed < CHEVRON_MIN_SPEED)
        g_scroll_chevron_speed = 0.0;
}

static void cairo_draw_scroll_chevron(cairo_t* cr, double cx, double cy, double size, const Vector2D& direction, double spread, double alpha) {
    const SInertia inertia = inertia_from_delta(direction, 0.01);
    if (!inertia.active)
        return;

    const double dx = inertia.direction.x;
    const double dy = inertia.direction.y;
    const double pxn = -dy;
    const double pyn = dx;
    const double motion = std::clamp(spread, -0.45, 1.0);
    const double half_width = px(size);
    const double half_height = px(size * 0.42);
    const double gap = px(size * (0.36 + 0.24 * motion));

    for (double offset : {-gap, gap}) {
        const double chevron_center_x = cx + dx * offset;
        const double chevron_center_y = cy + dy * offset;
        const double tip_x = chevron_center_x + dx * half_height;
        const double tip_y = chevron_center_y + dy * half_height;
        const double wing_x = chevron_center_x - dx * half_height;
        const double wing_y = chevron_center_y - dy * half_height;

        cairo_move_to(cr, wing_x - pxn * half_width, wing_y - pyn * half_width);
        cairo_line_to(cr, tip_x, tip_y);
        cairo_line_to(cr, wing_x + pxn * half_width, wing_y + pyn * half_width);
    }
    cairo_stroke_shadowed(cr, alpha);
}

static void cairo_draw_arrowhead(cairo_t* cr, double tip_x, double tip_y, double dx, double dy, double alpha) {
    const double wing = px(5.0);
    const double pxn  = -dy;
    const double pyn  = dx;
    const double back_x = tip_x - dx * wing;
    const double back_y = tip_y - dy * wing;

    cairo_move_to(cr, tip_x, tip_y);
    cairo_line_to(cr, back_x + pxn * wing * 0.72, back_y + pyn * wing * 0.72);
    cairo_move_to(cr, tip_x, tip_y);
    cairo_line_to(cr, back_x - pxn * wing * 0.72, back_y - pyn * wing * 0.72);
    cairo_stroke_shadowed(cr, alpha);
}

static void cairo_draw_resize_arrow(cairo_t* cr, double cx, double cy, double dx, double dy, bool paired, double alpha) {
    const double tip_len  = px(14.0);
    const double tail_len = px(paired ? 14.0 : 3.0);
    const double start_x  = cx - dx * tail_len;
    const double start_y  = cy - dy * tail_len;
    const double tip_x    = cx + dx * tip_len;
    const double tip_y    = cy + dy * tip_len;

    cairo_move_to(cr, start_x, start_y);
    cairo_line_to(cr, tip_x, tip_y);
    cairo_stroke_shadowed(cr, alpha);
    cairo_draw_arrowhead(cr, tip_x, tip_y, dx, dy, alpha);
}

static void cairo_draw_resize_glyph(cairo_t* cr, double cx, double cy, double alpha) {
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
        cairo_draw_resize_arrow(cr, cx, cy, 0.707, -0.707, true, alpha);
        cairo_draw_resize_arrow(cr, cx, cy, -0.707, 0.707, true, alpha);
        return;
    } else if (shape == "nwse_resize" || shape == "size_fdiag") {
        n = e = s = w = true;
        cairo_draw_resize_arrow(cr, cx, cy, -0.707, -0.707, true, alpha);
        cairo_draw_resize_arrow(cr, cx, cy, 0.707, 0.707, true, alpha);
        return;
    } else {
        e = w = true;
    }

    const int count = static_cast<int>(n) + static_cast<int>(e) + static_cast<int>(s) + static_cast<int>(w);
    const bool paired = count > 1;
    if (n)
        cairo_draw_resize_arrow(cr, cx, cy, 0.0, -1.0, paired, alpha);
    if (e)
        cairo_draw_resize_arrow(cr, cx, cy, 1.0, 0.0, paired, alpha);
    if (s)
        cairo_draw_resize_arrow(cr, cx, cy, 0.0, 1.0, paired, alpha);
    if (w)
        cairo_draw_resize_arrow(cr, cx, cy, -1.0, 0.0, paired, alpha);
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
    const int content_width  = vertical ? text_height : text_width;
    const int content_height = vertical ? text_width : text_height;
    const double shadow_pad  = std::ceil(px(SHADOW_OFFSET + SHADOW_LINE_EXTRA + 2.0));
    const int width          = static_cast<int>(content_width + shadow_pad * 2.0);
    const int height         = static_cast<int>(content_height + shadow_pad * 2.0);
    const double x = std::round(anchor_x - content_width * align_x - shadow_pad);
    const double y = std::round(anchor_y - content_height * align_y - shadow_pad);

    cairo_t* cr = nullptr;
    cairo_surface_t* surface = make_cairo_surface(width, height, &cr);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, px(LABEL_FONT_SIZE));

    rounded_rect(cr, shadow_pad + px(SHADOW_OFFSET) + 0.5, shadow_pad + px(SHADOW_OFFSET) + 0.5, content_width - 1.0, content_height - 1.0, px(4.0));
    cairo_set_shadow(cr, 0.58);
    cairo_fill(cr);

    rounded_rect(cr, shadow_pad + 0.5, shadow_pad + 0.5, content_width - 1.0, content_height - 1.0, px(4.0));
    if (inverted)
        cairo_set_source_rgba(cr, g_color_r, g_color_g, g_color_b, 0.92);
    else
        cairo_set_source_rgba(cr, g_bg_r, g_bg_g, g_bg_b, 0.92);
    cairo_fill(cr);

    const double tx = shadow_pad + pad_x - extents.x_bearing + 1.0;
    const double ty = shadow_pad + pad_y - extents.y_bearing + 1.0;

    if (vertical) {
        cairo_translate(cr, width, 0);
        cairo_rotate(cr, CROSSHAIR_PI / 2.0);
    }

    if (inverted)
        cairo_set_source_rgba(cr, g_bg_r, g_bg_g, g_bg_b, 0.95);
    else
        cairo_set_source_rgba(cr, g_color_r, g_color_g, g_color_b, 0.95);
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text.c_str());

    cairo_destroy(cr);
    queue_cairo_texture(surface, x, y, width, height, damage);
    cairo_surface_destroy(surface);
}

static void cairo_draw_center_glyph(cairo_t* cr, double cx, double cy) {
    advance_chevron_inertia();

    const double key_pulse = active_pulse(g_key_pulse_started, KEY_PULSE_DURATION);
    const double scroll_age = steady_seconds() - g_scroll_chevron_started;
    const bool scroll_active = g_scroll_chevron_speed > 0.0 && scroll_age >= 0.0 && scroll_age <= SCROLL_CHEVRON_DURATION;
    const double scroll_pulse = scroll_age >= 0.0 && scroll_age <= SCROLL_CHEVRON_DURATION
        ? std::sin(std::clamp(scroll_age / SCROLL_CHEVRON_DURATION, 0.0, 1.0) * CROSSHAIR_PI)
        : 0.0;

    cairo_set_line_width(cr, px(LINE_WIDTH));

    if (scroll_active && vector_length(g_scroll_chevron_direction) > 0.0) {
        const double speed_pulse = std::clamp(g_scroll_chevron_speed, 0.0, 1.0);
        const double size = 12.0 + 3.0 * std::max(key_pulse, scroll_pulse);
        cairo_draw_scroll_chevron(cr, cx, cy, size, g_scroll_chevron_direction, speed_pulse, 0.78 + 0.18 * std::max(speed_pulse, scroll_pulse));
        return;
    }

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
        cairo_stroke_shadowed(cr, 1.0);
    } else if (g_cursor_intent == INTENT_NOT_ALLOWED) {
        cairo_move_to(cr, cx - px(8.0), cy - px(8.0));
        cairo_line_to(cr, cx + px(8.0), cy + px(8.0));
        cairo_move_to(cr, cx + px(8.0), cy - px(8.0));
        cairo_line_to(cr, cx - px(8.0), cy + px(8.0));
        cairo_stroke_shadowed(cr, 0.75);
    } else if (g_cursor_intent == INTENT_RESIZE) {
        cairo_draw_resize_glyph(cr, cx, cy, 0.78);
    } else if (g_cursor_intent == INTENT_MOVE || g_cursor_intent == INTENT_GRAB) {
        cairo_draw_center_cross(cr, cx, cy, 16.0 + 3.0 * key_pulse, 0.62 + 0.28 * key_pulse);
    } else if (g_cursor_intent == INTENT_WAIT) {
        cairo_draw_center_cross(cr, cx, cy, 14.0 + 3.0 * key_pulse, 0.78 + 0.18 * key_pulse);
        cairo_move_to(cr, cx - px(7.0), cy);
        cairo_line_to(cr, cx + px(7.0), cy);
        cairo_stroke_shadowed(cr, 0.68);
    } else {
        const double t      = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double breath = g_cursor_intent == INTENT_POINTER ? (0.5 + 0.5 * std::sin(t * 5.2)) : 0.0;
        if (g_cursor_intent == INTENT_POINTER) {
            cairo_arc(cr, cx, cy, px(8.0 + 2.0 * breath), 0, CROSSHAIR_PI * 2.0);
            cairo_stroke_shadowed(cr, 0.24 + 0.16 * breath);
            cairo_arc(cr, cx, cy, px(DOT_RADIUS * (2.0 + 0.55 * breath)), 0, CROSSHAIR_PI * 2.0);
            cairo_fill_shadowed(cr, 0.95);
            return;
        }
        cairo_draw_center_cross(cr, cx, cy, 14.0 + 4.0 * key_pulse, 0.95);
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
    if (!g_drag_active || !drag_label_visible_for(global))
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
        cairo_set_dash(cr, dash, 2, 0);
        cairo_move_to(cr, strip_mid, 0);
        cairo_line_to(cr, strip_mid, vertical_gap_start);
        cairo_move_to(cr, strip_mid, vertical_gap_end);
        cairo_line_to(cr, strip_mid, h);
        cairo_stroke_shadowed(cr, 0.42);
        cairo_destroy(cr);
        queue_cairo_texture(vertical, x - strip_mid, 0, strip_size, h, damage);
        cairo_surface_destroy(vertical);
    }

    if (draws_horizontal) {
        cairo_surface_t* horizontal = make_cairo_surface(static_cast<int>(std::ceil(w)), strip_size, &cr);
        cairo_set_line_width(cr, px(LINE_WIDTH));
        cairo_set_dash(cr, dash, 2, 0);
        cairo_move_to(cr, 0, strip_mid);
        cairo_line_to(cr, horizontal_gap_start, strip_mid);
        cairo_move_to(cr, horizontal_gap_end, strip_mid);
        cairo_line_to(cr, w, strip_mid);
        cairo_stroke_shadowed(cr, 0.42);
        cairo_destroy(cr);
        queue_cairo_texture(horizontal, 0, y - strip_mid, w, strip_size, damage);
        cairo_surface_destroy(horizontal);
    }

    draw_projected_line_labels(monitor, local, damage);
}

static void cairo_draw_modifier_pulse(cairo_t* cr, double center) {
    const double age = steady_seconds() - g_modifier_pulse_started;
    if (age < 0.0 || age > MODIFIER_PULSE_DURATION)
        return;

    const double t    = std::clamp(age / MODIFIER_PULSE_DURATION, 0.0, 1.0);
    const double ease = 1.0 - std::pow(1.0 - t, 3.0);
    const double fade = 1.0 - t;

    cairo_set_line_width(cr, px(LINE_WIDTH));
    cairo_set_dash(cr, nullptr, 0, 0);
    cairo_arc(cr, center, center, px(34.0 + 42.0 * ease), 0, CROSSHAIR_PI * 2.0);
    cairo_stroke_shadowed(cr, 0.62 * fade);

    cairo_arc(cr, center, center, px(22.0 + 32.0 * ease), 0, CROSSHAIR_PI * 2.0);
    cairo_stroke_shadowed(cr, 0.34 * fade);
}

static void draw_center_hud(PHLMONITOR monitor, const Vector2D& local, double x, double y, const Time::steady_tp& now, const CRegion& damage) {
    const int    patch_size = static_cast<int>(std::ceil(px(CENTER_DAMAGE * 2.0)));
    const double center     = patch_size / 2.0;
    cairo_t* cr = nullptr;
    cairo_surface_t* patch  = make_cairo_surface(patch_size, patch_size, &cr);

    (void)now;

    cairo_set_line_width(cr, px(LINE_WIDTH));
    cairo_set_dash(cr, nullptr, 0, 0);
    cairo_draw_modifier_pulse(cr, center);
    cairo_draw_center_glyph(cr, center, center);
    cairo_destroy(cr);
    queue_cairo_texture(patch, x - center, y - center, patch_size, patch_size, damage);
    cairo_surface_destroy(patch);

    if (!g_minimal_mode)
        draw_direction_labels(monitor, local, x, y, damage);
    draw_drag_label(monitor->m_position + local, x, y, damage);
}

static void draw_crosshair(PHLMONITOR monitor, const Time::steady_tp& now, CRegion& damage) {
    if (!g_pPointerManager || !g_pHyprRenderer)
        return;

    ++g_draw_calls;

    if (!g_crosshair_visible)
        return;

    g_render_scale = std::max(1.0, static_cast<double>(monitor->m_scale));

    const Vector2D global = g_pPointerManager->position();
    const Vector2D local  = global - monitor->m_position;
    const bool cursor_inside = local.x >= 0 && local.y >= 0 && local.x < monitor->m_size.x && local.y < monitor->m_size.y;
    const bool line_intersects = (local.x >= 0 && local.x < monitor->m_size.x) || (local.y >= 0 && local.y < monitor->m_size.y);
    if (!line_intersects)
        return;

    if (!g_minimal_mode)
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
            g_pHyprRenderer->damageBox(CBox{x - 8.0, my, 16.0, mh});
        if (draws_horizontal) {
            g_pHyprRenderer->damageBox(CBox{mx, y - 8.0, mw, 16.0});
            g_pHyprRenderer->damageBox(CBox{mx, y - 40.0, 150.0, 80.0});
            g_pHyprRenderer->damageBox(CBox{mx + mw - 150.0, y - 40.0, 150.0, 80.0});
        }

        if (draws_vertical) {
            g_pHyprRenderer->damageBox(CBox{x - 50.0, my, 100.0, 100.0});
            g_pHyprRenderer->damageBox(CBox{x - 50.0, my + mh - 150.0, 100.0, 150.0});
        }

        if (draws_vertical && draws_horizontal) {
            g_pHyprRenderer->damageBox(CBox{x - CENTER_DAMAGE - 8.0, y - CENTER_DAMAGE - 8.0, CENTER_DAMAGE * 2.0 + 16.0, CENTER_DAMAGE * 2.0 + 16.0});
            g_pHyprRenderer->damageBox(CBox{mx + mw - 140.0, y - 32.0, 140.0, 64.0});
            g_pHyprRenderer->damageBox(CBox{x - 70.0, my, 140.0, 100.0});
            if (g_drag_active)
                g_pHyprRenderer->damageBox(CBox{x - 240.0, y - 80.0, 480.0, 160.0});
        }
    }
}
