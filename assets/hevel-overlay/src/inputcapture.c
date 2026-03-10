#include "hevel.h"

#include <errno.h>

#include <systemd/sd-bus.h>

#define HEVEL_INPUTCAPTURE_RESPONSE_OTHER 2U
#define HEVEL_INPUTCAPTURE_VERSION 1U

struct inputcapture_state inputcapture = {0};

static void
inputcapture_destroy_requests(void)
{
  struct hevel_ic_request *request, *tmp;

  if (!inputcapture.requests.next) return;

  wl_list_for_each_safe(request, tmp, &inputcapture.requests, link)
  {
    wl_list_remove(&request->link);
    wl_list_init(&request->link);
    if (request->slot) {
      sd_bus_slot_unref(request->slot);
      request->slot = NULL;
    }
    free(request);
  }
}

static void
inputcapture_destroy_sessions(void)
{
  struct hevel_ic_session *session, *tmp;

  if (!inputcapture.sessions.next) return;

  wl_list_for_each_safe(session, tmp, &inputcapture.sessions, link)
  {
    wl_list_remove(&session->link);
    wl_list_init(&session->link);
    if (session->slot) {
      sd_bus_slot_unref(session->slot);
      session->slot = NULL;
    }
    free(session);
  }
}

static void
inputcapture_destroy_barriers(void)
{
  struct hevel_ic_barrier *barrier, *tmp;

  if (!inputcapture.barriers.next) return;

  wl_list_for_each_safe(barrier, tmp, &inputcapture.barriers, link)
  {
    wl_list_remove(&barrier->link);
    wl_list_init(&barrier->link);
    free(barrier);
  }
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

int
inputcapture_initialize(void)
{
  if (inputcapture.initialized) return 0;

  wl_list_init(&inputcapture.requests);
  wl_list_init(&inputcapture.sessions);
  wl_list_init(&inputcapture.zones);
  wl_list_init(&inputcapture.barriers);
  inputcapture.barrier_owner = NULL;
  inputcapture.zone_set_serial = 0;
  inputcapture.installed_barrier_zone_serial = 0;
  inputcapture.barriers_stale = false;
  inputcapture.initialized = true;
  return 0;
}

void
inputcapture_finalize(void)
{
  if (!inputcapture.initialized) return;

  inputcapture_destroy_requests();
  inputcapture_destroy_sessions();
  inputcapture_destroy_barriers();
  inputcapture_destroy_zone_list(&inputcapture.zones);

  wl_list_init(&inputcapture.requests);
  wl_list_init(&inputcapture.sessions);
  wl_list_init(&inputcapture.zones);
  wl_list_init(&inputcapture.barriers);
  inputcapture.barrier_owner = NULL;
  inputcapture.zone_set_serial = 0;
  inputcapture.installed_barrier_zone_serial = 0;
  inputcapture.barriers_stale = false;
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

  inputcapture_destroy_zone_list(&inputcapture.zones);
  wl_list_init(&inputcapture.zones);

  while (!wl_list_empty(&new_zones)) {
    struct hevel_ic_zone *zone =
        wl_container_of(new_zones.next, zone, link);
    wl_list_remove(&zone->link);
    wl_list_insert(inputcapture.zones.prev, &zone->link);
  }

  if (inputcapture.zone_set_serial < UINT32_MAX)
    ++inputcapture.zone_set_serial;

  if (!wl_list_empty(&inputcapture.barriers) &&
      inputcapture.installed_barrier_zone_serial !=
          inputcapture.zone_set_serial)
    inputcapture.barriers_stale = true;
}

static int
inputcapture_reply_response(sd_bus_message *m, uint32_t response)
{
  sd_bus_message *reply = NULL;
  int r;

  r = sd_bus_message_new_method_return(m, &reply);
  if (r < 0) return r;

  r = sd_bus_message_append(reply, "u", response);
  if (r < 0) goto out;

  r = sd_bus_message_open_container(reply, 'a', "{sv}");
  if (r < 0) goto out;

  r = sd_bus_message_close_container(reply);
  if (r < 0) goto out;

  r = sd_bus_send(sd_bus_message_get_bus(m), reply, NULL);

out:
  sd_bus_message_unref(reply);
  return r;
}

static int
inputcapture_method_cancelled(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error)
{
  (void)userdata;
  (void)ret_error;
  return inputcapture_reply_response(m, HEVEL_INPUTCAPTURE_RESPONSE_OTHER);
}

static int
inputcapture_connect_to_eis(sd_bus_message *m, void *userdata,
                            sd_bus_error *ret_error)
{
  (void)m;
  (void)userdata;
  return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_NOT_SUPPORTED,
                                "hevel InputCapture is not implemented yet");
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
  return sd_bus_message_append(reply, "u", 0U);
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

const sd_bus_vtable inputcapture_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CreateSession", "oossa{sv}", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetZones", "oosa{sv}", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPointerBarriers", "oosa{sv}aa{sv}u", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Enable", "osa{sv}", "ua{sv}", inputcapture_method_cancelled,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Disable", "osa{sv}", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Release", "osa{sv}", "ua{sv}", inputcapture_method_cancelled,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ConnectToEIS", "osa{sv}", "h", inputcapture_connect_to_eis,
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
