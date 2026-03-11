#include "inputcapture.h"

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
  const char *app_id;
  const char *handle;
  const char *parent_window;
  const char *session_handle;
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
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
  const char *app_id;
  const char *handle;
  const char *session_handle;
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "cannot export InputCapture request object");

  r = inputcapture_require_session(m, session_handle, app_id, &session);
  if (r != 0) {
    inputcapture_destroy_request(request);
    return r;
  }

  r = inputcapture_sync_zones_from_compositor(false);
  if (r < 0) {
    inputcapture_destroy_request(request);
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
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
  const char *app_id;
  const char *handle;
  const char *session_handle;
  struct hevel_ic_request *request = NULL;
  struct hevel_ic_session *session = NULL;
  struct hevel_ic_barrier *barrier, *tmp;
  struct inputcapture_failed_barriers_payload payload;
  struct wl_list accepted_barriers;
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "cannot store InputCapture pointer barriers");
  return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                "malformed SetPointerBarriers request");
}

static int
inputcapture_method_enable(sd_bus_message *m, void *userdata,
                           sd_bus_error *ret_error)
{
  const char *app_id;
  const char *session_handle;
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
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
  const char *app_id;
  const char *session_handle;
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
  const char *app_id;
  const char *session_handle;
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
  const char *app_id;
  const char *session_handle;
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
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "unknown InputCapture session");
  if (strcmp(session->app_id, app_id ? app_id : "") != 0)
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "InputCapture app id mismatch");
  if (session->eis_fd_issued)
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "InputCapture EIS fd already issued");

  capabilities = inputcapture_map_eis_capabilities(session->capabilities);
  if (capabilities == 0)
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "InputCapture session has no supported devices");

  fd = inject_open_eis_fd(NULL, session->session_id, capabilities,
                          session->app_id[0] != '\0' ? session->app_id
                                                     : "hevel-inputcapture");
  if (fd < 0)
    return sd_bus_error_set_const(ret_error, HEVEL_INPUTCAPTURE_PORTAL_ERROR,
                                  "cannot open compositor EIS fd");

  session->eis_fd_issued = true;
  r = sd_bus_reply_method_return(m, "h", fd);
  close(fd);
  return r;
}
