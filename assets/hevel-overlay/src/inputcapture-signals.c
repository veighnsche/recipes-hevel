#include "inputcapture.h"

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
                           inputcapture_dict_appender append, void *userdata)
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

  r = sd_bus_message_new_signal(portal.bus, &signal,
                                HEVEL_INPUTCAPTURE_OBJECT_PATH,
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
