#include "inputcapture.h"

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

static int
inputcapture_skip_options(sd_bus_message *m)
{
  int r;

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char *contents = NULL;
    const char *key = NULL;
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
    const char *contents = NULL;
    const char *key = NULL;
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
    const char *contents = NULL;
    const char *key = NULL;
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
    const char *contents = NULL;
    const char *key = NULL;
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
  int32_t cursor;
  int32_t end;
  int32_t start;

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
