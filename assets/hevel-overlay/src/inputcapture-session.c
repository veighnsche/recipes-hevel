#include "inputcapture.h"

#include <libeis.h>

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
                               HEVEL_INPUTCAPTURE_REQUEST_INTERFACE,
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
                               HEVEL_INPUTCAPTURE_SESSION_INTERFACE,
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
  const bool was_active = session->capture_state == HEVEL_IC_ACTIVE;

  inputcapture_transition_capture(session, HEVEL_IC_INACTIVE);
  if (!keep_enabled) inputcapture_transition_enable(session, HEVEL_IC_DISABLED);

  if (was_active) inputcapture_emit_deactivated(session, payload);
}

static void
inputcapture_disable_session(struct hevel_ic_session *session, bool emit_signal)
{
  const bool was_enabled = session->enable_state == HEVEL_IC_ENABLED;

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
                       HEVEL_INPUTCAPTURE_SESSION_INTERFACE, "Closed", "");

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
