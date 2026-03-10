#include "hevel.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include <libeis.h>

#define HEVEL_EIS_CAPABILITIES                                                  \
  (EIS_DEVICE_CAP_POINTER | EIS_DEVICE_CAP_POINTER_ABSOLUTE |                   \
   EIS_DEVICE_CAP_KEYBOARD | EIS_DEVICE_CAP_BUTTON | EIS_DEVICE_CAP_SCROLL)
#define HEVEL_EIS_DEFAULT_NAME "hevel"

struct eis_state eis_state = {0};

static const char *
eis_event_name(enum eis_event_type type)
{
  switch (type) {
    case EIS_EVENT_CLIENT_CONNECT:
      return "client-connect";
    case EIS_EVENT_CLIENT_DISCONNECT:
      return "client-disconnect";
    case EIS_EVENT_SEAT_BIND:
      return "seat-bind";
    case EIS_EVENT_DEVICE_CLOSED:
      return "device-closed";
    case EIS_EVENT_FRAME:
      return "frame";
    case EIS_EVENT_DEVICE_START_EMULATING:
      return "device-start-emulating";
    case EIS_EVENT_DEVICE_STOP_EMULATING:
      return "device-stop-emulating";
    case EIS_EVENT_POINTER_MOTION:
      return "pointer-motion";
    case EIS_EVENT_POINTER_MOTION_ABSOLUTE:
      return "pointer-motion-absolute";
    case EIS_EVENT_BUTTON_BUTTON:
      return "button";
    case EIS_EVENT_SCROLL_DELTA:
      return "scroll-delta";
    case EIS_EVENT_SCROLL_STOP:
      return "scroll-stop";
    case EIS_EVENT_SCROLL_CANCEL:
      return "scroll-cancel";
    case EIS_EVENT_SCROLL_DISCRETE:
      return "scroll-discrete";
    case EIS_EVENT_KEYBOARD_KEY:
      return "keyboard-key";
    default:
      return "unknown";
  }
}

static bool
eis_capability_enabled(uint32_t mask, enum eis_device_capability capability)
{
  return (mask & (uint32_t)capability) != 0;
}

static uint32_t
eis_event_time_ms(struct eis_event *event)
{
  uint64_t usec = eis_event_get_time(event);
  uint64_t ms = usec / 1000u;

  if (ms > UINT32_MAX) return UINT32_MAX;
  return (uint32_t)ms;
}

static int32_t
eis_fixed_from_double(double value)
{
  double scaled = value * 256.0;

  if (scaled > (double)INT32_MAX) return INT32_MAX;
  if (scaled < (double)INT32_MIN) return INT32_MIN;

  return scaled >= 0.0 ? (int32_t)(scaled + 0.5) : (int32_t)(scaled - 0.5);
}

static struct hevel_eis_client *
eis_client_from_event(struct eis_event *event)
{
  struct eis_client *client = eis_event_get_client(event);
  struct eis_device *device;

  if (client) return eis_client_get_user_data(client);

  device = eis_event_get_device(event);
  if (!device) return NULL;

  client = eis_device_get_client(device);
  return client ? eis_client_get_user_data(client) : NULL;
}

static void
eis_request_free(struct hevel_eis_request *request)
{
  if (!request) return;
  free(request);
}

static bool
eis_request_matches_session(const struct hevel_eis_request *request,
                            const char *session_id)
{
  if (!request || !session_id || session_id[0] == '\0') return false;
  return strcmp(request->session_id, session_id) == 0;
}

static void
eis_release_device(struct eis_device **device)
{
  if (!device || !*device) return;
  eis_device_remove(*device);
  *device = eis_device_unref(*device);
}

static void
eis_cleanup_client(struct hevel_eis_client *client_state)
{
  if (!client_state) return;

  wl_list_remove(&client_state->link);

  if (client_state->client) eis_client_set_user_data(client_state->client, NULL);

  eis_release_device(&client_state->keyboard);
  eis_release_device(&client_state->pointer);
  eis_release_device(&client_state->pointer_abs);

  if (client_state->seat) {
    eis_seat_remove(client_state->seat);
    client_state->seat = eis_seat_unref(client_state->seat);
  }

  if (client_state->client)
    client_state->client = eis_client_unref(client_state->client);

  eis_request_free(client_state->request);
  free(client_state);
}

static struct hevel_eis_request *
eis_pop_request(void)
{
  struct hevel_eis_request *request;

  if (wl_list_empty(&eis_state.requests)) return NULL;

  request = wl_container_of(eis_state.requests.next, request, link);
  wl_list_remove(&request->link);
  wl_list_init(&request->link);
  return request;
}

static struct hevel_eis_request *
eis_new_request(const char *session_id, uint32_t capabilities, const char *name)
{
  struct hevel_eis_request *request = calloc(1, sizeof(*request));

  if (!request) return NULL;

  wl_list_init(&request->link);
  request->id = ++eis_state.next_request_id;
  request->capabilities = capabilities ? capabilities : HEVEL_EIS_CAPABILITIES;

  snprintf(request->session_id, sizeof(request->session_id), "%s",
           session_id && session_id[0] != '\0' ? session_id : "");
  snprintf(request->name, sizeof(request->name), "%s",
           name && name[0] != '\0' ? name : HEVEL_EIS_DEFAULT_NAME);

  return request;
}

static bool
eis_compute_absolute_origin(int32_t *origin_x, int32_t *origin_y)
{
  struct screen *screen;
  bool have_screen = false;
  int32_t min_x = 0, min_y = 0;

  wl_list_for_each(screen, &compositor.screens, link)
  {
    if (!screen->swc) continue;
    if (!have_screen) {
      min_x = screen->swc->geometry.x;
      min_y = screen->swc->geometry.y;
      have_screen = true;
      continue;
    }

    if (screen->swc->geometry.x < min_x) min_x = screen->swc->geometry.x;
    if (screen->swc->geometry.y < min_y) min_y = screen->swc->geometry.y;
  }

  if (!have_screen) {
    *origin_x = 0;
    *origin_y = 0;
    return false;
  }

  *origin_x = min_x;
  *origin_y = min_y;
  return true;
}

static bool
eis_add_absolute_regions(struct eis_device *device,
                         struct hevel_eis_client *client_state)
{
  struct screen *screen;
  bool have_screen = false;

  if (!eis_compute_absolute_origin(&client_state->absolute_origin_x,
                                   &client_state->absolute_origin_y))
    return false;

  wl_list_for_each(screen, &compositor.screens, link)
  {
    struct eis_region *region;
    int32_t region_x, region_y;

    if (!screen->swc) continue;

    region = eis_device_new_region(device);
    if (!region) continue;

    region_x = screen->swc->geometry.x - client_state->absolute_origin_x;
    region_y = screen->swc->geometry.y - client_state->absolute_origin_y;

    eis_region_set_offset(region, (uint32_t)region_x, (uint32_t)region_y);
    eis_region_set_size(region, screen->swc->geometry.width,
                        screen->swc->geometry.height);
    eis_region_add(region);
    eis_region_unref(region);
    have_screen = true;
  }

  return have_screen;
}

static struct eis_device *
eis_create_keyboard_device(struct hevel_eis_client *client_state)
{
  struct eis_device *device = eis_seat_new_device(client_state->seat);

  if (!device) return NULL;

  eis_device_configure_name(device, "hevel keyboard");
  eis_device_configure_capability(device, EIS_DEVICE_CAP_KEYBOARD);
  eis_device_add(device);
  eis_device_resume(device);
  return device;
}

static struct eis_device *
eis_create_pointer_device(struct hevel_eis_client *client_state,
                          bool want_pointer, bool want_button,
                          bool want_scroll)
{
  struct eis_device *device;

  if (!want_pointer && !want_button && !want_scroll) return NULL;

  device = eis_seat_new_device(client_state->seat);
  if (!device) return NULL;

  eis_device_configure_name(device, "hevel pointer");
  if (want_pointer) eis_device_configure_capability(device, EIS_DEVICE_CAP_POINTER);
  if (want_button) eis_device_configure_capability(device, EIS_DEVICE_CAP_BUTTON);
  if (want_scroll) eis_device_configure_capability(device, EIS_DEVICE_CAP_SCROLL);
  eis_device_add(device);
  eis_device_resume(device);
  return device;
}

static struct eis_device *
eis_create_absolute_device(struct hevel_eis_client *client_state)
{
  struct eis_device *device = eis_seat_new_device(client_state->seat);

  if (!device) return NULL;

  eis_device_configure_name(device, "hevel absolute pointer");
  eis_device_configure_capability(device, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
  if (!eis_add_absolute_regions(device, client_state)) {
    fprintf(stderr, "hevel: cannot export absolute EIS regions without screens\n");
    eis_device_unref(device);
    return NULL;
  }

  eis_device_add(device);
  eis_device_resume(device);
  return device;
}

static void
eis_sync_bound_devices(struct hevel_eis_client *client_state,
                       struct eis_event *event)
{
  uint32_t capabilities = client_state->request->capabilities;
  bool want_keyboard =
      eis_capability_enabled(capabilities, EIS_DEVICE_CAP_KEYBOARD) &&
      eis_event_seat_has_capability(event, EIS_DEVICE_CAP_KEYBOARD);
  bool want_pointer =
      (eis_capability_enabled(capabilities, EIS_DEVICE_CAP_POINTER) &&
       eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER)) ||
      (eis_capability_enabled(capabilities, EIS_DEVICE_CAP_BUTTON) &&
       eis_event_seat_has_capability(event, EIS_DEVICE_CAP_BUTTON)) ||
      (eis_capability_enabled(capabilities, EIS_DEVICE_CAP_SCROLL) &&
       eis_event_seat_has_capability(event, EIS_DEVICE_CAP_SCROLL));
  bool want_relative_pointer =
      eis_capability_enabled(capabilities, EIS_DEVICE_CAP_POINTER) &&
      eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER);
  bool want_button =
      eis_capability_enabled(capabilities, EIS_DEVICE_CAP_BUTTON) &&
      eis_event_seat_has_capability(event, EIS_DEVICE_CAP_BUTTON);
  bool want_scroll =
      eis_capability_enabled(capabilities, EIS_DEVICE_CAP_SCROLL) &&
      eis_event_seat_has_capability(event, EIS_DEVICE_CAP_SCROLL);
  bool want_absolute =
      eis_capability_enabled(capabilities, EIS_DEVICE_CAP_POINTER_ABSOLUTE) &&
      eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER_ABSOLUTE);

  if (want_keyboard && !client_state->keyboard)
    client_state->keyboard = eis_create_keyboard_device(client_state);
  if (!want_keyboard && client_state->keyboard) {
    client_state->keyboard_active = false;
    eis_release_device(&client_state->keyboard);
  }

  if (want_pointer && !client_state->pointer)
    client_state->pointer = eis_create_pointer_device(
        client_state, want_relative_pointer, want_button, want_scroll);
  if (!want_pointer && client_state->pointer) {
    client_state->pointer_active = false;
    eis_release_device(&client_state->pointer);
  }

  if (want_absolute && !client_state->pointer_abs)
    client_state->pointer_abs = eis_create_absolute_device(client_state);
  if (!want_absolute && client_state->pointer_abs) {
    client_state->pointer_abs_active = false;
    eis_release_device(&client_state->pointer_abs);
  }
}

static void
eis_handle_client_connect(struct eis_event *event)
{
  struct eis_client *client = eis_event_get_client(event);
  struct hevel_eis_request *request = NULL;
  struct hevel_eis_client *client_state = NULL;

  if (!client) return;

  if (!eis_client_is_sender(client)) {
    fprintf(stderr, "hevel: rejecting non-sender EIS client %s\n",
            eis_client_get_name(client));
    eis_client_disconnect(client);
    return;
  }

  request = eis_pop_request();
  if (!request) {
    fprintf(stderr, "hevel: rejecting unexpected EIS sender client %s\n",
            eis_client_get_name(client));
    eis_client_disconnect(client);
    return;
  }

  client_state = calloc(1, sizeof(*client_state));
  if (!client_state) {
    eis_request_free(request);
    eis_client_disconnect(client);
    return;
  }

  client_state->client = eis_client_ref(client);
  client_state->request = request;
  wl_list_insert(eis_state.clients.prev, &client_state->link);
  eis_client_set_user_data(client, client_state);

  eis_client_connect(client);

  client_state->seat = eis_client_new_seat(client, request->name);
  if (!client_state->seat) {
    fprintf(stderr, "hevel: cannot create EIS seat for %s\n",
            eis_client_get_name(client));
    eis_client_disconnect(client);
    eis_cleanup_client(client_state);
    return;
  }

  if (eis_capability_enabled(request->capabilities, EIS_DEVICE_CAP_POINTER))
    eis_seat_configure_capability(client_state->seat, EIS_DEVICE_CAP_POINTER);
  if (eis_capability_enabled(request->capabilities,
                             EIS_DEVICE_CAP_POINTER_ABSOLUTE))
    eis_seat_configure_capability(client_state->seat,
                                  EIS_DEVICE_CAP_POINTER_ABSOLUTE);
  if (eis_capability_enabled(request->capabilities, EIS_DEVICE_CAP_KEYBOARD))
    eis_seat_configure_capability(client_state->seat, EIS_DEVICE_CAP_KEYBOARD);
  if (eis_capability_enabled(request->capabilities, EIS_DEVICE_CAP_BUTTON))
    eis_seat_configure_capability(client_state->seat, EIS_DEVICE_CAP_BUTTON);
  if (eis_capability_enabled(request->capabilities, EIS_DEVICE_CAP_SCROLL))
    eis_seat_configure_capability(client_state->seat, EIS_DEVICE_CAP_SCROLL);

  eis_seat_add(client_state->seat);

  fprintf(stderr, "hevel: accepted EIS sender %s for %s%s%s\n",
          eis_client_get_name(client), request->name,
          request->session_id[0] != '\0' ? " session " : "",
          request->session_id[0] != '\0' ? request->session_id : "");
}

static void
eis_handle_client_disconnect(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  struct eis_client *client = eis_event_get_client(event);

  fprintf(stderr, "hevel: EIS client disconnected: %s\n",
          client ? eis_client_get_name(client) : "(unknown)");

  if (client_state) eis_cleanup_client(client_state);
}

static void
eis_handle_device_closed(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  struct eis_device *device = eis_event_get_device(event);

  if (!client_state || !device) return;

  if (client_state->keyboard == device) {
    client_state->keyboard_active = false;
    eis_release_device(&client_state->keyboard);
    return;
  }
  if (client_state->pointer == device) {
    client_state->pointer_active = false;
    eis_release_device(&client_state->pointer);
    return;
  }
  if (client_state->pointer_abs == device) {
    client_state->pointer_abs_active = false;
    eis_release_device(&client_state->pointer_abs);
  }
}

static void
eis_handle_emulation_state(struct eis_event *event, bool active)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  struct eis_device *device = eis_event_get_device(event);

  if (!client_state || !device) return;

  if (client_state->keyboard == device) client_state->keyboard_active = active;
  if (client_state->pointer == device) client_state->pointer_active = active;
  if (client_state->pointer_abs == device) client_state->pointer_abs_active = active;
}

static void
eis_log_forward_failure(const char *what)
{
  fprintf(stderr, "hevel: failed to forward %s into SWC\n", what);
}

static void
eis_forward_key(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  uint32_t key, state, time;

  if (!client_state || !client_state->keyboard) return;

  key = eis_event_keyboard_get_key(event);
  state = eis_event_keyboard_get_key_is_press(event)
              ? WL_KEYBOARD_KEY_STATE_PRESSED
              : WL_KEYBOARD_KEY_STATE_RELEASED;
  time = eis_event_time_ms(event);

  fprintf(stderr, "hevel: EIS key %u %s\n", key,
          state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release");
  if (!swc_keyboard_inject_key(time, key, state))
    eis_log_forward_failure("keyboard event");
}

static void
eis_forward_relative_motion(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  int32_t dx, dy;
  uint32_t time;

  if (!client_state || !client_state->pointer) return;

  dx = eis_fixed_from_double(eis_event_pointer_get_dx(event));
  dy = eis_fixed_from_double(eis_event_pointer_get_dy(event));
  time = eis_event_time_ms(event);

  fprintf(stderr, "hevel: EIS motion %.2f %.2f\n",
          eis_event_pointer_get_dx(event), eis_event_pointer_get_dy(event));
  if (!swc_pointer_inject_relative_motion(time, dx, dy))
    eis_log_forward_failure("relative motion");
}

static void
eis_forward_absolute_motion(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  int32_t x, y;
  uint32_t time;
  double raw_x, raw_y;

  if (!client_state || !client_state->pointer_abs) return;

  raw_x = eis_event_pointer_get_absolute_x(event) + client_state->absolute_origin_x;
  raw_y = eis_event_pointer_get_absolute_y(event) + client_state->absolute_origin_y;
  x = eis_fixed_from_double(raw_x);
  y = eis_fixed_from_double(raw_y);
  time = eis_event_time_ms(event);

  fprintf(stderr, "hevel: EIS absolute motion %.2f %.2f\n", raw_x, raw_y);
  if (!swc_pointer_inject_absolute_motion(time, x, y))
    eis_log_forward_failure("absolute motion");
}

static void
eis_forward_button(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  uint32_t button = eis_event_button_get_button(event);
  uint32_t state = eis_event_button_get_is_press(event)
                       ? WL_POINTER_BUTTON_STATE_PRESSED
                       : WL_POINTER_BUTTON_STATE_RELEASED;
  uint32_t time = eis_event_time_ms(event);

  if (!client_state || (!client_state->pointer && !client_state->pointer_abs))
    return;

  fprintf(stderr, "hevel: EIS button %u %s\n", button,
          state == WL_POINTER_BUTTON_STATE_PRESSED ? "press" : "release");
  if (!swc_pointer_inject_button(time, button, state))
    eis_log_forward_failure("button event");
}

static void
eis_forward_scroll_delta(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  double dx = eis_event_scroll_get_dx(event);
  double dy = eis_event_scroll_get_dy(event);
  uint32_t time = eis_event_time_ms(event);

  if (!client_state || (!client_state->pointer && !client_state->pointer_abs))
    return;

  fprintf(stderr, "hevel: EIS scroll delta %.2f %.2f\n", dx, dy);

  if (dy != 0.0 &&
      !swc_pointer_inject_axis(time, WL_POINTER_AXIS_VERTICAL_SCROLL,
                               WL_POINTER_AXIS_SOURCE_CONTINUOUS,
                               eis_fixed_from_double(dy), 0))
    eis_log_forward_failure("vertical scroll delta");
  if (dx != 0.0 &&
      !swc_pointer_inject_axis(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                               WL_POINTER_AXIS_SOURCE_CONTINUOUS,
                               eis_fixed_from_double(dx), 0))
    eis_log_forward_failure("horizontal scroll delta");
}

static void
eis_forward_scroll_discrete(struct eis_event *event)
{
  struct hevel_eis_client *client_state = eis_client_from_event(event);
  int32_t dx = eis_event_scroll_get_discrete_dx(event);
  int32_t dy = eis_event_scroll_get_discrete_dy(event);
  uint32_t time = eis_event_time_ms(event);

  if (!client_state || (!client_state->pointer && !client_state->pointer_abs))
    return;

  fprintf(stderr, "hevel: EIS scroll discrete %d %d\n", dx, dy);

  if (dy != 0 &&
      !swc_pointer_inject_axis(time, WL_POINTER_AXIS_VERTICAL_SCROLL,
                               WL_POINTER_AXIS_SOURCE_WHEEL,
                               eis_fixed_from_double((double)dy / 120.0), dy))
    eis_log_forward_failure("vertical discrete scroll");
  if (dx != 0 &&
      !swc_pointer_inject_axis(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                               WL_POINTER_AXIS_SOURCE_WHEEL,
                               eis_fixed_from_double((double)dx / 120.0), dx))
    eis_log_forward_failure("horizontal discrete scroll");
}

static void
eis_forward_frame(struct eis_event *event)
{
  (void)event;
  fprintf(stderr, "hevel: EIS frame\n");
  if (!swc_pointer_inject_frame()) eis_log_forward_failure("frame");
}

static void
eis_handle_event(struct eis_event *event)
{
  switch (eis_event_get_type(event)) {
    case EIS_EVENT_CLIENT_CONNECT:
      eis_handle_client_connect(event);
      return;
    case EIS_EVENT_CLIENT_DISCONNECT:
      eis_handle_client_disconnect(event);
      return;
    case EIS_EVENT_SEAT_BIND: {
      struct hevel_eis_client *client_state = eis_client_from_event(event);
      if (client_state) eis_sync_bound_devices(client_state, event);
      return;
    }
    case EIS_EVENT_DEVICE_CLOSED:
      eis_handle_device_closed(event);
      return;
    case EIS_EVENT_DEVICE_START_EMULATING:
      eis_handle_emulation_state(event, true);
      return;
    case EIS_EVENT_DEVICE_STOP_EMULATING:
      eis_handle_emulation_state(event, false);
      return;
    case EIS_EVENT_POINTER_MOTION:
      eis_forward_relative_motion(event);
      return;
    case EIS_EVENT_POINTER_MOTION_ABSOLUTE:
      eis_forward_absolute_motion(event);
      return;
    case EIS_EVENT_BUTTON_BUTTON:
      eis_forward_button(event);
      return;
    case EIS_EVENT_SCROLL_DELTA:
      eis_forward_scroll_delta(event);
      return;
    case EIS_EVENT_SCROLL_DISCRETE:
      eis_forward_scroll_discrete(event);
      return;
    case EIS_EVENT_KEYBOARD_KEY:
      eis_forward_key(event);
      return;
    case EIS_EVENT_FRAME:
      eis_forward_frame(event);
      return;
    case EIS_EVENT_SCROLL_STOP:
    case EIS_EVENT_SCROLL_CANCEL:
      fprintf(stderr, "hevel: ignoring %s\n",
              eis_event_name(eis_event_get_type(event)));
      return;
    default:
      fprintf(stderr, "hevel: ignoring unsupported EIS event %s\n",
              eis_event_name(eis_event_get_type(event)));
      return;
  }
}

static int
eis_dispatch_ready(int fd, uint32_t mask, void *data)
{
  (void)fd;
  (void)data;

  if ((mask & WL_EVENT_READABLE) == 0) return 0;

  eis_dispatch(eis_state.ctx);
  for (;;) {
    struct eis_event *event = eis_get_event(eis_state.ctx);
    if (!event) break;
    eis_handle_event(event);
    eis_event_unref(event);
  }

  return 0;
}

int
eis_initialize(void)
{
  int r;

  if (!eis_state.requests.next) wl_list_init(&eis_state.requests);
  if (!eis_state.clients.next) wl_list_init(&eis_state.clients);

  if (eis_state.ctx) {
    eis_state.available = true;
    return 0;
  }

  if (!compositor.evloop) {
    fprintf(stderr, "hevel: cannot initialize EIS without compositor event loop\n");
    eis_state.available = false;
    return -1;
  }

  wl_list_init(&eis_state.requests);
  wl_list_init(&eis_state.clients);
  eis_state.next_request_id = 0;
  eis_state.fd = -1;

  eis_state.ctx = eis_new(NULL);
  if (!eis_state.ctx) {
    fprintf(stderr, "hevel: cannot create EIS context\n");
    eis_state.available = false;
    return -1;
  }

  r = eis_setup_backend_fd(eis_state.ctx);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot initialize EIS backend: %s\n",
            strerror(-r));
    eis_finalize();
    return -1;
  }

  eis_state.fd = eis_get_fd(eis_state.ctx);
  if (eis_state.fd < 0) {
    fprintf(stderr, "hevel: cannot get EIS backend fd\n");
    eis_finalize();
    return -1;
  }

  eis_state.source = wl_event_loop_add_fd(compositor.evloop, eis_state.fd,
                                          WL_EVENT_READABLE, eis_dispatch_ready,
                                          NULL);
  if (!eis_state.source) {
    fprintf(stderr, "hevel: cannot attach EIS fd to event loop\n");
    eis_finalize();
    return -1;
  }

  eis_state.available = true;
  fprintf(stderr, "hevel: compositor EIS server ready\n");
  return 0;
}

void
eis_finalize(void)
{
  if (eis_state.clients.next) {
    while (!wl_list_empty(&eis_state.clients)) {
      struct hevel_eis_client *client_state =
          wl_container_of(eis_state.clients.next, client_state, link);
      eis_cleanup_client(client_state);
    }
    wl_list_init(&eis_state.clients);
  }

  if (eis_state.requests.next) {
    while (!wl_list_empty(&eis_state.requests)) {
      struct hevel_eis_request *request =
          wl_container_of(eis_state.requests.next, request, link);
      wl_list_remove(&request->link);
      eis_request_free(request);
    }
    wl_list_init(&eis_state.requests);
  }

  if (eis_state.source) {
    wl_event_source_remove(eis_state.source);
    eis_state.source = NULL;
  }

  if (eis_state.ctx) {
    eis_unref(eis_state.ctx);
    eis_state.ctx = NULL;
  }

  eis_state.fd = -1;
  eis_state.available = false;
}

int
eis_open_client_fd(const char *session_id, uint32_t capabilities,
                   const char *name)
{
  struct hevel_eis_request *request;
  int fd;

  if (!eis_state.available || !eis_state.ctx) return -ENOTCONN;

  request = eis_new_request(session_id, capabilities, name);
  if (!request) return -ENOMEM;

  fd = eis_backend_fd_add_client(eis_state.ctx);
  if (fd < 0) {
    eis_request_free(request);
    return fd;
  }

  wl_list_insert(eis_state.requests.prev, &request->link);
  return fd;
}

int
eis_close_session(const char *session_id)
{
  struct hevel_eis_client *client_state, *client_tmp;
  struct hevel_eis_request *request, *request_tmp;
  bool found = false;

  if (!session_id || session_id[0] == '\0') return -EINVAL;
  if (!eis_state.requests.next || !eis_state.clients.next) return -ENOTCONN;

  wl_list_for_each_safe(request, request_tmp, &eis_state.requests, link)
  {
    if (!eis_request_matches_session(request, session_id)) continue;
    wl_list_remove(&request->link);
    wl_list_init(&request->link);
    eis_request_free(request);
    found = true;
  }

  wl_list_for_each_safe(client_state, client_tmp, &eis_state.clients, link)
  {
    if (!eis_request_matches_session(client_state->request, session_id)) continue;
    if (client_state->client) eis_client_disconnect(client_state->client);
    eis_cleanup_client(client_state);
    found = true;
  }

  return found ? 0 : -ENOENT;
}
