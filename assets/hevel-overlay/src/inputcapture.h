#ifndef HEVEL_INPUTCAPTURE_H
#define HEVEL_INPUTCAPTURE_H

#include <errno.h>
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

#define HEVEL_INPUTCAPTURE_REQUEST_INTERFACE                                   \
  "org.freedesktop.impl.portal.Request"
#define HEVEL_INPUTCAPTURE_SESSION_INTERFACE                                   \
  "org.freedesktop.impl.portal.Session"
#define HEVEL_INPUTCAPTURE_OBJECT_PATH                                         \
  "/org/freedesktop/portal/desktop"
#define HEVEL_INPUTCAPTURE_PORTAL_ERROR "org.freedesktop.portal.Error.Failed"

enum hevel_ic_enable_state {
  HEVEL_IC_DISABLED,
  HEVEL_IC_ENABLED,
};

enum hevel_ic_capture_state {
  HEVEL_IC_INACTIVE,
  HEVEL_IC_ACTIVE,
};

struct hevel_ic_request {
  struct wl_list link;
  char handle[256];
  char session_handle[256];
  char app_id[128];
  struct sd_bus_slot *slot;
};

struct hevel_ic_session {
  struct wl_list link;
  struct wl_list barriers;
  char session_handle[256];
  char session_id[128];
  char app_id[128];
  char parent_window[256];
  uint32_t capabilities;
  enum hevel_ic_enable_state enable_state;
  enum hevel_ic_capture_state capture_state;
  uint32_t activation_id;
  uint32_t barrier_zone_serial;
  double release_cursor_x;
  double release_cursor_y;
  bool barriers_stale;
  bool have_release_cursor;
  bool eis_fd_issued;
  struct sd_bus_slot *slot;
};

struct hevel_ic_zone {
  struct wl_list link;
  uint32_t id;
  struct swc_rectangle geometry;
};

struct hevel_ic_barrier {
  struct wl_list link;
  uint32_t id;
  uint32_t zone_id;
  uint32_t zone_serial;
  int32_t x1;
  int32_t y1;
  int32_t x2;
  int32_t y2;
  struct hevel_ic_session *owner;
};

struct inputcapture_state {
  struct wl_list requests;
  struct wl_list sessions;
  struct wl_list zones;
  struct sd_event_source *zone_watch_source;
  uint32_t zone_set_serial;
  bool initialized;
};
extern struct inputcapture_state inputcapture;

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

typedef int (*inputcapture_dict_appender)(sd_bus_message *message,
                                          void *userdata);

static int inputcapture_method_close_request(sd_bus_message *m, void *userdata,
                                             sd_bus_error *ret_error);
static int inputcapture_method_close_session(sd_bus_message *m, void *userdata,
                                             sd_bus_error *ret_error);
static int inputcapture_method_create_session(sd_bus_message *m, void *userdata,
                                              sd_bus_error *ret_error);
static int inputcapture_method_get_zones(sd_bus_message *m, void *userdata,
                                         sd_bus_error *ret_error);
static int inputcapture_method_set_pointer_barriers(sd_bus_message *m,
                                                    void *userdata,
                                                    sd_bus_error *ret_error);
static int inputcapture_method_enable(sd_bus_message *m, void *userdata,
                                      sd_bus_error *ret_error);
static int inputcapture_method_disable(sd_bus_message *m, void *userdata,
                                       sd_bus_error *ret_error);
static int inputcapture_method_release(sd_bus_message *m, void *userdata,
                                       sd_bus_error *ret_error);
static int inputcapture_method_connect_to_eis(sd_bus_message *m, void *userdata,
                                              sd_bus_error *ret_error);

static int inputcapture_property_supported_capabilities(sd_bus *bus,
                                                        const char *path,
                                                        const char *interface,
                                                        const char *property,
                                                        sd_bus_message *reply,
                                                        void *userdata,
                                                        sd_bus_error *ret_error);
static int inputcapture_property_version(sd_bus *bus, const char *path,
                                         const char *interface,
                                         const char *property,
                                         sd_bus_message *reply,
                                         void *userdata,
                                         sd_bus_error *ret_error);
static int inputcapture_property_session_version(sd_bus *bus, const char *path,
                                                 const char *interface,
                                                 const char *property,
                                                 sd_bus_message *reply,
                                                 void *userdata,
                                                 sd_bus_error *ret_error);

static void inputcapture_log(const char *message, const char *handle,
                             const char *detail);
static int inputcapture_append_dict_u32(sd_bus_message *reply, const char *key,
                                        uint32_t value);
static int inputcapture_append_dict_string(sd_bus_message *reply,
                                           const char *key,
                                           const char *value);
static int inputcapture_append_dict_cursor_position(sd_bus_message *reply,
                                                    const char *key, double x,
                                                    double y);
static int inputcapture_append_dict_zones(sd_bus_message *reply,
                                          const char *key,
                                          struct wl_list *zones);
static int inputcapture_append_dict_failed_barriers(sd_bus_message *reply,
                                                    const char *key,
                                                    const uint32_t *failed_ids,
                                                    size_t failed_count);
static int inputcapture_append_session_created(sd_bus_message *reply,
                                               void *userdata);
static int inputcapture_append_zones_results(sd_bus_message *reply,
                                             void *userdata);
static int inputcapture_append_failed_barriers_results(sd_bus_message *reply,
                                                       void *userdata);
static int inputcapture_append_signal_payload(sd_bus_message *reply,
                                              void *userdata);
static int inputcapture_send_response(sd_bus_message *m, uint32_t response,
                                      inputcapture_dict_appender append,
                                      void *userdata);
static int inputcapture_reply_empty(sd_bus_message *m, uint32_t response);
static int inputcapture_reply_rejected(sd_bus_message *m, const char *context,
                                       const char *detail);
static int inputcapture_emit_signal(struct hevel_ic_session *session,
                                    const char *member,
                                    inputcapture_dict_appender append,
                                    void *userdata);
static void inputcapture_emit_zones_changed_all(void);
static void inputcapture_emit_disabled(struct hevel_ic_session *session);
static void
inputcapture_emit_deactivated(struct hevel_ic_session *session,
                              const struct inputcapture_signal_payload *payload);

static void inputcapture_destroy_barrier(struct hevel_ic_barrier *barrier);
static void inputcapture_destroy_barrier_list(struct wl_list *barriers);
static int inputcapture_skip_options(sd_bus_message *m);
static int inputcapture_parse_create_session_options(sd_bus_message *m,
                                                     uint32_t *capabilities,
                                                     bool *have_capabilities);
static int inputcapture_parse_release_options(sd_bus_message *m,
                                              uint32_t *activation_id,
                                              bool *have_activation_id,
                                              double *cursor_x,
                                              double *cursor_y,
                                              bool *have_cursor);
static int inputcapture_parse_barrier_entry(sd_bus_message *m,
                                            struct hevel_ic_barrier *barrier);
static bool inputcapture_barrier_id_in_list(struct wl_list *barriers,
                                            uint32_t id);
static bool
inputcapture_barrier_segment_valid(const struct hevel_ic_barrier *barrier);
static const struct hevel_ic_zone *
inputcapture_find_barrier_zone(const struct hevel_ic_barrier *barrier);
static int inputcapture_append_failed_id(uint32_t **failed_ids,
                                         size_t *failed_count,
                                         uint32_t barrier_id);

static void inputcapture_destroy_zone_list(struct wl_list *zones);
static bool inputcapture_zone_lists_equal(struct wl_list *left,
                                          struct wl_list *right);
static struct hevel_ic_zone *inputcapture_new_zone(
    const struct swc_screen *screen, uint32_t zone_id);
static int inputcapture_refresh_local_zones(struct wl_list *new_zones,
                                            uint32_t zone_set_serial,
                                            bool emit_changes);
static int inputcapture_sync_zones_from_compositor(bool emit_changes);
static int inputcapture_zone_watch(sd_event_source *source, uint64_t usec,
                                   void *userdata);
static void inputcapture_update_zone_watch(void);

static struct hevel_ic_request *inputcapture_find_request(const char *handle);
static struct hevel_ic_session *inputcapture_find_session(
    const char *session_handle);
static int inputcapture_generate_session_id(char *buffer, size_t buffer_size);
static uint32_t inputcapture_map_eis_capabilities(uint32_t capabilities);
static int inputcapture_new_request(const char *handle,
                                    const char *session_handle,
                                    const char *app_id,
                                    struct hevel_ic_request **out_request);
static int inputcapture_new_session(const char *session_handle,
                                    const char *app_id,
                                    const char *parent_window,
                                    uint32_t capabilities,
                                    struct hevel_ic_session **out_session);
static int inputcapture_require_session(sd_bus_message *m,
                                        const char *session_handle,
                                        const char *app_id,
                                        struct hevel_ic_session **out_session);
static void inputcapture_transition_enable(struct hevel_ic_session *session,
                                           enum hevel_ic_enable_state state);
static void inputcapture_transition_capture(struct hevel_ic_session *session,
                                            enum hevel_ic_capture_state state);
static bool inputcapture_session_has_barriers(
    const struct hevel_ic_session *session);
static void
inputcapture_set_session_inactive(struct hevel_ic_session *session,
                                  bool keep_enabled,
                                  const struct inputcapture_signal_payload *payload);
static void inputcapture_disable_session(struct hevel_ic_session *session,
                                         bool emit_signal);
static void inputcapture_destroy_request(struct hevel_ic_request *request);
static void inputcapture_destroy_requests(void);
static void inputcapture_destroy_session(struct hevel_ic_session *session,
                                         bool emit_closed);
static void inputcapture_destroy_sessions(void);

#endif
