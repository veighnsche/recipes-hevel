#include "hevel.h"

#include <errno.h>
#include <fcntl.h>
#include <libei.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

#include <systemd/sd-bus.h>

#ifndef SO_PEERCRED
#define SO_PEERCRED 17
#endif

static int inject_fd = -1;
static struct wl_event_source *inject_source = NULL;
static char inject_socket_path[256] = {0};
static sd_bus *inject_bus = NULL;
static const char *inject_portal_bus_name =
    "org.freedesktop.impl.portal.desktop.hevel";

struct inject_peer_cred {
  pid_t pid;
  uid_t uid;
  gid_t gid;
};

static uint32_t
inject_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;

  return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static int
inject_set_nonblock(int fd)
{
  int flags = fcntl(fd, F_GETFL);

  if (flags < 0) return -1;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

  return 0;
}

static int
inject_parse_u32(const char *value, uint32_t *out)
{
  char *end = NULL;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(value, &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX) return -1;

  *out = (uint32_t)parsed;
  return 0;
}

static int
inject_parse_i32(const char *value, int32_t *out)
{
  char *end = NULL;
  long parsed;

  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed < INT32_MIN ||
      parsed > INT32_MAX)
    return -1;

  *out = (int32_t)parsed;
  return 0;
}

static int
inject_resolve_socket_path(char *path, size_t path_size, const char *display)
{
  const char *override = getenv("HEVEL_INJECT_SOCKET");
  const char *runtime_dir;
  int written;

  if (override && override[0] != '\0') {
    written = snprintf(path, path_size, "%s", override);
    return (written < 0 || (size_t)written >= path_size) ? -1 : 0;
  }

  runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (!runtime_dir || !display || display[0] == '\0') return -1;

  written = snprintf(path, path_size, "%s/hevel-inject-%s.sock", runtime_dir,
                     display);
  return (written < 0 || (size_t)written >= path_size) ? -1 : 0;
}

static int
inject_send_reply(int fd, const char *reply)
{
  size_t remaining = strlen(reply);
  const char *cursor = reply;

  while (remaining > 0) {
    ssize_t written = write(fd, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    remaining -= (size_t)written;
    cursor += written;
  }

  return 0;
}

static int
inject_send_fd(int socket_fd, int send_fd, const char *reply)
{
  struct msghdr msg = {0};
  struct iovec iov = {.iov_base = (void *)reply, .iov_len = strlen(reply)};
  char control[CMSG_SPACE(sizeof(send_fd))];
  struct cmsghdr *cmsg;
  ssize_t written;

  memset(control, 0, sizeof(control));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(send_fd));
  memcpy(CMSG_DATA(cmsg), &send_fd, sizeof(send_fd));
  msg.msg_controllen = cmsg->cmsg_len;

  do {
    written = sendmsg(socket_fd, &msg, 0);
  } while (written < 0 && errno == EINTR);

  return written < 0 ? -1 : 0;
}

static int
inject_open_user_bus(void)
{
  int r;

  if (inject_bus) return 0;

  r = sd_bus_default_user(&inject_bus);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot open user bus for socket authorization: %s\n",
            strerror(-r));
    inject_bus = NULL;
  }

  return r;
}

static int
inject_get_portal_owner_pid(pid_t *pid)
{
  sd_bus_creds *creds = NULL;
  int r;

  if (!pid) return -EINVAL;

  r = inject_open_user_bus();
  if (r < 0) return r;

  r = sd_bus_get_name_creds(inject_bus, inject_portal_bus_name,
                            SD_BUS_CREDS_PID, &creds);
  if (r < 0) return r;

  r = sd_bus_creds_get_pid(creds, pid);
  sd_bus_creds_unref(creds);
  return r;
}

static int
inject_get_peer_pid(int fd, pid_t *pid)
{
  struct inject_peer_cred cred = {0};
  socklen_t length = sizeof(cred);

  if (!pid) return -EINVAL;

  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &length) < 0)
    return -errno;
  if (length < sizeof(cred)) return -EIO;

  *pid = cred.pid;
  return 0;
}

static bool
inject_authorize_portal_peer(int fd, const char *command)
{
  pid_t peer_pid = 0;
  pid_t owner_pid = 0;
  int r;

  r = inject_get_peer_pid(fd, &peer_pid);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot determine peer pid for %s: %s\n", command,
            strerror(-r));
    return false;
  }

  r = inject_get_portal_owner_pid(&owner_pid);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot resolve portal owner pid for %s: %s\n",
            command, strerror(-r));
    return false;
  }

  if (peer_pid != owner_pid) {
    fprintf(stderr,
            "hevel: rejecting unauthorized socket command %s from pid %ld "
            "(portal pid %ld)\n",
            command, (long)peer_pid, (long)owner_pid);
    return false;
  }

  return true;
}

static int
inject_connect_socket(const char *socket_path)
{
  struct sockaddr_un addr = {0};
  int fd;

  if (strlen(socket_path) >= sizeof(addr.sun_path)) {
    fprintf(stderr, "hevel: injection socket path too long\n");
    return -1;
  }

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, socket_path);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}

static int
inject_execute_command(char *buffer, char *error, size_t error_size)
{
  char *argv[2] = {0};
  char *save = NULL;
  int argc = 0;

  for (char *token = strtok_r(buffer, " \t\r\n", &save);
       token && argc < (int)(sizeof(argv) / sizeof(argv[0]));
       token = strtok_r(NULL, " \t\r\n", &save)) {
    argv[argc++] = token;
  }

  if (argc == 0) {
    snprintf(error, error_size, "empty command");
    return -1;
  }

  if (strcmp(argv[0], "ping") == 0) return 0;

  snprintf(error, error_size,
           "raw injection commands are disabled; use the portal path");
  return -1;
}

static void
inject_parse_spawn_prepare(char *buffer, uint32_t *width, uint32_t *height,
                           char *error, size_t error_size)
{
  char *argv[4] = {0};
  char *save = NULL;
  int argc = 0;

  for (char *token = strtok_r(buffer, " \t\r\n", &save);
       token && argc < (int)(sizeof(argv) / sizeof(argv[0]));
       token = strtok_r(NULL, " \t\r\n", &save)) {
    argv[argc++] = token;
  }

  if (argc != 3 || inject_parse_u32(argv[1], width) < 0 ||
      inject_parse_u32(argv[2], height) < 0) {
    snprintf(error, error_size, "usage: spawn-prepare <width> <height>");
    return;
  }

  error[0] = '\0';
}

static int
inject_prepare_spawn(uint32_t width, uint32_t height)
{
  struct screen *screen = compositor.current_screen;

  if (!screen && !wl_list_empty(&compositor.screens))
    screen = wl_container_of(compositor.screens.next, screen, link);
  if (!screen || !screen->swc) return -ENODEV;

  if (width < 320) width = 320;
  if (height < 160) height = 160;
  if (width > screen->swc->geometry.width) width = screen->swc->geometry.width;
  if (height > screen->swc->geometry.height)
    height = screen->swc->geometry.height;

  input.spawn_pending = true;
  input.spawn_geometry.width = width;
  input.spawn_geometry.height = height;
  input.spawn_geometry.x =
      screen->swc->geometry.x +
      ((int32_t)screen->swc->geometry.width - (int32_t)width) / 2;
  input.spawn_geometry.y =
      screen->swc->geometry.y +
      ((int32_t)screen->swc->geometry.height - (int32_t)height) / 2;

  return 0;
}

static void
inject_cancel_spawn(void)
{
  input.spawn_pending = false;
  memset(&input.spawn_geometry, 0, sizeof(input.spawn_geometry));
}

static void
inject_parse_eis_open(char *buffer, char *session_id, size_t session_id_size,
                      uint32_t *capabilities, char *name, size_t name_size,
                      char *error, size_t error_size)
{
  char *argv[4] = {0};
  char *save = NULL;
  int argc = 0;

  for (char *token = strtok_r(buffer, " \t\r\n", &save);
       token && argc < (int)(sizeof(argv) / sizeof(argv[0]));
       token = strtok_r(NULL, " \t\r\n", &save)) {
    argv[argc++] = token;
  }

  if (argc != 4 || inject_parse_u32(argv[2], capabilities) < 0 ||
      snprintf(session_id, session_id_size, "%s", argv[1]) >=
          (int)session_id_size ||
      snprintf(name, name_size, "%s", argv[3]) >= (int)name_size) {
    snprintf(error, error_size, "usage: eis-open <session-id> <capabilities> <name>");
    return;
  }

  error[0] = '\0';
}

static void
inject_parse_eis_close(char *buffer, char *session_id, size_t session_id_size,
                       char *error, size_t error_size)
{
  char *argv[3] = {0};
  char *save = NULL;
  int argc = 0;

  for (char *token = strtok_r(buffer, " \t\r\n", &save);
       token && argc < (int)(sizeof(argv) / sizeof(argv[0]));
       token = strtok_r(NULL, " \t\r\n", &save)) {
    argv[argc++] = token;
  }

  if (argc != 2 ||
      snprintf(session_id, session_id_size, "%s", argv[1]) >=
          (int)session_id_size) {
    snprintf(error, error_size, "usage: eis-close <session-id>");
    return;
  }

  error[0] = '\0';
}

static void
inject_parse_spawn_cancel(char *buffer, char *error, size_t error_size)
{
  char *argv[3] = {0};
  char *save = NULL;
  int argc = 0;

  for (char *token = strtok_r(buffer, " \t\r\n", &save);
       token && argc < (int)(sizeof(argv) / sizeof(argv[0]));
       token = strtok_r(NULL, " \t\r\n", &save)) {
    argv[argc++] = token;
  }

  if (argc != 1) {
    snprintf(error, error_size, "usage: spawn-cancel");
    return;
  }

  error[0] = '\0';
}

static void
inject_handle_client(int client_fd)
{
  char buffer[256];
  char error[128];
  ssize_t amount;

  amount = read(client_fd, buffer, sizeof(buffer) - 1);
  if (amount < 0) {
    if (errno == EINTR) return;
    inject_send_reply(client_fd, "error read failed\n");
    return;
  }
  if (amount == 0) {
    inject_send_reply(client_fd, "error empty command\n");
    return;
  }

  buffer[amount] = '\0';
  if (strncmp(buffer, "eis-open", 8) == 0 &&
      (buffer[8] == '\0' || buffer[8] == '\n' || buffer[8] == ' ' ||
       buffer[8] == '\t' || buffer[8] == '\r')) {
    char session_id[128];
    char name[64];
    uint32_t capabilities = 0;
    int eis_fd;

    if (!inject_authorize_portal_peer(client_fd, "eis-open")) {
      inject_send_reply(client_fd, "error unauthorized command\n");
      return;
    }

    inject_parse_eis_open(buffer, session_id, sizeof(session_id), &capabilities,
                          name, sizeof(name), error, sizeof(error));
    if (error[0] != '\0') {
      dprintf(client_fd, "error %s\n", error);
      return;
    }

    eis_fd =
        eis_open_client_fd(session_id[0] != '\0' ? session_id : NULL,
                           capabilities, name);

    if (eis_fd < 0) {
      dprintf(client_fd, "error cannot open EIS client fd: %s\n",
              strerror(-eis_fd));
      return;
    }

    if (inject_send_fd(client_fd, eis_fd, "ok\n") < 0)
      inject_send_reply(client_fd, "error cannot send fd\n");
    close(eis_fd);
    return;
  }

  if (strncmp(buffer, "spawn-prepare", 13) == 0 &&
      (buffer[13] == '\0' || buffer[13] == '\n' || buffer[13] == ' ' ||
       buffer[13] == '\t' || buffer[13] == '\r')) {
    uint32_t width = 0;
    uint32_t height = 0;
    int r;

    if (!inject_authorize_portal_peer(client_fd, "spawn-prepare")) {
      inject_send_reply(client_fd, "error unauthorized command\n");
      return;
    }

    inject_parse_spawn_prepare(buffer, &width, &height, error,
                               sizeof(error));
    if (error[0] != '\0') {
      dprintf(client_fd, "error %s\n", error);
      return;
    }

    r = inject_prepare_spawn(width, height);
    if (r < 0) {
      dprintf(client_fd, "error cannot prepare spawn: %s\n", strerror(-r));
      return;
    }

    inject_send_reply(client_fd, "ok\n");
    return;
  }

  if (strncmp(buffer, "eis-close", 9) == 0 &&
      (buffer[9] == '\0' || buffer[9] == '\n' || buffer[9] == ' ' ||
       buffer[9] == '\t' || buffer[9] == '\r')) {
    char session_id[128];
    int r;

    if (!inject_authorize_portal_peer(client_fd, "eis-close")) {
      inject_send_reply(client_fd, "error unauthorized command\n");
      return;
    }

    inject_parse_eis_close(buffer, session_id, sizeof(session_id), error,
                           sizeof(error));
    if (error[0] != '\0') {
      dprintf(client_fd, "error %s\n", error);
      return;
    }

    r = eis_close_session(session_id);
    if (r < 0) {
      dprintf(client_fd, "error cannot close EIS session: %s\n", strerror(-r));
      return;
    }

    inject_send_reply(client_fd, "ok\n");
    return;
  }

  if (strncmp(buffer, "spawn-cancel", 12) == 0 &&
      (buffer[12] == '\0' || buffer[12] == '\n' || buffer[12] == ' ' ||
       buffer[12] == '\t' || buffer[12] == '\r')) {
    if (!inject_authorize_portal_peer(client_fd, "spawn-cancel")) {
      inject_send_reply(client_fd, "error unauthorized command\n");
      return;
    }

    inject_parse_spawn_cancel(buffer, error, sizeof(error));
    if (error[0] != '\0') {
      dprintf(client_fd, "error %s\n", error);
      return;
    }

    inject_cancel_spawn();
    inject_send_reply(client_fd, "ok\n");
    return;
  }

  if (inject_execute_command(buffer, error, sizeof(error)) < 0) {
    dprintf(client_fd, "error %s\n", error);
    return;
  }

  inject_send_reply(client_fd, "ok\n");
}

static int
inject_accept(int fd, uint32_t mask, void *data)
{
  (void)data;

  if ((mask & WL_EVENT_READABLE) == 0) return 0;

  for (;;) {
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      perror("hevel: accept");
      return 0;
    }

    inject_handle_client(client_fd);
    close(client_fd);
  }
}

int
inject_initialize(const char *display_name)
{
  struct sockaddr_un addr = {0};
  int fd;

  if (inject_resolve_socket_path(inject_socket_path, sizeof(inject_socket_path),
                                 display_name) < 0) {
    fprintf(stderr, "hevel: cannot resolve injection socket path\n");
    inject_socket_path[0] = '\0';
    return -1;
  }

  if (strlen(inject_socket_path) >= sizeof(addr.sun_path)) {
    fprintf(stderr, "hevel: injection socket path too long\n");
    inject_socket_path[0] = '\0';
    return -1;
  }

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("hevel: socket");
    inject_socket_path[0] = '\0';
    return -1;
  }

  if (inject_set_nonblock(fd) < 0) {
    perror("hevel: fcntl");
    close(fd);
    unlink(inject_socket_path);
    inject_socket_path[0] = '\0';
    return -1;
  }

  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, inject_socket_path);

  unlink(inject_socket_path);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("hevel: bind");
    close(fd);
    unlink(inject_socket_path);
    inject_socket_path[0] = '\0';
    return -1;
  }

  if (chmod(inject_socket_path, 0600) < 0) {
    perror("hevel: chmod");
    close(fd);
    unlink(inject_socket_path);
    inject_socket_path[0] = '\0';
    return -1;
  }

  if (listen(fd, 4) < 0) {
    perror("hevel: listen");
    close(fd);
    unlink(inject_socket_path);
    inject_socket_path[0] = '\0';
    return -1;
  }

  inject_source =
      wl_event_loop_add_fd(compositor.evloop, fd, WL_EVENT_READABLE,
                           inject_accept, NULL);
  if (!inject_source) {
    fprintf(stderr, "hevel: cannot add injection fd to event loop\n");
    close(fd);
    unlink(inject_socket_path);
    inject_socket_path[0] = '\0';
    return -1;
  }

  inject_fd = fd;
  return 0;
}

void
inject_finalize(void)
{
  if (inject_source) {
    wl_event_source_remove(inject_source);
    inject_source = NULL;
  }

  if (inject_fd >= 0) {
    close(inject_fd);
    inject_fd = -1;
  }

  if (inject_socket_path[0] != '\0') {
    unlink(inject_socket_path);
    inject_socket_path[0] = '\0';
  }

  if (inject_bus) {
    sd_bus_unref(inject_bus);
    inject_bus = NULL;
  }
}

static int
inject_build_command(char *buffer, size_t buffer_size, int argc, char **argv)
{
  size_t used = 0;

  for (int i = 0; i < argc; ++i) {
    int written;

    written = snprintf(buffer + used, buffer_size - used, "%s%s",
                       i == 0 ? "" : " ", argv[i]);
    if (written < 0 || (size_t)written >= buffer_size - used) return -1;
    used += (size_t)written;
  }

  if (used + 2 > buffer_size) return -1;
  buffer[used++] = '\n';
  buffer[used] = '\0';

  return 0;
}

static int
inject_send_command(const char *socket_path, const char *command)
{
  char response[256] = {0};
  int fd;
  ssize_t amount;

  fd = inject_connect_socket(socket_path);
  if (fd < 0) {
    perror("hevel: socket");
    return 1;
  }

  if (inject_send_reply(fd, command) < 0) {
    perror("hevel: write");
    close(fd);
    return 1;
  }

  amount = read(fd, response, sizeof(response) - 1);
  if (amount < 0) {
    perror("hevel: read");
    close(fd);
    return 1;
  }

  response[amount] = '\0';
  close(fd);

  if (strncmp(response, "ok", 2) == 0) return 0;

  fprintf(stderr, "%s", response[0] ? response : "error no reply\n");
  return 1;
}

int
inject_cli_main(int argc, char **argv)
{
  char socket_path[256];
  char command[256];
  const char *display = getenv("WAYLAND_DISPLAY");

  if (argc <= 0) {
    fprintf(stderr,
            "usage: hevel --inject [--socket PATH] "
            "<ping|key|motion|absolute|button|axis|frame> ...\n");
    return 2;
  }

  if (argc >= 3 && strcmp(argv[0], "--socket") == 0) {
    if (snprintf(socket_path, sizeof(socket_path), "%s", argv[1]) >=
        (int)sizeof(socket_path)) {
      fprintf(stderr, "hevel: socket path too long\n");
      return 2;
    }
    argv += 2;
    argc -= 2;
  } else if (inject_resolve_socket_path(socket_path, sizeof(socket_path),
                                        display) < 0) {
    fprintf(stderr,
            "hevel: cannot resolve injection socket path; set WAYLAND_DISPLAY "
            "and XDG_RUNTIME_DIR or use --socket\n");
    return 1;
  }

  if (inject_build_command(command, sizeof(command), argc, argv) < 0) {
    fprintf(stderr, "hevel: injection command too long\n");
    return 2;
  }

  return inject_send_command(socket_path, command);
}

struct hevel_ei_client {
  struct ei *ctx;
  struct ei_seat *seat;
  struct ei_device *keyboard;
  struct ei_device *pointer;
  struct ei_device *pointer_abs;
  struct ei_device *button;
  struct ei_device *scroll;
  uint32_t sequence;
  bool connected;
  bool disconnected;
  bool keyboard_ready;
  bool pointer_ready;
  bool pointer_abs_ready;
  bool button_ready;
  bool scroll_ready;
};

static void
eis_cli_release_device(struct ei_device **device)
{
  if (!device || !*device) return;
  *device = ei_device_unref(*device);
}

static void
eis_cli_reset(struct hevel_ei_client *client)
{
  eis_cli_release_device(&client->keyboard);
  eis_cli_release_device(&client->pointer);
  eis_cli_release_device(&client->pointer_abs);
  eis_cli_release_device(&client->button);
  eis_cli_release_device(&client->scroll);

  if (client->seat) client->seat = ei_seat_unref(client->seat);
  if (client->ctx) {
    ei_unref(client->ctx);
    client->ctx = NULL;
  }
}

static void
eis_cli_track_device(struct hevel_ei_client *client, struct ei_device *device)
{
  if (!client->keyboard && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD))
    client->keyboard = ei_device_ref(device);
  if (!client->pointer && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER))
    client->pointer = ei_device_ref(device);
  if (!client->pointer_abs &&
      ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
    client->pointer_abs = ei_device_ref(device);
  if (!client->button && ei_device_has_capability(device, EI_DEVICE_CAP_BUTTON))
    client->button = ei_device_ref(device);
  if (!client->scroll && ei_device_has_capability(device, EI_DEVICE_CAP_SCROLL))
    client->scroll = ei_device_ref(device);
}

static void
eis_cli_mark_ready(struct hevel_ei_client *client, struct ei_device *device,
                   bool ready)
{
  if (client->keyboard == device) client->keyboard_ready = ready;
  if (client->pointer == device) client->pointer_ready = ready;
  if (client->pointer_abs == device) client->pointer_abs_ready = ready;
  if (client->button == device) client->button_ready = ready;
  if (client->scroll == device) client->scroll_ready = ready;
}

static void
eis_cli_process_events(struct hevel_ei_client *client)
{
  struct ei_event *event;

  while ((event = ei_get_event(client->ctx)) != NULL) {
    switch (ei_event_get_type(event)) {
      case EI_EVENT_CONNECT:
        client->connected = true;
        break;
      case EI_EVENT_DISCONNECT:
        client->disconnected = true;
        break;
      case EI_EVENT_SEAT_ADDED:
        if (!client->seat) client->seat = ei_seat_ref(ei_event_get_seat(event));
        if (client->seat)
          ei_seat_bind_capabilities(
              client->seat, EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE,
              EI_DEVICE_CAP_KEYBOARD, EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_SCROLL,
              NULL);
        break;
      case EI_EVENT_DEVICE_ADDED:
        eis_cli_track_device(client, ei_event_get_device(event));
        break;
      case EI_EVENT_DEVICE_RESUMED: {
        struct ei_device *device = ei_event_get_device(event);
        ei_device_start_emulating(device, ++client->sequence);
        eis_cli_mark_ready(client, device, true);
        break;
      }
      case EI_EVENT_DEVICE_PAUSED:
        eis_cli_mark_ready(client, ei_event_get_device(event), false);
        break;
      default:
        break;
    }

    ei_event_unref(event);
  }
}

static int
inject_request_eis_fd(const char *socket_path, const char *session_id,
                      uint32_t capabilities, const char *name)
{
  char command[256];
  char reply[64] = {0};
  struct msghdr msg = {0};
  struct iovec iov = {.iov_base = reply, .iov_len = sizeof(reply) - 1};
  char control[CMSG_SPACE(sizeof(int))];
  struct cmsghdr *cmsg;
  int fd = -1;
  int socket_fd;
  ssize_t amount;

  socket_fd = inject_connect_socket(socket_path);
  if (socket_fd < 0) {
    perror("hevel: connect");
    return -1;
  }

  if (!session_id || session_id[0] == '\0' || !name || name[0] == '\0') {
    fprintf(stderr,
            "hevel: EIS requests must identify an approved session and name\n");
    close(socket_fd);
    return -1;
  }

  if (snprintf(command, sizeof(command), "eis-open %s %u %s\n", session_id,
               capabilities, name) >= (int)sizeof(command)) {
    fprintf(stderr, "hevel: EIS request command too long\n");
    close(socket_fd);
    return -1;
  }

  if (inject_send_reply(socket_fd, command) < 0) {
    perror("hevel: write");
    close(socket_fd);
    return -1;
  }

  memset(control, 0, sizeof(control));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  do {
    amount = recvmsg(socket_fd, &msg, 0);
  } while (amount < 0 && errno == EINTR);

  if (amount < 0) {
    perror("hevel: recvmsg");
    close(socket_fd);
    return -1;
  }

  reply[amount] = '\0';
  if (strncmp(reply, "ok", 2) != 0) {
    fprintf(stderr, "%s", reply[0] ? reply : "error no reply\n");
    close(socket_fd);
    return -1;
  }

  cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
      cmsg->cmsg_len < CMSG_LEN(sizeof(int))) {
    fprintf(stderr, "hevel: EIS fd reply did not include a file descriptor\n");
    close(socket_fd);
    return -1;
  }

  memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
  close(socket_fd);
  return fd;
}

int
inject_open_eis_fd(const char *display_name, const char *session_id,
                   uint32_t capabilities, const char *name)
{
  char socket_path[256];
  const char *display = display_name && display_name[0] != '\0'
                            ? display_name
                            : getenv("WAYLAND_DISPLAY");

  if (!session_id || session_id[0] == '\0' || !name || name[0] == '\0')
    return -EINVAL;

  if (inject_resolve_socket_path(socket_path, sizeof(socket_path), display) < 0) {
    fprintf(stderr,
            "hevel: cannot resolve injection socket path for EIS handoff\n");
    return -1;
  }

  return inject_request_eis_fd(socket_path, session_id, capabilities, name);
}

int
inject_close_eis_session(const char *display_name, const char *session_id)
{
  char socket_path[256];
  char command[256];
  const char *display = display_name && display_name[0] != '\0'
                            ? display_name
                            : getenv("WAYLAND_DISPLAY");

  if (!session_id || session_id[0] == '\0') return -EINVAL;

  if (inject_resolve_socket_path(socket_path, sizeof(socket_path), display) < 0) {
    fprintf(stderr,
            "hevel: cannot resolve injection socket path for EIS teardown\n");
    return -1;
  }

  if (snprintf(command, sizeof(command), "eis-close %s\n", session_id) >=
      (int)sizeof(command)) {
    fprintf(stderr, "hevel: EIS close command too long\n");
    return -1;
  }

  return inject_send_command(socket_path, command) == 0 ? 0 : -1;
}

int
inject_prepare_approval_window(const char *display_name, const char *app_id,
                               uint32_t width, uint32_t height)
{
  char socket_path[256];
  char command[256];
  const char *display = display_name && display_name[0] != '\0'
                            ? display_name
                            : getenv("WAYLAND_DISPLAY");

  if (!app_id || strcmp(app_id, select_term_app_id) != 0) return -EINVAL;

  if (inject_resolve_socket_path(socket_path, sizeof(socket_path), display) < 0) {
    fprintf(stderr,
            "hevel: cannot resolve injection socket path for approval window\n");
    return -1;
  }

  if (snprintf(command, sizeof(command), "spawn-prepare %u %u\n", width,
               height) >= (int)sizeof(command)) {
    fprintf(stderr, "hevel: spawn prepare command too long\n");
    return -1;
  }

  return inject_send_command(socket_path, command) == 0 ? 0 : -1;
}

int
inject_cancel_prepared_spawn(const char *display_name)
{
  char socket_path[256];
  const char *display = display_name && display_name[0] != '\0'
                            ? display_name
                            : getenv("WAYLAND_DISPLAY");

  if (inject_resolve_socket_path(socket_path, sizeof(socket_path), display) < 0) {
    fprintf(stderr,
            "hevel: cannot resolve injection socket path for spawn rollback\n");
    return -1;
  }

  return inject_send_command(socket_path, "spawn-cancel\n") == 0 ? 0 : -1;
}

static int
eis_cli_connect(struct hevel_ei_client *client, const char *socket_path)
{
  int fd = inject_request_eis_fd(socket_path, NULL, 0, NULL);
  int r;

  if (fd < 0) return -1;

  client->ctx = ei_new_sender(NULL);
  if (!client->ctx) {
    close(fd);
    fprintf(stderr, "hevel: cannot create EI sender context\n");
    return -1;
  }

  ei_configure_name(client->ctx, "hevel-cli");
  r = ei_setup_backend_fd(client->ctx, fd);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot connect EI sender: %s\n", strerror(-r));
    eis_cli_reset(client);
    return -1;
  }

  return 0;
}

static bool
eis_cli_ready_for_command(const struct hevel_ei_client *client,
                          const char *command)
{
  if (strcmp(command, "ping") == 0) return client->connected;
  if (strcmp(command, "key") == 0) return client->keyboard_ready;
  if (strcmp(command, "motion") == 0) return client->pointer_ready;
  if (strcmp(command, "absolute") == 0) return client->pointer_abs_ready;
  if (strcmp(command, "button") == 0) return client->button_ready;
  if (strcmp(command, "axis") == 0) return client->scroll_ready;
  return false;
}

static int
eis_cli_wait_ready(struct hevel_ei_client *client, const char *command)
{
  struct pollfd pfd;
  uint32_t start = inject_now_ms();

  pfd.fd = ei_get_fd(client->ctx);
  pfd.events = POLLIN;

  while (!eis_cli_ready_for_command(client, command)) {
    int timeout_ms = 3000 - (int)(inject_now_ms() - start);
    int r;

    if (client->disconnected) {
      fprintf(stderr, "hevel: EI sender disconnected before %s became ready\n",
              command);
      return -1;
    }
    if (timeout_ms <= 0) {
      fprintf(stderr, "hevel: timed out waiting for %s capability\n", command);
      return -1;
    }

    r = poll(&pfd, 1, timeout_ms);
    if (r < 0) {
      if (errno == EINTR) continue;
      perror("hevel: poll");
      return -1;
    }
    if (r == 0) {
      fprintf(stderr, "hevel: timed out waiting for EI events\n");
      return -1;
    }
    if ((pfd.revents & POLLIN) == 0) {
      fprintf(stderr, "hevel: EI sender connection became unusable\n");
      return -1;
    }

    ei_dispatch(client->ctx);
    eis_cli_process_events(client);
  }

  return 0;
}

static void
eis_cli_flush(struct hevel_ei_client *client, int timeout_ms)
{
  struct pollfd pfd;
  uint32_t start = inject_now_ms();

  pfd.fd = ei_get_fd(client->ctx);
  pfd.events = POLLIN | POLLOUT;

  for (;;) {
    int remaining = timeout_ms - (int)(inject_now_ms() - start);
    int r;

    ei_dispatch(client->ctx);
    eis_cli_process_events(client);

    if (remaining <= 0) return;

    r = poll(&pfd, 1, remaining > 20 ? 20 : remaining);
    if (r < 0) {
      if (errno == EINTR) continue;
      return;
    }
    if (r == 0) return;
  }
}

static int
eis_cli_send_command(struct hevel_ei_client *client, int argc, char **argv)
{
  uint32_t key, button, state, axis;
  int32_t dx, dy, x, y, value, value120;

  if (argc <= 0) return 2;

  if (strcmp(argv[0], "ping") == 0) {
    puts("ok");
    return 0;
  }

  if (strcmp(argv[0], "key") == 0) {
    if (argc != 3 || inject_parse_u32(argv[1], &key) < 0 ||
        inject_parse_u32(argv[2], &state) < 0) {
      fprintf(stderr, "usage: hevel --eis key <evdev-key> <state>\n");
      return 2;
    }
    ei_device_keyboard_key(client->keyboard, key, state != 0);
    ei_device_frame(client->keyboard, ei_now(client->ctx));
    eis_cli_flush(client, 100);
    puts("sent");
    return 0;
  }

  if (strcmp(argv[0], "motion") == 0) {
    if (argc != 3 || inject_parse_i32(argv[1], &dx) < 0 ||
        inject_parse_i32(argv[2], &dy) < 0) {
      fprintf(stderr, "usage: hevel --eis motion <dx> <dy>\n");
      return 2;
    }
    ei_device_pointer_motion(client->pointer, (double)dx, (double)dy);
    ei_device_frame(client->pointer, ei_now(client->ctx));
    eis_cli_flush(client, 100);
    puts("sent");
    return 0;
  }

  if (strcmp(argv[0], "absolute") == 0) {
    if (argc != 3 || inject_parse_i32(argv[1], &x) < 0 ||
        inject_parse_i32(argv[2], &y) < 0) {
      fprintf(stderr, "usage: hevel --eis absolute <x> <y>\n");
      return 2;
    }
    ei_device_pointer_motion_absolute(client->pointer_abs, (double)x, (double)y);
    ei_device_frame(client->pointer_abs, ei_now(client->ctx));
    eis_cli_flush(client, 100);
    puts("sent");
    return 0;
  }

  if (strcmp(argv[0], "button") == 0) {
    if (argc != 3 || inject_parse_u32(argv[1], &button) < 0 ||
        inject_parse_u32(argv[2], &state) < 0) {
      fprintf(stderr, "usage: hevel --eis button <button> <state>\n");
      return 2;
    }
    ei_device_button_button(client->button, button, state != 0);
    ei_device_frame(client->button, ei_now(client->ctx));
    eis_cli_flush(client, 100);
    puts("sent");
    return 0;
  }

  if (strcmp(argv[0], "axis") == 0) {
    if (argc != 4 || inject_parse_u32(argv[1], &axis) < 0 ||
        inject_parse_i32(argv[2], &value) < 0 ||
        inject_parse_i32(argv[3], &value120) < 0) {
      fprintf(stderr, "usage: hevel --eis axis <axis> <value> <value120>\n");
      return 2;
    }
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
      if (value != 0) ei_device_scroll_delta(client->scroll, 0.0, (double)value);
      if (value120 != 0) ei_device_scroll_discrete(client->scroll, 0, value120);
    } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
      if (value != 0) ei_device_scroll_delta(client->scroll, (double)value, 0.0);
      if (value120 != 0) ei_device_scroll_discrete(client->scroll, value120, 0);
    } else {
      fprintf(stderr, "hevel: unsupported axis %u\n", axis);
      return 2;
    }
    ei_device_frame(client->scroll, ei_now(client->ctx));
    eis_cli_flush(client, 100);
    puts("sent");
    return 0;
  }

  fprintf(stderr,
          "usage: hevel --eis [--socket PATH] <ping|key|motion|absolute|button|axis> ...\n");
  return 2;
}

int
eis_cli_main(int argc, char **argv)
{
  struct hevel_ei_client client = {0};
  char socket_path[256];
  const char *display = getenv("WAYLAND_DISPLAY");
  int rc;

  if (argc <= 0) {
    fprintf(stderr,
            "usage: hevel --eis [--socket PATH] "
            "<ping|key|motion|absolute|button|axis> ...\n");
    return 2;
  }

  if (argc >= 3 && strcmp(argv[0], "--socket") == 0) {
    if (snprintf(socket_path, sizeof(socket_path), "%s", argv[1]) >=
        (int)sizeof(socket_path)) {
      fprintf(stderr, "hevel: socket path too long\n");
      return 2;
    }
    argv += 2;
    argc -= 2;
  } else if (inject_resolve_socket_path(socket_path, sizeof(socket_path),
                                        display) < 0) {
    fprintf(stderr,
            "hevel: cannot resolve injection socket path; set WAYLAND_DISPLAY "
            "and XDG_RUNTIME_DIR or use --socket\n");
    return 1;
  }

  if (eis_cli_connect(&client, socket_path) < 0) {
    eis_cli_reset(&client);
    return 1;
  }

  ei_dispatch(client.ctx);
  eis_cli_process_events(&client);
  if (eis_cli_wait_ready(&client, argv[0]) < 0) {
    eis_cli_reset(&client);
    return 1;
  }
  eis_cli_flush(&client, 100);

  rc = eis_cli_send_command(&client, argc, argv);
  eis_cli_reset(&client);
  return rc;
}
