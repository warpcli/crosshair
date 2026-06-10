#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UPDATE_MS 8
#define ANIMATION_MS 16
#define LINE_WIDTH 1.8
#define DAMAGE_PAD 8
#define CENTER_GAP 24.0
#define CENTER_DAMAGE 42
#define DOT_RADIUS 3.0
#define RING_RADIUS 17.0
#define TICK_INNER_RADIUS 23.0
#define TICK_OUTER_RADIUS 30.0
#define PI 3.14159265358979323846

typedef struct {
    GtkWidget *area;
    GdkRectangle geometry;
    double local_x;
    double local_y;
    gboolean inside;
} CrosshairSurface;

static GPtrArray *surfaces = NULL;
static pid_t wlsunset_pid = -1;
static double cursor_x = -1.0;
static double cursor_y = -1.0;
static double line_r = 1.0;
static double line_g = 1.0;
static double line_b = 1.0;
static gboolean line_color_loaded = FALSE;
static gboolean cursor_hidden = FALSE;
static guint color_reload_source = 0;
static GFileMonitor *colors_json_monitor = NULL;
static GFileMonitor *colors_monitor = NULL;

static void kill_wlsunset(void) {
    if (wlsunset_pid > 0)
        kill(wlsunset_pid, SIGTERM);
}

static void spawn_wlsunset(const char *gamma) {
    wlsunset_pid = fork();
    if (wlsunset_pid == 0) {
        execlp("wlsunset", "wlsunset", "-T", "6501", "-t", "6500", "-g", gamma, NULL);
        _exit(1);
    }
    atexit(kill_wlsunset);
}

static void restore_hyprland_cursor(void) {
    if (!cursor_hidden)
        return;

    system("hyprctl keyword cursor:invisible false >/dev/null 2>&1");
    cursor_hidden = FALSE;
}

static void hide_hyprland_cursor(void) {
    const char *setting = getenv("CROSSHAIR_HIDE_CURSOR");
    if (setting && (strcmp(setting, "0") == 0 || strcmp(setting, "false") == 0 || strcmp(setting, "no") == 0))
        return;

    if (system("hyprctl keyword cursor:invisible true >/dev/null 2>&1") == 0
        && system("hyprctl getoption cursor:invisible 2>/dev/null | grep -q 'bool: true'") == 0) {
        cursor_hidden = TRUE;
        atexit(restore_hyprland_cursor);
    }
}

static void handle_signal(int sig) {
    restore_hyprland_cursor();
    kill_wlsunset();
    signal(sig, SIG_DFL);
    raise(sig);
}

static gboolean parse_hex_color(const char *text, double *r, double *g, double *b) {
    const char *hex = strchr(text, '#');
    if (!hex)
        return FALSE;

    unsigned int rv = 0;
    unsigned int gv = 0;
    unsigned int bv = 0;
    if (sscanf(hex, "#%02x%02x%02x", &rv, &gv, &bv) != 3)
        return FALSE;

    *r = rv / 255.0;
    *g = gv / 255.0;
    *b = bv / 255.0;
    return TRUE;
}

static gboolean read_color_from_json(const char *path, double *r, double *g, double *b) {
    FILE *fp = fopen(path, "r");
    if (!fp)
        return FALSE;

    char line[512];
    gboolean found = FALSE;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"color1\"") && parse_hex_color(line, r, g, b)) {
            found = TRUE;
            break;
        }
    }

    fclose(fp);
    return found;
}

static gboolean read_color_from_colors(const char *path, double *r, double *g, double *b) {
    FILE *fp = fopen(path, "r");
    if (!fp)
        return FALSE;

    char line[128];
    gboolean found = FALSE;
    for (int i = 0; fgets(line, sizeof(line), fp); i++) {
        if (i == 1 && parse_hex_color(line, r, g, b)) {
            found = TRUE;
            break;
        }
    }

    fclose(fp);
    return found;
}

static void queue_redraw_all(void) {
    if (!surfaces)
        return;

    for (guint i = 0; i < surfaces->len; i++) {
        CrosshairSurface *surface = g_ptr_array_index(surfaces, i);
        gtk_widget_queue_draw(surface->area);
    }
}

static gboolean load_accent_color(void) {
    double next_r = 1.0;
    double next_g = 1.0;
    double next_b = 1.0;

    const char *env_color = getenv("CROSSHAIR_COLOR");
    if (env_color && parse_hex_color(env_color, &next_r, &next_g, &next_b))
        goto apply;

    const char *home = getenv("HOME");
    if (!home)
        return FALSE;

    char path[4096];
    snprintf(path, sizeof(path), "%s/.cache/wal/colors.json", home);
    if (read_color_from_json(path, &next_r, &next_g, &next_b))
        goto apply;

    snprintf(path, sizeof(path), "%s/.cache/wal/colors", home);
    if (!read_color_from_colors(path, &next_r, &next_g, &next_b))
        return FALSE;

apply:
    if (line_color_loaded && next_r == line_r && next_g == line_g && next_b == line_b)
        return FALSE;

    line_r = next_r;
    line_g = next_g;
    line_b = next_b;
    line_color_loaded = TRUE;
    return TRUE;
}

static gboolean reload_color(gpointer _) {
    (void)_;
    color_reload_source = 0;

    if (load_accent_color())
        queue_redraw_all();

    return G_SOURCE_REMOVE;
}

static void schedule_color_reload(void) {
    if (getenv("CROSSHAIR_COLOR"))
        return;

    if (color_reload_source)
        g_source_remove(color_reload_source);

    color_reload_source = g_timeout_add(120, reload_color, NULL);
}

static void on_color_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
                                  GFileMonitorEvent event, gpointer user_data) {
    (void)monitor;
    (void)file;
    (void)other_file;
    (void)user_data;

    if (event == G_FILE_MONITOR_EVENT_CHANGED
        || event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
        || event == G_FILE_MONITOR_EVENT_CREATED
        || event == G_FILE_MONITOR_EVENT_DELETED
        || event == G_FILE_MONITOR_EVENT_MOVED_IN
        || event == G_FILE_MONITOR_EVENT_RENAMED) {
        schedule_color_reload();
    }
}

static GFileMonitor *watch_file(const char *path) {
    GFile *file = g_file_new_for_path(path);
    GError *error = NULL;
    GFileMonitor *monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
    g_object_unref(file);

    if (!monitor) {
        if (error)
            g_error_free(error);
        return NULL;
    }

    g_signal_connect(monitor, "changed", G_CALLBACK(on_color_file_changed), NULL);
    return monitor;
}

static void watch_accent_files(void) {
    if (getenv("CROSSHAIR_COLOR"))
        return;

    const char *home = getenv("HOME");
    if (!home)
        return;

    char path[4096];
    snprintf(path, sizeof(path), "%s/.cache/wal/colors.json", home);
    colors_json_monitor = watch_file(path);

    snprintf(path, sizeof(path), "%s/.cache/wal/colors", home);
    colors_monitor = watch_file(path);
}

static gboolean cursor_inside(const CrosshairSurface *surface, double *local_x, double *local_y) {
    *local_x = cursor_x - surface->geometry.x;
    *local_y = cursor_y - surface->geometry.y;

    return *local_x >= 0
        && *local_y >= 0
        && *local_x < surface->geometry.width
        && *local_y < surface->geometry.height;
}

static void queue_crosshair_damage(CrosshairSurface *surface, double local_x, double local_y) {
    if (!surface->inside)
        return;

    int x = (int)round(local_x);
    int y = (int)round(local_y);
    int pad = DAMAGE_PAD;

    gtk_widget_queue_draw_area(surface->area,
                               x - pad, 0,
                               pad * 2 + 1, surface->geometry.height);
    gtk_widget_queue_draw_area(surface->area,
                               0, y - pad,
                               surface->geometry.width, pad * 2 + 1);
    gtk_widget_queue_draw_area(surface->area,
                               x - CENTER_DAMAGE, y - CENTER_DAMAGE,
                               CENTER_DAMAGE * 2 + 1, CENTER_DAMAGE * 2 + 1);
}

static void update_surface_cursor_state(CrosshairSurface *surface) {
    double local_x = -1.0;
    double local_y = -1.0;
    gboolean inside = cursor_inside(surface, &local_x, &local_y);

    double old_x = surface->local_x;
    double old_y = surface->local_y;
    gboolean old_inside = surface->inside;

    surface->local_x = local_x;
    surface->local_y = local_y;
    surface->inside = inside;

    if (old_inside != inside) {
        gtk_widget_queue_draw(surface->area);
        return;
    }

    if (old_inside) {
        surface->inside = TRUE;
        queue_crosshair_damage(surface, old_x, old_y);
    }

    if (inside)
        queue_crosshair_damage(surface, local_x, local_y);

    surface->inside = inside;
}

static gboolean read_cursor_position(double *x, double *y) {
    FILE *fp = popen("hyprctl cursorpos 2>/dev/null", "r");
    if (!fp)
        return FALSE;

    char line[128] = {0};
    gboolean ok = fgets(line, sizeof(line), fp) != NULL;
    int status = pclose(fp);
    if (!ok || status == -1)
        return FALSE;

    return sscanf(line, " %lf , %lf", x, y) == 2;
}

static gboolean update_cursor(gpointer _) {
    (void)_;

    double next_x = -1.0;
    double next_y = -1.0;
    if (!read_cursor_position(&next_x, &next_y))
        return G_SOURCE_CONTINUE;

    if ((int)next_x == (int)cursor_x && (int)next_y == (int)cursor_y)
        return G_SOURCE_CONTINUE;

    cursor_x = next_x;
    cursor_y = next_y;

    for (guint i = 0; i < surfaces->len; i++) {
        CrosshairSurface *surface = g_ptr_array_index(surfaces, i);
        update_surface_cursor_state(surface);
    }

    return G_SOURCE_CONTINUE;
}

static gboolean update_animation(gpointer _) {
    (void)_;

    if (!surfaces)
        return G_SOURCE_CONTINUE;

    for (guint i = 0; i < surfaces->len; i++) {
        CrosshairSurface *surface = g_ptr_array_index(surfaces, i);
        if (!surface->inside)
            continue;

        int x = (int)round(surface->local_x);
        int y = (int)round(surface->local_y);
        gtk_widget_queue_draw_area(surface->area,
                                   x - CENTER_DAMAGE, y - CENTER_DAMAGE,
                                   CENTER_DAMAGE * 2 + 1, CENTER_DAMAGE * 2 + 1);
    }

    return G_SOURCE_CONTINUE;
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)w;
    CrosshairSurface *surface = data;

    GtkAllocation a;
    gtk_widget_get_allocation(surface->area, &a);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    if (!surface->inside)
        return FALSE;

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_set_source_rgba(cr, line_r, line_g, line_b, 0.42);
    cairo_set_line_width(cr, LINE_WIDTH);
    const double dash[] = {4.0, 8.0};
    cairo_set_dash(cr, dash, 2, 0);

    double x = round(surface->local_x);
    double y = round(surface->local_y);
    double gap = CENTER_GAP;

    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, MAX(0.0, y - gap));
    cairo_move_to(cr, x, MIN((double)a.height, y + gap));
    cairo_line_to(cr, x, a.height);
    cairo_move_to(cr, 0, y);
    cairo_line_to(cr, MAX(0.0, x - gap), y);
    cairo_move_to(cr, MIN((double)a.width, x + gap), y);
    cairo_line_to(cr, a.width, y);
    cairo_stroke(cr);

    gint64 now = g_get_monotonic_time();
    double t = now / 1000000.0;
    double rotation = fmod(t * PI * 0.72, PI * 2.0);

    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_set_dash(cr, NULL, 0, 0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_set_line_width(cr, LINE_WIDTH);
    cairo_set_source_rgba(cr, line_r, line_g, line_b, 0.76);
    for (int i = 0; i < 3; i++) {
        double start = rotation + i * (PI * 2.0 / 3.0);
        cairo_arc(cr, 0, 0, RING_RADIUS, start, start + PI * 0.34);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, LINE_WIDTH);
    cairo_set_source_rgba(cr, line_r, line_g, line_b, 0.48);
    for (int i = 0; i < 4; i++) {
        double angle = rotation * -0.72 + i * (PI / 2.0);
        double sx = cos(angle) * TICK_INNER_RADIUS;
        double sy = sin(angle) * TICK_INNER_RADIUS;
        double ex = cos(angle) * TICK_OUTER_RADIUS;
        double ey = sin(angle) * TICK_OUTER_RADIUS;
        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, LINE_WIDTH);
    cairo_set_source_rgba(cr, line_r, line_g, line_b, 0.28);
    cairo_arc(cr, 0, 0, RING_RADIUS + 5.0, 0, PI * 2.0);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, line_r, line_g, line_b, 0.95);
    cairo_arc(cr, 0, 0, DOT_RADIUS, 0, PI * 2.0);
    cairo_fill(cr);

    cairo_restore(cr);

    return FALSE;
}

static void make_surface(GtkApplication *app, GdkMonitor *monitor) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(app, GTK_WINDOW(win));
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);

    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(win));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual)
        gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);

    CrosshairSurface *surface = g_new0(CrosshairSurface, 1);
    gdk_monitor_get_geometry(monitor, &surface->geometry);
    surface->local_x = -1.0;
    surface->local_y = -1.0;
    surface->inside = FALSE;

    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_monitor(GTK_WINDOW(win), monitor);
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), -1);

    GtkWidget *area = gtk_drawing_area_new();
    surface->area = area;
    g_signal_connect(area, "draw", G_CALLBACK(on_draw), surface);
    gtk_container_add(GTK_CONTAINER(win), area);
    g_ptr_array_add(surfaces, surface);

    gtk_widget_show_all(win);

    cairo_region_t *empty = cairo_region_create();
    gdk_window_input_shape_combine_region(gtk_widget_get_window(win), empty, 0, 0);
    cairo_region_destroy(empty);
}

static void activate(GtkApplication *app, gpointer _) {
    (void)_;

    surfaces = g_ptr_array_new_with_free_func(g_free);

    GdkDisplay *display = gdk_display_get_default();
    int monitor_count = gdk_display_get_n_monitors(display);
    for (int i = 0; i < monitor_count; i++)
        make_surface(app, gdk_display_get_monitor(display, i));

    update_cursor(NULL);
    g_timeout_add(UPDATE_MS, update_cursor, NULL);
    g_timeout_add(ANIMATION_MS, update_animation, NULL);
    watch_accent_files();
}

int main(int argc, char **argv) {
    load_accent_color();
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    hide_hyprland_cursor();

    if (argc >= 2)
        spawn_wlsunset(argv[1]);

    GtkApplication *app = gtk_application_new("se.n1k0.crosshair", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_clear_object(&colors_json_monitor);
    g_clear_object(&colors_monitor);
    g_object_unref(app);
    return status;
}
