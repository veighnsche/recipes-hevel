#include "hevel.h"

struct compositor_state compositor = {0};
struct input_state input = {0};
struct chord_state chord = {0};
struct scroll_state scroll = {0};
struct zoom_state zoom = {0};
struct sel_state sel = {0};
bool focus_center = FOCUS_CENTER;

#include "input.c"
#include "scroll.c"
#include "select.c"
#include "window.c"
#include "zoom.c"
#include "eis.c"
#include "inject.c"
#include "inputcapture.c"
#include "remotedesktop.c"
#include "portal.c"

static void
maybe_enable_nein_cursor_theme(void)
{
  const struct nein_cursor_meta *arrow =
      &nein_cursor_metadata[NEIN_CURSOR_WHITEARROW];
  const struct nein_cursor_meta *box =
      &nein_cursor_metadata[NEIN_CURSOR_BOXCURSOR];
  const struct nein_cursor_meta *cross =
      &nein_cursor_metadata[NEIN_CURSOR_CROSSCURSOR];
  const struct nein_cursor_meta *sight =
      &nein_cursor_metadata[NEIN_CURSOR_SIGHTCURSOR];
  const struct nein_cursor_meta *up = &nein_cursor_metadata[NEIN_CURSOR_T];
  const struct nein_cursor_meta *down = &nein_cursor_metadata[NEIN_CURSOR_B];

  if (!cursor_theme || strcmp(cursor_theme, "nein") != 0) return;

  swc_set_cursor_mode(SWC_CURSOR_MODE_COMPOSITOR);
  swc_set_cursor_image(SWC_CURSOR_DEFAULT, &nein_cursor_data[arrow->offset],
                       arrow->width, arrow->height, arrow->hotspot_x,
                       arrow->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_BOX, &nein_cursor_data[box->offset],
                       box->width, box->height, box->hotspot_x, box->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_CROSS, &nein_cursor_data[cross->offset],
                       cross->width, cross->height, cross->hotspot_x,
                       cross->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_SIGHT, &nein_cursor_data[sight->offset],
                       sight->width, sight->height, sight->hotspot_x,
                       sight->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_UP, &nein_cursor_data[up->offset], up->width,
                       up->height, up->hotspot_x, up->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_DOWN, &nein_cursor_data[down->offset],
                       down->width, down->height, down->hotspot_x,
                       down->hotspot_y);

  update_mode_cursor();
}

static void
newdevice(struct libinput_device *dev)
{
  (void)dev;
}

static const struct swc_manager manager = {
    .new_screen = newscreen,
    .new_window = newwindow,
    .new_device = newdevice,
};

static void
quit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  (void)data;
  (void)time;
  (void)value;
  (void)state;
  wl_display_terminate(compositor.display);
}

static void
sig(int s)
{
  (void)s;
  wl_display_terminate(compositor.display);
}

int
main(int argc, char **argv)
{
  struct wl_event_loop *evloop;
  const char *sock;

  if (argc > 1) {
    if (strcmp(argv[1], "--portal-service") == 0) return portal_service_main();
    if (strcmp(argv[1], "--inject") == 0)
      return inject_cli_main(argc - 2, argv + 2);
    fprintf(stderr, "usage: %s [--portal-service | --inject <command> ...]\n",
            argv[0]);
    return 2;
  }

  wl_list_init(&compositor.windows);
  wl_list_init(&compositor.screens);

  compositor.current_screen = NULL;
  compositor.display = wl_display_create();
  if (!compositor.display) {
    fprintf(stderr, "cannot create display\n");
    return 1;
  }

  evloop = wl_display_get_event_loop(compositor.display);
  compositor.evloop = evloop;

  if (!swc_initialize(compositor.display, evloop, &manager)) {
    fprintf(stderr, "cannot initialize swc\n");
    return 1;
  }

  maybe_enable_nein_cursor_theme();

  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT, XKB_KEY_q,
                  quit, NULL);

  /* we can bind mouse buttons using SWC_MOD_ANY */
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_LEFT, button, NULL);
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_MIDDLE, button, NULL);
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_RIGHT, button, NULL);
  if (swc_add_axis_binding(SWC_MOD_ANY, 0, axis, NULL) < 0)
    fprintf(stderr, "cannot bind vertical scroll axis\n");
  if (swc_add_axis_binding(SWC_MOD_ANY, 1, axis, NULL) < 0)
    fprintf(stderr, "cannot bind horizontal scroll axis\n");

  sock = wl_display_add_socket_auto(compositor.display);
  if (!sock) {
    fprintf(stderr, "cannot add socket\n");
    return 1;
  }

  printf("%s\n", sock);
  setenv("WAYLAND_DISPLAY", sock, 1);

  if (inject_initialize(sock) < 0)
    fprintf(stderr, "hevel: continuing without local injection hook\n");

  signal(SIGTERM, sig);
  signal(SIGINT, sig);

  wl_display_run(compositor.display);

  inject_finalize();
  swc_finalize();
  wl_display_destroy(compositor.display);

  return 0;
}
