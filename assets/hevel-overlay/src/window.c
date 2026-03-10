#include "window.h"
#include "hevel.h"
#include "input.h"
#include "scroll.h"
#include "zoom.h"

static pid_t
get_parent_pid(pid_t pid);
static struct window *
find_window_by_pid(pid_t pid);
static bool
is_terminal_window(struct window *w);
static void
mk_spawn_link(struct window *terminal, struct window *child);

void
focus_window(struct swc_window *swc, const char *reason)
{
  const char *from = compositor.focused && compositor.focused->title
                         ? compositor.focused->title
                         : "";
  const char *to = swc && swc->title ? swc->title : "";

  if (compositor.focused == swc) return;
  printf("focus %p ('%s') -> %p ('%s') (%s)\n", (void *)compositor.focused,
         from, (void *)swc, to, reason);

  if (compositor.focused)
    swc_window_set_border(compositor.focused, inner_border_color_inactive,
                          inner_border_width, outer_border_color_inactive,
                          outer_border_width);

  swc_window_focus(swc);

  /* zoom to default size when focusing a window */
  if (enable_zoom && swc && swc_get_zoom() != 1.0f) {
    zoom.target = 1.0f;
    if (!zoom.timer)
      zoom.timer = wl_event_loop_add_timer(compositor.evloop, zoom_tick, NULL);
    if (zoom.timer) wl_event_source_timer_update(zoom.timer, 1);
  }

  if (swc)
    swc_window_set_border(swc, inner_border_color_active, inner_border_width,
                          outer_border_color_active, outer_border_width);

  compositor.focused = swc;

  /* center the focused window: both axes in drag mode, vertical only in scroll
   * wheel mode, only when visible or jumping to it, else you can center
   * offscreen windows */
  if (focus_center == true && swc && compositor.current_screen &&
      (is_visible(compositor.focused, compositor.current_screen) ||
       chord.mode == MODE_JUMP)) {
    struct swc_rectangle window_geom;

    if (swc_window_get_geometry(swc, &window_geom)) {
      /* skip if window has no size yet (not configured by client) */
      if (window_geom.width == 0 || window_geom.height == 0) return;

      int32_t window_center_x = window_geom.x + (int32_t)window_geom.width / 2;
      int32_t window_center_y = window_geom.y + (int32_t)window_geom.height / 2;
      int32_t screen_center_x =
          compositor.current_screen->swc->geometry.x +
          (int32_t)compositor.current_screen->swc->geometry.width / 2;
      int32_t screen_center_y =
          compositor.current_screen->swc->geometry.y +
          (int32_t)compositor.current_screen->swc->geometry.height / 2;

      /* in drag mode: center on both axes; in scroll wheel mode: vertical only
       */
      int32_t scroll_delta_x =
          scroll_drag_mode ? (screen_center_x - window_center_x) : 0;
      int32_t scroll_delta_y = screen_center_y - window_center_y;

      if (scroll_delta_x != 0 || scroll_delta_y != 0) {
        /* stop scroll before auto-scroll */
        scroll_stop();

        scroll.pending_px = scroll_delta_y;
        scroll.pending_px_x = scroll_delta_x;
        scroll.rem = 0;
        scroll.rem_x = 0;
        scroll.auto_scrolling = true;

        if (!scroll.timer) {
          scroll.timer =
              wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
        }
        wl_event_source_timer_update(scroll.timer, timerms);
      }
    }
  }
}

bool
is_visible(struct swc_window *w, struct screen *screen)
{
  struct swc_rectangle *geom = &screen->swc->geometry;
  struct swc_rectangle wgeom;
  swc_window_get_geometry(w, &wgeom);

  bool h = wgeom.x + (int32_t)wgeom.width > geom->x &&
           wgeom.x < geom->x + (int32_t)geom->width;
  bool v = wgeom.y + (int32_t)wgeom.height > geom->y &&
           wgeom.y < geom->y + (int32_t)geom->height;

  return h && v;
}

/* hacky sorta, only works for vertical cuz of this */
bool
is_on_screen(struct swc_rectangle *window, struct screen *screen)
{
  struct swc_rectangle *geom = &screen->swc->geometry;

  return window->x + (int32_t)window->width > geom->x &&
         window->x < geom->x + (int32_t)geom->width;
}

bool
is_acme(const struct swc_window *swc)
{
  return swc && swc->app_id && strcmp(swc->app_id, "acme") == 0;
}

void
windowdestroy(void *data)
{
  struct window *w = data;

  /* cleanup for term spawn*/
  if (w->spawn_parent) {
    struct window *terminal = w->spawn_parent;

    wl_list_remove(&w->spawn_link);

    if (wl_list_empty(&terminal->spawn_children) &&
        terminal->hidden_for_spawn) {
      /* restore term */
      swc_window_show(terminal->swc);
      swc_window_set_geometry(terminal->swc, &terminal->saved_geometry);
      terminal->hidden_for_spawn = false;

      /* focus terminal */
      focus_window(terminal->swc, "spawn_child_destroyed");
    }
  }

  if (!wl_list_empty(&w->spawn_children)) {
    struct window *child, *tmp;
    wl_list_for_each_safe(child, tmp, &w->spawn_children, spawn_link)
    {
      child->spawn_parent = NULL;
      wl_list_remove(&child->spawn_link);
      wl_list_init(&child->spawn_link);
    }
  }

  if (compositor.focused == w->swc) focus_window(NULL, "destroy");
  wl_list_remove(&w->link);
  free(w);
}

static void
windowappidchanged(void *data)
{
  struct window *w = data;
  struct swc_rectangle geometry;
  bool is_select = input.spawn_pending && w->swc->app_id &&
                   strcmp(w->swc->app_id, select_term_app_id) == 0;

  if (!is_select) return;

  geometry = input.spawn_geometry;
  if (geometry.width < 50) geometry.width = 50;
  if (geometry.height < 50) geometry.height = 50;
  swc_window_set_geometry(w->swc, &geometry);
  input.spawn_pending = false;
}

static const struct swc_window_handler windowhandler = {
    .destroy = windowdestroy,
    .app_id_changed = windowappidchanged,
};

void
newwindow(struct swc_window *swc)
{
  struct window *w;
  struct swc_rectangle geometry;
  bool is_select = input.spawn_pending && swc->app_id &&
                   strcmp(swc->app_id, select_term_app_id) == 0;

  w = malloc(sizeof(*w));
  if (!w) return;
  w->swc = swc;
  w->pid = 0;
  w->spawn_parent = NULL;
  wl_list_init(&w->spawn_children);
  wl_list_init(&w->spawn_link);
  w->hidden_for_spawn = false;
  w->sticky = false;

  wl_list_insert(&compositor.windows, &w->link);
  swc_window_set_handler(swc, &windowhandler, w);
  swc_window_set_stacked(swc);
  swc_window_set_border(swc, inner_border_color_inactive, inner_border_width,
                        outer_border_color_inactive, outer_border_width);

  /* get pid and check conf for term spawn */
  if (enable_terminal_spawning) {
    w->pid = swc_window_get_pid(swc);

    if (w->pid > 0) {
      /* im so fucking dumb, we need to walk up the proc tree to get the term,
       * otherwise we just get the shell */
      pid_t current_pid = w->pid;
      struct window *terminal = NULL;
      int depth = 0;

      /* walk up 10 levels */
      while (depth < 10 && current_pid > 1) {
        pid_t parent_pid = get_parent_pid(current_pid);
        if (parent_pid <= 1) break;

        /* check pid against term*/
        struct window *candidate = find_window_by_pid(parent_pid);
        if (candidate && is_terminal_window(candidate)) {
          terminal = candidate;
          break;
        }

        current_pid = parent_pid;
        depth++;
      }

      if (terminal) mk_spawn_link(terminal, w);
    }
  }

  if (is_select) {
    geometry = input.spawn_geometry;
    if (geometry.width < 50) geometry.width = 50;
    if (geometry.height < 50) geometry.height = 50;
    swc_window_set_geometry(swc, &geometry);
    input.spawn_pending = false;
  }
  swc_window_show(swc);
  printf("window '%s'\n", swc->title ? swc->title : "");
  focus_window(swc, "new_window");
}

void
screendestroy(void *data)
{
  struct screen *s = data;
  wl_list_remove(&s->link);
  inputcapture_refresh_zones();
  free(s);
}

static const struct swc_screen_handler screenhandler = {
    .destroy = screendestroy,
};

void
newscreen(struct swc_screen *swc)
{
  struct screen *s;

  s = malloc(sizeof(*s));
  if (!s) return;
  s->swc = swc;
  wl_list_insert(&compositor.screens, &s->link);
  swc_screen_set_handler(swc, &screenhandler, s);
  printf("screen %dx%d\n", swc->geometry.width, swc->geometry.height);

  inputcapture_refresh_zones();

  if (!input.cursor_timer)
    input.cursor_timer =
        wl_event_loop_add_timer(compositor.evloop, cursor_tick, NULL);
  if (input.cursor_timer)
    wl_event_source_timer_update(input.cursor_timer, timerms);
}

/* helpers for pid*/
static pid_t
get_parent_pid(pid_t pid)
{
  char path[64];
  FILE *f;
  pid_t parent_pid = 0;

  snprintf(path, sizeof(path), "/proc/%d/stat", pid);
  f = fopen(path, "r");
  if (!f) return 0;

  /* its like: pid (comm) state ppid ... */
  fscanf(f, "%*d %*s %*c %d", &parent_pid);
  fclose(f);
  return parent_pid;
}

static struct window *
find_window_by_pid(pid_t pid)
{
  struct window *w;

  wl_list_for_each(w, &compositor.windows, link)
  {
    if (w->pid == pid) return w;
  }
  return NULL;
}

static bool
is_terminal_window(struct window *w)
{
  if (!w || !w->swc) return false;

  /* check app_id */
  if (w->swc->app_id) {
    for (const char *const *term = terminal_app_ids; *term; term++) {
      if (strstr(w->swc->app_id, *term)) return true;
    }
  }

  /* check title too, because, paranoia */
  if (w->swc->title) {
    for (const char *const *term = terminal_app_ids; *term; term++) {
      if (strstr(w->swc->title, *term)) return true;
    }
  }

  return false;
}

static void
mk_spawn_link(struct window *terminal, struct window *child)
{
  child->spawn_parent = terminal;
  wl_list_insert(&terminal->spawn_children, &child->spawn_link);

  /* save term geom */
  if (swc_window_get_geometry(terminal->swc, &terminal->saved_geometry)) {
    terminal->hidden_for_spawn = true;
    swc_window_hide(terminal->swc);
    swc_window_set_geometry(child->swc, &terminal->saved_geometry);
  }
}
