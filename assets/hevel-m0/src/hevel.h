#ifndef HEVEL_H
#define HEVEL_H

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>

#ifdef __linux__
#include <linux/input-event-codes.h>
#else
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#endif

#include <swc.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "../config.h"
#include "nein_cursor.h"

typedef enum {
  MODE_NONE,
  MODE_KILL,
  MODE_SCROLL,
  MODE_MOVE,
  MODE_RESIZE,
  MODE_JUMP,
  MODE_SELECT,
} chord_mode;

struct window {
  struct swc_window *swc;
  struct wl_list link;

  pid_t pid;
  struct window *spawn_parent;
  struct wl_list spawn_children;
  struct wl_list spawn_link;
  bool hidden_for_spawn;
  struct swc_rectangle saved_geometry;

  bool sticky;
};

struct screen {
  struct swc_screen *swc;
  struct wl_list link;
};

struct compositor_state {
  struct wl_display *display;
  struct wl_event_loop *evloop;
  struct wl_list windows;
  struct wl_list screens;
  struct screen *current_screen;
  struct swc_window *focused;
};
extern struct compositor_state compositor;

struct input_state {
  bool active;
  int32_t move_start_win_x, move_start_win_y;
  int32_t move_start_cursor_x, move_start_cursor_y;
  struct wl_event_source *move_scroll_timer;
  struct wl_event_source *cursor_timer;
  int32_t scroll_drag_last_x, scroll_drag_last_y;
  struct wl_event_source *scroll_drag_timer;
  bool spawn_pending;
  struct swc_rectangle spawn_geometry;
};
extern struct input_state input;

struct chord_state {
  chord_mode mode;
  bool left, middle, right;
  bool pending;
  bool forwarded;
  uint32_t button;
  uint32_t time;
  struct wl_event_source *timer;
};
extern struct chord_state chord;

struct scroll_state {
  int32_t pending_px, pending_px_x;
  int32_t rem, rem_x;
  int8_t cursor_dir;
  bool active;
  bool auto_scrolling;
  struct wl_event_source *timer;
};
extern struct scroll_state scroll;

struct zoom_state {
  float target;
  struct wl_event_source *timer;
};
extern struct zoom_state zoom;

struct sel_state {
  bool selecting;
  int32_t start_x, start_y;
  int32_t cur_x, cur_y;
  struct wl_event_source *timer;
};
extern struct sel_state sel;

struct eis;
struct eis_client;
struct eis_device;
struct eis_seat;

struct hevel_eis_request {
  struct wl_list link;
  uint32_t id;
  uint32_t capabilities;
  char session_id[128];
  char name[64];
};

struct hevel_eis_client {
  struct wl_list link;
  struct eis_client *client;
  struct hevel_eis_request *request;
  struct eis_seat *seat;
  struct eis_device *keyboard;
  struct eis_device *pointer;
  struct eis_device *pointer_abs;
  int32_t absolute_origin_x;
  int32_t absolute_origin_y;
  bool keyboard_active;
  bool pointer_active;
  bool pointer_abs_active;
};

struct portal_state {
  bool available;
};
extern struct portal_state portal;

struct eis_state {
  struct eis *ctx;
  int fd;
  struct wl_event_source *source;
  struct wl_list requests;
  struct wl_list clients;
  uint32_t next_request_id;
  bool available;
};
extern struct eis_state eis_state;

int portal_initialize(void);
void portal_finalize(void);
int portal_service_main(void);

int eis_initialize(void);
void eis_finalize(void);
int eis_open_client_fd(const char *session_id, uint32_t capabilities,
                       const char *name);
int eis_cli_main(int argc, char **argv);

int inject_initialize(const char *display_name);
void inject_finalize(void);
int inject_cli_main(int argc, char **argv);

extern const int timerms;

#endif
