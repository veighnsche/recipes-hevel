#include "hevel.h"

#include <errno.h>
#include <time.h>

#include <libeis.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#define HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS 0U
#define HEVEL_INPUTCAPTURE_RESPONSE_OTHER 2U
#define HEVEL_INPUTCAPTURE_VERSION 1U
#define HEVEL_INPUTCAPTURE_SESSION_VERSION 1U
#define HEVEL_INPUTCAPTURE_CAPABILITY_KEYBOARD 1U
#define HEVEL_INPUTCAPTURE_CAPABILITY_POINTER 2U
#define HEVEL_INPUTCAPTURE_SUPPORTED_CAPABILITIES                              \
  (HEVEL_INPUTCAPTURE_CAPABILITY_KEYBOARD |                                 \
   HEVEL_INPUTCAPTURE_CAPABILITY_POINTER)
#define HEVEL_INPUTCAPTURE_ZONE_WATCH_USEC (500ULL * 1000ULL)

static const char *inputcapture_request_interface =
    "org.freedesktop.impl.portal.Request";
static const char *inputcapture_session_interface =
    "org.freedesktop.impl.portal.Session";
static const char *inputcapture_object_path =
    "/org/freedesktop/portal/desktop";
static const char *inputcapture_portal_error =
    "org.freedesktop.portal.Error.Failed";

struct inputcapture_state inputcapture = {0};

struct inputcapture_session_created_payload {
  const struct hevel_ic_session *session;
};

struct inputcapture_zones_payload {
  struct wl_list *zones;
  uint32_t zone_set;
};

struct inputcapture_failed_barriers_payload {
  const uint32_t *failed_ids;
  size_t failed_count;
};

struct inputcapture_signal_payload {
  uint32_t zone_set;
  uint32_t activation_id;
  uint32_t barrier_id;
  double cursor_x;
  double cursor_y;
  bool have_zone_set;
  bool have_activation_id;
  bool have_barrier_id;
  bool have_cursor_position;
};

typedef int (*inputcapture_dict_appender)(sd_bus_message *message, void *userdata);

static int inputcapture_method_close_request(sd_bus_message *m, void *userdata,
                                             sd_bus_error *ret_error);
static int inputcapture_method_close_session(sd_bus_message *m, void *userdata,
                                             sd_bus_error *ret_error);
static int inputcapture_property_session_version(sd_bus *bus, const char *path,
                                                 const char *interface,
                                                 const char *property,
                                                 sd_bus_message *reply,
                                                 void *userdata,
                                                 sd_bus_error *ret_error);
static int inputcapture_zone_watch(sd_event_source *source, uint64_t usec,
                                   void *userdata);
static void inputcapture_destroy_request(struct hevel_ic_request *request);
static void inputcapture_destroy_session(struct hevel_ic_session *session,
                                         bool emit_closed);
static void inputcapture_destroy_requests(void);
static void inputcapture_destroy_sessions(void);
static void inputcapture_update_zone_watch(void);
static int inputcapture_sync_zones_from_compositor(bool emit_changes);
static void inputcapture_emit_disabled(struct hevel_ic_session *session);
static void
inputcapture_emit_deactivated(struct hevel_ic_session *session,
                              const struct inputcapture_signal_payload *payload);

static const sd_bus_vtable inputcapture_request_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Close", "", "", inputcapture_method_close_request,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable inputcapture_session_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Close", "", "", inputcapture_method_close_session,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Closed", "", 0),
    SD_BUS_PROPERTY("version", "u", inputcapture_property_session_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static void
inputcapture_log(const char *message, const char *handle, const char *detail)
{
  fprintf(stderr, "hevel: InputCapture %s%s%s%s\n", message,
          handle && handle[0] != '\0' ? " " : "",
          handle && handle[0] != '\0' ? handle : "",
          detail && detail[0] != '\0' ? detail : "");
}

static int
inputcapture_append_dict_u32(sd_bus_message *reply, const char *key,
                             uint32_t value)
{
  int r;

  r = sd_bus_message_open_container(reply, 'e', "sv");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'v', "u");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "u", value);
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}

static int
inputcapture_append_dict_string(sd_bus_message *reply, const char *key,
                                const char *value)
{
  int r;

  r = sd_bus_message_open_container(reply, 'e', "sv");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'v', "s");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", value);
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}

static int
inputcapture_append_dict_cursor_position(sd_bus_message *reply, const char *key,
                                         double x, double y)
{
  int r;

  r = sd_bus_message_open_container(reply, 'e', "sv");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'v', "(dd)");
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'r', "dd");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "dd", x, y);
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}

static int
inputcapture_append_dict_zones(sd_bus_message *reply, const char *key,
                               struct wl_list *zones)
{
  struct hevel_ic_zone *zone;
  int r;

  r = sd_bus_message_open_container(reply, 'e', "sv");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'v', "a(uuii)");
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'a', "(uuii)");
  if (r < 0) return r;

  wl_list_for_each(zone, zones, link)
  {
    r = sd_bus_message_append(reply, "(uuii)", zone->geometry.width,
                              zone->geometry.height, zone->geometry.x,
                              zone->geometry.y);
    if (r < 0) return r;
  }

  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}

static int
inputcapture_append_dict_failed_barriers(sd_bus_message *reply, const char *key,
                                         const uint32_t *failed_ids,
                                         size_t failed_count)
{
  int r;

  r = sd_bus_message_open_container(reply, 'e', "sv");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'v', "au");
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'a', "u");
  if (r < 0) return r;

  for (size_t i = 0; i < failed_count; ++i) {
    r = sd_bus_message_append(reply, "u", failed_ids[i]);
    if (r < 0) return r;
  }

  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}

static int
inputcapture_append_session_created(sd_bus_message *reply, void *userdata)
{
  struct inputcapture_session_created_payload *payload = userdata;
  int r;

  r = inputcapture_append_dict_string(reply, "session_id",
                                      payload->session->session_id);
  if (r < 0) return r;

  return inputcapture_append_dict_u32(reply, "capabilities",
                                      payload->session->capabilities);
}

static int
inputcapture_append_zones_results(sd_bus_message *reply, void *userdata)
{
  struct inputcapture_zones_payload *payload = userdata;
  int r;

  r = inputcapture_append_dict_zones(reply, "zones", payload->zones);
  if (r < 0) return r;

  return inputcapture_append_dict_u32(reply, "zone_set", payload->zone_set);
}

static int
inputcapture_append_failed_barriers_results(sd_bus_message *reply,
                                            void *userdata)
{
  struct inputcapture_failed_barriers_payload *payload = userdata;

  return inputcapture_append_dict_failed_barriers(
      reply, "failed_barriers", payload->failed_ids, payload->failed_count);
}

static int
inputcapture_append_signal_payload(sd_bus_message *reply, void *userdata)
{
  struct inputcapture_signal_payload *payload = userdata;
  int r;

  if (payload->have_zone_set) {
    r = inputcapture_append_dict_u32(reply, "zone_set", payload->zone_set);
    if (r < 0) return r;
  }
  if (payload->have_activation_id) {
    r = inputcapture_append_dict_u32(reply, "activation_id",
                                     payload->activation_id);
    if (r < 0) return r;
  }
  if (payload->have_barrier_id) {
    r = inputcapture_append_dict_u32(reply, "barrier_id", payload->barrier_id);
    if (r < 0) return r;
  }
  if (payload->have_cursor_position)
    return inputcapture_append_dict_cursor_position(
        reply, "cursor_position", payload->cursor_x, payload->cursor_y);

  return 0;
}

static int
inputcapture_send_response(sd_bus_message *m, uint32_t response,
                           inputcapture_dict_appender append,
                           void *userdata)
{
  sd_bus_message *reply = NULL;
  int r;

  r = sd_bus_message_new_method_return(m, &reply);
  if (r < 0) return r;

  r = sd_bus_message_append(reply, "u", response);
  if (r < 0) goto out;

  r = sd_bus_message_open_container(reply, 'a', "{sv}");
  if (r < 0) goto out;

  if (append) {
    r = append(reply, userdata);
    if (r < 0) goto out;
  }

  r = sd_bus_message_close_container(reply);
  if (r < 0) goto out;

  r = sd_bus_send(sd_bus_message_get_bus(m), reply, NULL);

out:
  sd_bus_message_unref(reply);
  return r;
}

static int
inputcapture_reply_empty(sd_bus_message *m, uint32_t response)
{
  return inputcapture_send_response(m, response, NULL, NULL);
}

static int
inputcapture_reply_rejected(sd_bus_message *m, const char *context,
                            const char *detail)
{
  inputcapture_log(context, NULL, detail);
  return inputcapture_reply_empty(m, HEVEL_INPUTCAPTURE_RESPONSE_OTHER);
}

static int
inputcapture_emit_signal(struct hevel_ic_session *session, const char *member,
                         inputcapture_dict_appender append, void *userdata)
{
  sd_bus_message *signal = NULL;
  int r;

  if (!portal.bus || !session) return 0;

  r = sd_bus_message_new_signal(portal.bus, &signal, inputcapture_object_path,
                                "org.freedesktop.impl.portal.InputCapture",
                                member);
  if (r < 0) return r;

  r = sd_bus_message_append(signal, "o", session->session_handle);
  if (r < 0) goto out;

  r = sd_bus_message_open_container(signal, 'a', "{sv}");
  if (r < 0) goto out;

  if (append) {
    r = append(signal, userdata);
    if (r < 0) goto out;
  }

  r = sd_bus_message_close_container(signal);
  if (r < 0) goto out;

  r = sd_bus_send(portal.bus, signal, NULL);

out:
  sd_bus_message_unref(signal);
  return r;
}

static void
inputcapture_emit_zones_changed_all(void)
{
  struct hevel_ic_session *session;
  struct inputcapture_signal_payload payload = {
      .zone_set = inputcapture.zone_set_serial,
      .have_zone_set = true,
  };

  wl_list_for_each(session, &inputcapture.sessions, link)
  {
    if (inputcapture_emit_signal(session, "ZonesChanged",
                                 inputcapture_append_signal_payload,
                                 &payload) < 0)
      fprintf(stderr, "hevel: failed to emit InputCapture ZonesChanged\n");
  }
}

static void
inputcapture_emit_disabled(struct hevel_ic_session *session)
{
  if (inputcapture_emit_signal(session, "Disabled", NULL, NULL) < 0)
    fprintf(stderr, "hevel: failed to emit InputCapture Disabled\n");
}

static void
inputcapture_emit_deactivated(struct hevel_ic_session *session,
                              const struct inputcapture_signal_payload *payload)
{
  if (inputcapture_emit_signal(session, "Deactivated",
                               payload ? inputcapture_append_signal_payload
                                       : NULL,
                               (void *)payload) < 0)
    fprintf(stderr, "hevel: failed to emit InputCapture Deactivated\n");
}

static void
inputcapture_destroy_zone_list(struct wl_list *zones)
{
  struct hevel_ic_zone *zone, *tmp;

  if (!zones || !zones->next) return;

  wl_list_for_each_safe(zone, tmp, zones, link)
  {
    wl_list_remove(&zone->link);
    wl_list_init(&zone->link);
    free(zone);
  }
}

static void
inputcapture_destroy_barrier(struct hevel_ic_barrier *barrier)
{
  if (!barrier) return;
  wl_list_remove(&barrier->link);
  wl_list_init(&barrier->link);
  free(barrier);
}

static void
inputcapture_destroy_barrier_list(struct wl_list *barriers)
{
  struct hevel_ic_barrier *barrier, *tmp;

  if (!barriers || !barriers->next) return;

  wl_list_for_each_safe(barrier, tmp, barriers, link)
  {
    inputcapture_destroy_barrier(barrier);
  }
}

static bool
inputcapture_zone_equals(const struct hevel_ic_zone *left,
                         const struct hevel_ic_zone *right)
{
  if (left->id != right->id) return false;
  if (left->geometry.x != right->geometry.x) return false;
  if (left->geometry.y != right->geometry.y) return false;
  if (left->geometry.width != right->geometry.width) return false;
  if (left->geometry.height != right->geometry.height) return false;
  return true;
}

static bool
inputcapture_zone_lists_equal(struct wl_list *left, struct wl_list *right)
{
  struct wl_list *left_link;
  struct wl_list *right_link;
  struct hevel_ic_zone *left_zone;
  struct hevel_ic_zone *right_zone;

  left_link = left->next;
  right_link = right->next;

  while (left_link != left && right_link != right) {
    left_zone = wl_container_of(left_link, left_zone, link);
    right_zone = wl_container_of(right_link, right_zone, link);
    if (!inputcapture_zone_equals(left_zone, right_zone)) return false;
    left_link = left_link->next;
    right_link = right_link->next;
  }

  return left_link == left && right_link == right;
}

static struct hevel_ic_zone *
inputcapture_new_zone(const struct swc_screen *screen, uint32_t zone_id)
{
  struct hevel_ic_zone *zone;

  if (!screen) return NULL;

  zone = calloc(1, sizeof(*zone));
  if (!zone) return NULL;

  wl_list_init(&zone->link);
  zone->id = zone_id;
  zone->geometry = screen->geometry;
  return zone;
}

static struct hevel_ic_request *
inputcapture_find_request(const char *handle)
{
  struct hevel_ic_request *request;

  wl_list_for_each(request, &inputcapture.requests, link)
  {
    if (strcmp(request->handle, handle) == 0) return request;
  }

  return NULL;
}

static struct hevel_ic_session *
inputcapture_find_session(const char *session_handle)
{
  struct hevel_ic_session *session;

  wl_list_for_each(session, &inputcapture.sessions, link)
  {
    if (strcmp(session->session_handle, session_handle) == 0) return session;
  }

  return NULL;
}

static int
inputcapture_generate_session_id(char *buffer, size_t buffer_size)
{
  ++portal.next_session_id;
  return snprintf(buffer, buffer_size, "hevel-ic-%u", portal.next_session_id) <
                 (int)buffer_size
             ? 0
             : -ENAMETOOLONG;
}

static uint32_t
inputcapture_map_eis_capabilities(uint32_t capabilities)
{
  uint32_t eis_capabilities = 0;

  if ((capabilities & HEVEL_INPUTCAPTURE_CAPABILITY_KEYBOARD) != 0)
    eis_capabilities |= EIS_DEVICE_CAP_KEYBOARD;

  if ((capabilities & HEVEL_INPUTCAPTURE_CAPABILITY_POINTER) != 0) {
    eis_capabilities |= EIS_DEVICE_CAP_POINTER;
    eis_capabilities |= EIS_DEVICE_CAP_POINTER_ABSOLUTE;
    eis_capabilities |= EIS_DEVICE_CAP_BUTTON;
    eis_capabilities |= EIS_DEVICE_CAP_SCROLL;
  }

  return eis_capabilities;
}

static int
inputcapture_new_request(const char *handle, const char *session_handle,
                         const char *app_id,
                         struct hevel_ic_request **out_request)
{
  struct hevel_ic_request *request;
  int r;

  if (!portal.bus) return -ENOTCONN;
  if (inputcapture_find_request(handle)) return -EEXIST;

  request = calloc(1, sizeof(*request));
  if (!request) return -ENOMEM;

  wl_list_init(&request->link);
  snprintf(request->handle, sizeof(request->handle), "%s", handle);
  snprintf(request->session_handle, sizeof(request->session_handle), "%s",
           session_handle ? session_handle : "");
  snprintf(request->app_id, sizeof(request->app_id), "%s",
           app_id ? app_id : "");

  r = sd_bus_add_object_vtable(portal.bus, &request->slot, request->handle,
                               inputcapture_request_interface,
                               inputcapture_request_vtable, request);
  if (r < 0) {
    free(request);
    return r;
  }

  wl_list_insert(inputcapture.requests.prev, &request->link);
  *out_request = request;
  return 0;
}

static int
inputcapture_new_session(const char *session_handle, const char *app_id,
                         const char *parent_window, uint32_t capabilities,
                         struct hevel_ic_session **out_session)
{
  struct hevel_ic_session *session;
  int r;

  if (!portal.bus) return -ENOTCONN;
  if (inputcapture_find_session(session_handle)) return -EEXIST;

  session = calloc(1, sizeof(*session));
  if (!session) return -ENOMEM;

  wl_list_init(&session->link);
  wl_list_init(&session->barriers);
  snprintf(session->session_handle, sizeof(session->session_handle), "%s",
           session_handle);
  snprintf(session->app_id, sizeof(session->app_id), "%s", app_id ? app_id : "");
  snprintf(session->parent_window, sizeof(session->parent_window), "%s",
           parent_window ? parent_window : "");
  session->capabilities =
      capabilities & HEVEL_INPUTCAPTURE_SUPPORTED_CAPABILITIES;
  session->enable_state = HEVEL_IC_DISABLED;
  session->capture_state = HEVEL_IC_INACTIVE;
  session->barriers_stale = false;

  r = inputcapture_generate_session_id(session->session_id,
                                       sizeof(session->session_id));
  if (r < 0) {
    free(session);
    return r;
  }

  r = sd_bus_add_object_vtable(portal.bus, &session->slot,
                               session->session_handle,
                               inputcapture_session_interface,
                               inputcapture_session_vtable, session);
  if (r < 0) {
    free(session);
    return r;
  }

  wl_list_insert(inputcapture.sessions.prev, &session->link);
  inputcapture_update_zone_watch();
  *out_session = session;
  return 0;
}

static int
inputcapture_require_session(sd_bus_message *m, const char *session_handle,
                             const char *app_id,
                             struct hevel_ic_session **out_session)
{
  struct hevel_ic_session *session = inputcapture_find_session(session_handle);

  if (!session)
    return inputcapture_reply_rejected(m, "rejecting unknown session",
                                       session_handle);

  if (strcmp(session->app_id, app_id ? app_id : "") != 0)
    return inputcapture_reply_rejected(m, "rejecting app-id mismatch for",
                                       session_handle);

  *out_session = session;
  return 0;
}

static void
inputcapture_transition_enable(struct hevel_ic_session *session,
                               enum hevel_ic_enable_state state)
{
  session->enable_state = state;
}

static void
inputcapture_transition_capture(struct hevel_ic_session *session,
                                enum hevel_ic_capture_state state)
{
  session->capture_state = state;
}

static bool
inputcapture_session_has_barriers(const struct hevel_ic_session *session)
{
  return session && !wl_list_empty((struct wl_list *)&session->barriers);
}

static void
inputcapture_set_session_inactive(struct hevel_ic_session *session,
                                  bool keep_enabled,
                                  const struct inputcapture_signal_payload *payload)
{
  const bool was_active =
      session->capture_state == HEVEL_IC_ACTIVE;

  inputcapture_transition_capture(session, HEVEL_IC_INACTIVE);
  if (!keep_enabled) inputcapture_transition_enable(session, HEVEL_IC_DISABLED);

  if (was_active) inputcapture_emit_deactivated(session, payload);
}

static void
inputcapture_disable_session(struct hevel_ic_session *session, bool emit_signal)
{
  const bool was_enabled =
      session->enable_state == HEVEL_IC_ENABLED;

  if (emit_signal)
    inputcapture_set_session_inactive(session, false, NULL);
  else {
    inputcapture_transition_capture(session, HEVEL_IC_INACTIVE);
    inputcapture_transition_enable(session, HEVEL_IC_DISABLED);
  }
  session->activation_id = 0;
  session->have_release_cursor = false;

  if (emit_signal && was_enabled) inputcapture_emit_disabled(session);
}

static void
inputcapture_destroy_request(struct hevel_ic_request *request)
{
  if (!request) return;

  wl_list_remove(&request->link);
  wl_list_init(&request->link);

  if (request->slot) {
    sd_bus_slot_unref(request->slot);
    request->slot = NULL;
  }

  free(request);
}

static void
inputcapture_destroy_requests(void)
{
  while (!wl_list_empty(&inputcapture.requests)) {
    struct hevel_ic_request *request =
        wl_container_of(inputcapture.requests.next, request, link);
    inputcapture_destroy_request(request);
  }

  wl_list_init(&inputcapture.requests);
}

static void
inputcapture_destroy_session(struct hevel_ic_session *session, bool emit_closed)
{
  if (!session) return;

  if (inputcapture_session_has_barriers(session)) {
    inputcapture_disable_session(session, emit_closed);
    inputcapture_destroy_barrier_list(&session->barriers);
    wl_list_init(&session->barriers);
    session->barrier_zone_serial = 0;
    session->barriers_stale = false;
  } else {
    inputcapture_disable_session(session, false);
  }

  if (session->eis_fd_issued && session->session_id[0] != '\0' &&
      inject_close_eis_session(NULL, session->session_id) < 0)
    fprintf(stderr, "hevel: failed to revoke InputCapture EIS session %s\n",
            session->session_id);

  if (emit_closed && portal.bus)
    sd_bus_emit_signal(portal.bus, session->session_handle,
                       inputcapture_session_interface, "Closed", "");

  wl_list_remove(&session->link);
  wl_list_init(&session->link);

  if (session->slot) {
    sd_bus_slot_unref(session->slot);
    session->slot = NULL;
  }

  free(session);
  inputcapture_update_zone_watch();
}

static void
inputcapture_destroy_sessions(void)
{
  while (!wl_list_empty(&inputcapture.sessions)) {
    struct hevel_ic_session *session =
        wl_container_of(inputcapture.sessions.next, session, link);
    inputcapture_destroy_session(session, false);
  }

  wl_list_init(&inputcapture.sessions);
}

static int
inputcapture_skip_options(sd_bus_message *m)
{
  int r;

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char *key = NULL;
    const char *contents = NULL;
    char type;

    r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;

    r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (type != 'v') return -EINVAL;

    r = sd_bus_message_enter_container(m, 'v', contents);
    if (r < 0) return r;

    r = sd_bus_message_skip(m, contents);
    if (r < 0) return r;

    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
  }

  if (r < 0) return r;
  return sd_bus_message_exit_container(m);
}

static int
inputcapture_parse_create_session_options(sd_bus_message *m,
                                          uint32_t *capabilities,
                                          bool *have_capabilities)
{
  int r;

  *capabilities = 0;
  *have_capabilities = false;

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char *key = NULL;
    const char *contents = NULL;
    char type;

    r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;

    r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (type != 'v') return -EINVAL;

    r = sd_bus_message_enter_container(m, 'v', contents);
    if (r < 0) return r;

    if (strcmp(key, "capabilities") == 0) {
      if (strcmp(contents, "u") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "u", capabilities);
      if (r < 0) return r;
      *have_capabilities = true;
    } else {
      r = sd_bus_message_skip(m, contents);
      if (r < 0) return r;
    }

    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
  }

  if (r < 0) return r;
  return sd_bus_message_exit_container(m);
}

static int
inputcapture_parse_release_options(sd_bus_message *m, uint32_t *activation_id,
                                   bool *have_activation_id, double *cursor_x,
                                   double *cursor_y, bool *have_cursor)
{
  int r;

  *activation_id = 0;
  *have_activation_id = false;
  *cursor_x = 0.0;
  *cursor_y = 0.0;
  *have_cursor = false;

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char *key = NULL;
    const char *contents = NULL;
    char type;

    r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;

    r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (type != 'v') return -EINVAL;

    r = sd_bus_message_enter_container(m, 'v', contents);
    if (r < 0) return r;

    if (strcmp(key, "activation_id") == 0) {
      if (strcmp(contents, "u") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "u", activation_id);
      if (r < 0) return r;
      *have_activation_id = true;
    } else if (strcmp(key, "cursor_position") == 0) {
      if (strcmp(contents, "(dd)") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "(dd)", cursor_x, cursor_y);
      if (r < 0) return r;
      *have_cursor = true;
    } else {
      r = sd_bus_message_skip(m, contents);
      if (r < 0) return r;
    }

    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
  }

  if (r < 0) return r;
  return sd_bus_message_exit_container(m);
}

static int
inputcapture_parse_barrier_entry(sd_bus_message *m,
                                 struct hevel_ic_barrier *barrier)
{
  bool have_id = false;
  bool have_position = false;
  int r;

  memset(barrier, 0, sizeof(*barrier));

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char *key = NULL;
    const char *contents = NULL;
    char type;

    r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;

    r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (type != 'v') return -EINVAL;

    r = sd_bus_message_enter_container(m, 'v', contents);
    if (r < 0) return r;

    if (strcmp(key, "barrier_id") == 0) {
      if (strcmp(contents, "u") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "u", &barrier->id);
      if (r < 0) return r;
      have_id = true;
    } else if (strcmp(key, "position") == 0) {
      if (strcmp(contents, "(iiii)") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "(iiii)", &barrier->x1, &barrier->y1,
                              &barrier->x2, &barrier->y2);
      if (r < 0) return r;
      have_position = true;
    } else {
      r = sd_bus_message_skip(m, contents);
      if (r < 0) return r;
    }

    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
  }

  if (r < 0) return r;

  return have_id && have_position ? 0 : -EINVAL;
}

static bool
inputcapture_barrier_id_in_list(struct wl_list *barriers, uint32_t id)
{
  struct hevel_ic_barrier *barrier;

  wl_list_for_each(barrier, barriers, link)
  {
    if (barrier->id == id) return true;
  }

  return false;
}

static bool
inputcapture_barrier_segment_valid(const struct hevel_ic_barrier *barrier)
{
  if (barrier->id == 0) return false;

  if (barrier->x1 == barrier->x2) return barrier->y1 <= barrier->y2;
  if (barrier->y1 == barrier->y2) return barrier->x1 <= barrier->x2;
  return false;
}

static bool
inputcapture_zone_contains_point(const struct hevel_ic_zone *zone, int32_t x,
                                 int32_t y)
{
  const int32_t x_max = zone->geometry.x + (int32_t)zone->geometry.width;
  const int32_t y_max = zone->geometry.y + (int32_t)zone->geometry.height;

  return x >= zone->geometry.x && x < x_max && y >= zone->geometry.y &&
         y < y_max;
}

static const struct hevel_ic_zone *
inputcapture_find_zone_at_point(int32_t x, int32_t y)
{
  struct hevel_ic_zone *zone;

  wl_list_for_each(zone, &inputcapture.zones, link)
  {
    if (inputcapture_zone_contains_point(zone, x, y)) return zone;
  }

  return NULL;
}

static const struct hevel_ic_zone *
inputcapture_find_barrier_zone(const struct hevel_ic_barrier *barrier)
{
  const struct hevel_ic_zone *owner_zone = NULL;
  int32_t start;
  int32_t end;
  int32_t cursor;

  if (barrier->x1 == barrier->x2) {
    start = barrier->y1;
    end = barrier->y2;
    for (cursor = start; cursor <= end; ++cursor) {
      const struct hevel_ic_zone *left =
          inputcapture_find_zone_at_point(barrier->x1 - 1, cursor);
      const struct hevel_ic_zone *right =
          inputcapture_find_zone_at_point(barrier->x1, cursor);
      const struct hevel_ic_zone *inside = left ? left : right;

      if (!!left == !!right) return NULL;
      if (!inside) return NULL;
      if (!owner_zone) owner_zone = inside;
    }

    return owner_zone;
  }

  if (barrier->y1 == barrier->y2) {
    start = barrier->x1;
    end = barrier->x2;
    for (cursor = start; cursor <= end; ++cursor) {
      const struct hevel_ic_zone *above =
          inputcapture_find_zone_at_point(cursor, barrier->y1 - 1);
      const struct hevel_ic_zone *below =
          inputcapture_find_zone_at_point(cursor, barrier->y1);
      const struct hevel_ic_zone *inside = above ? above : below;

      if (!!above == !!below) return NULL;
      if (!inside) return NULL;
      if (!owner_zone) owner_zone = inside;
    }

    return owner_zone;
  }

  return NULL;
}

static int
inputcapture_append_failed_id(uint32_t **failed_ids, size_t *failed_count,
                              uint32_t barrier_id)
{
  uint32_t *resized;

  resized = realloc(*failed_ids, sizeof(**failed_ids) * (*failed_count + 1));
  if (!resized) return -ENOMEM;

  resized[*failed_count] = barrier_id;
  *failed_ids = resized;
  ++(*failed_count);
  return 0;
}

static int
inputcapture_refresh_local_zones(struct wl_list *new_zones, uint32_t zone_set_serial,
                                 bool emit_changes)
{
  struct hevel_ic_session *session;
  const bool changed =
      zone_set_serial != inputcapture.zone_set_serial ||
      !inputcapture_zone_lists_equal(&inputcapture.zones, new_zones);

  if (!changed) {
    inputcapture_destroy_zone_list(new_zones);
    return 0;
  }

  inputcapture_destroy_zone_list(&inputcapture.zones);
  wl_list_init(&inputcapture.zones);

  while (!wl_list_empty(new_zones)) {
    struct hevel_ic_zone *zone =
        wl_container_of(new_zones->next, zone, link);
    wl_list_remove(&zone->link);
    wl_list_insert(inputcapture.zones.prev, &zone->link);
  }

  inputcapture.zone_set_serial = zone_set_serial;

  wl_list_for_each(session, &inputcapture.sessions, link)
  {
    if (!inputcapture_session_has_barriers(session)) continue;
    if (session->barrier_zone_serial == inputcapture.zone_set_serial) {
      session->barriers_stale = false;
      continue;
    }

    session->barriers_stale = true;
    if (session->enable_state == HEVEL_IC_ENABLED)
      inputcapture_disable_session(session, true);
  }

  if (emit_changes) inputcapture_emit_zones_changed_all();
  return 0;
}

static int
inputcapture_sync_zones_from_compositor(bool emit_changes)
{
  struct wl_list zones;
  uint32_t zone_set_serial = 0;
  int r;

  wl_list_init(&zones);
  r = inject_fetch_inputcapture_zones(NULL, &zones, &zone_set_serial);
  if (r < 0) return r;

  return inputcapture_refresh_local_zones(&zones, zone_set_serial, emit_changes);
}

static int
inputcapture_zone_watch(sd_event_source *source, uint64_t usec, void *userdata)
{
  uint64_t next = 0;
  int r;

  (void)usec;
  (void)userdata;

  if (!inputcapture.initialized || wl_list_empty(&inputcapture.sessions)) {
    if (source) {
      sd_event_source_unref(source);
      inputcapture.zone_watch_source = NULL;
    }
    return 0;
  }

  r = inputcapture_sync_zones_from_compositor(true);
  if (r < 0 && r != -ENOENT)
    fprintf(stderr, "hevel: failed to poll InputCapture zones: %s\n",
            strerror(-r));

  if (!portal.event || !source) return 0;

  r = sd_event_now(portal.event, CLOCK_MONOTONIC, &next);
  if (r < 0) return 0;
  next += HEVEL_INPUTCAPTURE_ZONE_WATCH_USEC;

  r = sd_event_source_set_time(source, next);
  if (r < 0) return 0;

  return sd_event_source_set_enabled(source, SD_EVENT_ON);
}

static void
inputcapture_update_zone_watch(void)
{
  int r;

  if (!portal.event) return;

  if (wl_list_empty(&inputcapture.sessions)) {
    if (inputcapture.zone_watch_source) {
      sd_event_source_unref(inputcapture.zone_watch_source);
      inputcapture.zone_watch_source = NULL;
    }
    return;
  }

  if (inputcapture.zone_watch_source) return;

  r = sd_event_add_time_relative(portal.event, &inputcapture.zone_watch_source,
                                 CLOCK_MONOTONIC, 0, 0, inputcapture_zone_watch,
                                 NULL);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot add InputCapture zone watch: %s\n",
            strerror(-r));
    inputcapture.zone_watch_source = NULL;
  }
}

int
inputcapture_initialize(void)
{
  if (inputcapture.initialized) return 0;

  wl_list_init(&inputcapture.requests);
  wl_list_init(&inputcapture.sessions);
  wl_list_init(&inputcapture.zones);
  inputcapture.zone_watch_source = NULL;
  inputcapture.zone_set_serial = 0;
  inputcapture.initialized = true;
  return 0;
}

void
inputcapture_finalize(void)
{
  if (!inputcapture.initialized) return;

  if (inputcapture.zone_watch_source) {
    sd_event_source_unref(inputcapture.zone_watch_source);
    inputcapture.zone_watch_source = NULL;
  }

  inputcapture_destroy_requests();
  inputcapture_destroy_sessions();
  inputcapture_destroy_zone_list(&inputcapture.zones);

  wl_list_init(&inputcapture.requests);
  wl_list_init(&inputcapture.sessions);
  wl_list_init(&inputcapture.zones);
  inputcapture.zone_set_serial = 0;
  inputcapture.initialized = false;
}

void
inputcapture_refresh_zones(void)
{
  struct wl_list new_zones;
  struct screen *screen;
  uint32_t zone_id = 0;

  if (!inputcapture.initialized) return;

  wl_list_init(&new_zones);

  wl_list_for_each(screen, &compositor.screens, link)
  {
    struct hevel_ic_zone *zone;

    if (!screen->swc) continue;

    zone = inputcapture_new_zone(screen->swc, ++zone_id);
    if (!zone) {
      inputcapture_destroy_zone_list(&new_zones);
      return;
    }

    wl_list_insert(new_zones.prev, &zone->link);
  }

  if (inputcapture_zone_lists_equal(&inputcapture.zones, &new_zones)) {
    inputcapture_destroy_zone_list(&new_zones);
    return;
  }

  if (inputcapture_refresh_local_zones(&new_zones,
                                       inputcapture.zone_set_serial < UINT32_MAX
                                           ? inputcapture.zone_set_serial + 1
                                           : UINT32_MAX,
                                       true) < 0)
    inputcapture_destroy_zone_list(&new_zones);
}

static int
inputcapture_method_close_request(sd_bus_message *m, void *userdata,
                                  sd_bus_error *ret_error)
{
  struct hevel_ic_request *request = userdata;
  int r;

  (void)ret_error;

  r = sd_bus_reply_method_return(m, "");
  inputcapture_destroy_request(request);
  return r;
}

static int
inputcapture_method_close_session(sd_bus_message *m, void *userdata,
                                  sd_bus_error *ret_error)
{
  struct hevel_ic_session *session = userdata;
  int r;

  (void)ret_error;

  r = sd_bus_reply_method_return(m, "");
  inputcapture_destroy_session(session, true);
  return r;
}

static int
inputcapture_method_create_session(sd_bus_message *m, void *userdata,
                                   sd_bus_error *ret_error)
{
  const char *handle;
  const char *session_handle;
  const char *app_id;
  const char *parent_window;
  struct hevel_ic_request *request = NULL;
  struct hevel_ic_session *session = NULL;
  struct inputcapture_session_created_payload payload;
  uint32_t requested_capabilities = 0;
  bool have_capabilities = false;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "ooss", &handle, &session_handle, &app_id,
                          &parent_window);
  if (r < 0) return r;

  r = inputcapture_parse_create_session_options(m, &requested_capabilities,
                                                &have_capabilities);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed CreateSession options");
  if (!have_capabilities || requested_capabilities == 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "missing InputCapture capabilities");

  r = inputcapture_new_request(handle, session_handle, app_id, &request);
  if (r == -EEXIST)
    return inputcapture_reply_rejected(m, "rejecting duplicate request",
                                       handle);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot export InputCapture request object");

  r = inputcapture_new_session(session_handle, app_id, parent_window,
                               requested_capabilities, &session);
  if (r == -EEXIST) {
    inputcapture_destroy_request(request);
    return inputcapture_reply_rejected(m, "rejecting duplicate session",
                                       session_handle);
  }
  if (r < 0) {
    inputcapture_destroy_request(request);
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot export InputCapture session object");
  }
  if (session->capabilities == 0) {
    inputcapture_destroy_request(request);
    inputcapture_destroy_session(session, false);
    return inputcapture_reply_rejected(m, "rejecting unsupported capabilities",
                                       session_handle);
  }

  payload.session = session;
  r = inputcapture_send_response(m, HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS,
                                 inputcapture_append_session_created, &payload);
  inputcapture_destroy_request(request);
  return r;
}

static int
inputcapture_method_get_zones(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error)
{
  const char *handle;
  const char *session_handle;
  const char *app_id;
  struct hevel_ic_request *request = NULL;
  struct hevel_ic_session *session = NULL;
  struct inputcapture_zones_payload payload;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "oos", &handle, &session_handle, &app_id);
  if (r < 0) return r;

  r = inputcapture_skip_options(m);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed GetZones options");

  r = inputcapture_new_request(handle, session_handle, app_id, &request);
  if (r == -EEXIST)
    return inputcapture_reply_rejected(m, "rejecting duplicate request",
                                       handle);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot export InputCapture request object");

  r = inputcapture_require_session(m, session_handle, app_id, &session);
  if (r != 0) {
    inputcapture_destroy_request(request);
    return r;
  }

  r = inputcapture_sync_zones_from_compositor(false);
  if (r < 0) {
    inputcapture_destroy_request(request);
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot query compositor zones");
  }

  payload.zones = &inputcapture.zones;
  payload.zone_set = inputcapture.zone_set_serial;
  r = inputcapture_send_response(m, HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS,
                                 inputcapture_append_zones_results, &payload);
  inputcapture_destroy_request(request);
  return r;
}

static int
inputcapture_method_set_pointer_barriers(sd_bus_message *m, void *userdata,
                                         sd_bus_error *ret_error)
{
  const char *handle;
  const char *session_handle;
  const char *app_id;
  struct hevel_ic_request *request = NULL;
  struct hevel_ic_session *session = NULL;
  struct wl_list accepted_barriers;
  struct hevel_ic_barrier *barrier, *tmp;
  struct inputcapture_failed_barriers_payload payload;
  uint32_t *failed_ids = NULL;
  uint32_t zone_set = 0;
  size_t failed_count = 0;
  int r;

  (void)userdata;

  wl_list_init(&accepted_barriers);

  r = sd_bus_message_read(m, "oos", &handle, &session_handle, &app_id);
  if (r < 0) return r;

  r = inputcapture_skip_options(m);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed SetPointerBarriers options");

  r = inputcapture_new_request(handle, session_handle, app_id, &request);
  if (r == -EEXIST)
    return inputcapture_reply_rejected(m, "rejecting duplicate request",
                                       handle);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot export InputCapture request object");

  r = inputcapture_require_session(m, session_handle, app_id, &session);
  if (r != 0) {
    inputcapture_destroy_request(request);
    return r;
  }

  r = sd_bus_message_enter_container(m, 'a', "a{sv}");
  if (r < 0) goto malformed;

  while ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) > 0) {
    struct hevel_ic_barrier *parsed;

    parsed = calloc(1, sizeof(*parsed));
    if (!parsed) {
      r = -ENOMEM;
      goto malformed;
    }

    wl_list_init(&parsed->link);
    r = inputcapture_parse_barrier_entry(m, parsed);
    if (r < 0) {
      free(parsed);
      goto malformed;
    }
    r = sd_bus_message_exit_container(m);
    if (r < 0) {
      free(parsed);
      goto malformed;
    }

    if (!inputcapture_barrier_segment_valid(parsed) ||
        inputcapture_barrier_id_in_list(&accepted_barriers, parsed->id)) {
      r = inputcapture_append_failed_id(&failed_ids, &failed_count, parsed->id);
      free(parsed);
      if (r < 0) goto malformed;
      continue;
    }

    wl_list_insert(accepted_barriers.prev, &parsed->link);
  }

  if (r < 0) goto malformed;

  r = sd_bus_message_exit_container(m);
  if (r < 0) goto malformed;

  r = sd_bus_message_read(m, "u", &zone_set);
  if (r < 0) goto malformed;

  r = inputcapture_sync_zones_from_compositor(false);
  if (r < 0) {
    inputcapture_destroy_request(request);
    inputcapture_destroy_barrier_list(&accepted_barriers);
    free(failed_ids);
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot query compositor zones");
  }

  if (zone_set != inputcapture.zone_set_serial) {
    wl_list_for_each_safe(barrier, tmp, &accepted_barriers, link)
    {
      r = inputcapture_append_failed_id(&failed_ids, &failed_count, barrier->id);
      if (r < 0) goto malformed;
      inputcapture_destroy_barrier(barrier);
    }

    payload.failed_ids = failed_ids;
    payload.failed_count = failed_count;
    r = inputcapture_send_response(m, HEVEL_INPUTCAPTURE_RESPONSE_OTHER,
                                   inputcapture_append_failed_barriers_results,
                                   &payload);
    inputcapture_destroy_request(request);
    free(failed_ids);
    return r;
  }

  wl_list_for_each_safe(barrier, tmp, &accepted_barriers, link)
  {
    const struct hevel_ic_zone *zone = inputcapture_find_barrier_zone(barrier);

    if (!zone) {
      r = inputcapture_append_failed_id(&failed_ids, &failed_count, barrier->id);
      if (r < 0) goto malformed;
      inputcapture_destroy_barrier(barrier);
      continue;
    }

    barrier->zone_id = zone->id;
    barrier->zone_serial = zone_set;
    barrier->owner = session;
  }

  if (session->enable_state == HEVEL_IC_ENABLED)
    inputcapture_disable_session(session, true);

  inputcapture_destroy_barrier_list(&session->barriers);
  wl_list_init(&session->barriers);
  session->barrier_zone_serial = 0;
  session->barriers_stale = false;

  while (!wl_list_empty(&accepted_barriers)) {
    barrier = wl_container_of(accepted_barriers.next, barrier, link);
    wl_list_remove(&barrier->link);
    wl_list_insert(session->barriers.prev, &barrier->link);
  }

  if (!wl_list_empty(&session->barriers)) {
    session->barrier_zone_serial = zone_set;
    session->barriers_stale = false;
  }

  payload.failed_ids = failed_ids;
  payload.failed_count = failed_count;
  r = inputcapture_send_response(m, HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS,
                                 inputcapture_append_failed_barriers_results,
                                 &payload);
  inputcapture_destroy_request(request);
  free(failed_ids);
  return r;

malformed:
  inputcapture_destroy_request(request);
  inputcapture_destroy_barrier_list(&accepted_barriers);
  free(failed_ids);
  if (r == -ENOMEM)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot store InputCapture pointer barriers");
  return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                "malformed SetPointerBarriers request");
}

static int
inputcapture_method_enable(sd_bus_message *m, void *userdata,
                           sd_bus_error *ret_error)
{
  const char *session_handle;
  const char *app_id;
  struct hevel_ic_session *session = NULL;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "os", &session_handle, &app_id);
  if (r < 0) return r;

  r = inputcapture_skip_options(m);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed Enable options");

  r = inputcapture_require_session(m, session_handle, app_id, &session);
  if (r != 0) return r;

  r = inputcapture_sync_zones_from_compositor(false);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot query compositor zones");

  if (session->eis_fd_issued == false)
    return inputcapture_reply_rejected(m,
                                       "rejecting Enable before ConnectToEIS",
                                       session_handle);
  if (!inputcapture_session_has_barriers(session))
    return inputcapture_reply_rejected(m,
                                       "rejecting Enable without barriers",
                                       session_handle);
  if (session->barriers_stale)
    return inputcapture_reply_rejected(m,
                                       "rejecting Enable with stale barriers",
                                       session_handle);

  inputcapture_transition_enable(session, HEVEL_IC_ENABLED);
  inputcapture_transition_capture(session, HEVEL_IC_INACTIVE);
  return inputcapture_reply_empty(m, HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS);
}

static int
inputcapture_method_disable(sd_bus_message *m, void *userdata,
                            sd_bus_error *ret_error)
{
  const char *session_handle;
  const char *app_id;
  struct hevel_ic_session *session = NULL;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "os", &session_handle, &app_id);
  if (r < 0) return r;

  r = inputcapture_skip_options(m);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed Disable options");

  r = inputcapture_require_session(m, session_handle, app_id, &session);
  if (r != 0) return r;

  inputcapture_disable_session(session, true);
  return inputcapture_reply_empty(m, HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS);
}

static int
inputcapture_method_release(sd_bus_message *m, void *userdata,
                            sd_bus_error *ret_error)
{
  const char *session_handle;
  const char *app_id;
  struct hevel_ic_session *session = NULL;
  struct inputcapture_signal_payload payload = {0};
  uint32_t activation_id = 0;
  double cursor_x = 0.0;
  double cursor_y = 0.0;
  bool have_activation_id = false;
  bool have_cursor = false;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "os", &session_handle, &app_id);
  if (r < 0) return r;

  r = inputcapture_parse_release_options(m, &activation_id,
                                         &have_activation_id, &cursor_x,
                                         &cursor_y, &have_cursor);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed Release options");

  r = inputcapture_require_session(m, session_handle, app_id, &session);
  if (r != 0) return r;

  if (session->capture_state != HEVEL_IC_ACTIVE)
    return inputcapture_reply_rejected(m, "rejecting Release for inactive session",
                                       session_handle);
  if (have_activation_id && session->activation_id != activation_id)
    return inputcapture_reply_rejected(m,
                                       "rejecting Release activation mismatch",
                                       session_handle);

  session->activation_id = have_activation_id ? activation_id
                                              : session->activation_id;
  session->release_cursor_x = cursor_x;
  session->release_cursor_y = cursor_y;
  session->have_release_cursor = have_cursor;

  payload.activation_id = session->activation_id;
  payload.have_activation_id = session->activation_id != 0;
  payload.cursor_x = cursor_x;
  payload.cursor_y = cursor_y;
  payload.have_cursor_position = have_cursor;

  inputcapture_set_session_inactive(session, true, &payload);
  return inputcapture_reply_empty(m, HEVEL_INPUTCAPTURE_RESPONSE_SUCCESS);
}

static int
inputcapture_method_connect_to_eis(sd_bus_message *m, void *userdata,
                                   sd_bus_error *ret_error)
{
  const char *session_handle;
  const char *app_id;
  struct hevel_ic_session *session = NULL;
  uint32_t capabilities;
  int fd;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "os", &session_handle, &app_id);
  if (r < 0) return r;

  r = inputcapture_skip_options(m);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed ConnectToEIS options");

  session = inputcapture_find_session(session_handle);
  if (!session)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "unknown InputCapture session");
  if (strcmp(session->app_id, app_id ? app_id : "") != 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "InputCapture app id mismatch");
  if (session->eis_fd_issued)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "InputCapture EIS fd already issued");

  capabilities = inputcapture_map_eis_capabilities(session->capabilities);
  if (capabilities == 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "InputCapture session has no supported devices");

  fd = inject_open_eis_fd(NULL, session->session_id, capabilities,
                          session->app_id[0] != '\0' ? session->app_id
                                                     : "hevel-inputcapture");
  if (fd < 0)
    return sd_bus_error_set_const(ret_error, inputcapture_portal_error,
                                  "cannot open compositor EIS fd");

  session->eis_fd_issued = true;
  r = sd_bus_reply_method_return(m, "h", fd);
  close(fd);
  return r;
}

static int
inputcapture_property_supported_capabilities(sd_bus *bus, const char *path,
                                             const char *interface,
                                             const char *property,
                                             sd_bus_message *reply,
                                             void *userdata,
                                             sd_bus_error *ret_error)
{
  (void)bus;
  (void)path;
  (void)interface;
  (void)property;
  (void)userdata;
  (void)ret_error;
  return sd_bus_message_append(reply, "u",
                               HEVEL_INPUTCAPTURE_SUPPORTED_CAPABILITIES);
}

static int
inputcapture_property_version(sd_bus *bus, const char *path,
                              const char *interface, const char *property,
                              sd_bus_message *reply, void *userdata,
                              sd_bus_error *ret_error)
{
  (void)bus;
  (void)path;
  (void)interface;
  (void)property;
  (void)userdata;
  (void)ret_error;
  return sd_bus_message_append(reply, "u", HEVEL_INPUTCAPTURE_VERSION);
}

static int
inputcapture_property_session_version(sd_bus *bus, const char *path,
                                      const char *interface,
                                      const char *property,
                                      sd_bus_message *reply, void *userdata,
                                      sd_bus_error *ret_error)
{
  (void)bus;
  (void)path;
  (void)interface;
  (void)property;
  (void)userdata;
  (void)ret_error;
  return sd_bus_message_append(reply, "u",
                               HEVEL_INPUTCAPTURE_SESSION_VERSION);
}

const sd_bus_vtable inputcapture_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CreateSession", "oossa{sv}", "ua{sv}",
                  inputcapture_method_create_session,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetZones", "oosa{sv}", "ua{sv}",
                  inputcapture_method_get_zones,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPointerBarriers", "oosa{sv}aa{sv}u", "ua{sv}",
                  inputcapture_method_set_pointer_barriers,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Enable", "osa{sv}", "ua{sv}", inputcapture_method_enable,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Disable", "osa{sv}", "ua{sv}",
                  inputcapture_method_disable, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Release", "osa{sv}", "ua{sv}",
                  inputcapture_method_release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ConnectToEIS", "osa{sv}", "h",
                  inputcapture_method_connect_to_eis,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Disabled", "oa{sv}", 0),
    SD_BUS_SIGNAL("Activated", "oa{sv}", 0),
    SD_BUS_SIGNAL("Deactivated", "oa{sv}", 0),
    SD_BUS_SIGNAL("ZonesChanged", "oa{sv}", 0),
    SD_BUS_PROPERTY("SupportedCapabilities", "u",
                    inputcapture_property_supported_capabilities, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("version", "u", inputcapture_property_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};
