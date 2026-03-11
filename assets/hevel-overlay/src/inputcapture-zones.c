#include "inputcapture.h"

#include <time.h>

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
  struct hevel_ic_zone *left_zone;
  struct hevel_ic_zone *right_zone;
  struct wl_list *left_link;
  struct wl_list *right_link;

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

static int
inputcapture_refresh_local_zones(struct wl_list *new_zones,
                                 uint32_t zone_set_serial, bool emit_changes)
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
    struct hevel_ic_zone *zone = wl_container_of(new_zones->next, zone, link);
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
