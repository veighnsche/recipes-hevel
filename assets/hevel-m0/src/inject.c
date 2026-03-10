#include "hevel.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

static int inject_fd = -1;
static struct wl_event_source *inject_source = NULL;
static char inject_socket_path[256] = {0};

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
inject_execute_command(char *buffer, char *error, size_t error_size)
{
  char *argv[8] = {0};
  char *save = NULL;
  int argc = 0;
  uint32_t time = inject_now_ms();
  uint32_t key, button, state, axis, source;
  int32_t dx, dy, x, y, value, value120;
  bool ok = false;

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

  if (strcmp(argv[0], "key") == 0) {
    if (argc != 3 || inject_parse_u32(argv[1], &key) < 0 ||
        inject_parse_u32(argv[2], &state) < 0) {
      snprintf(error, error_size, "usage: key <evdev-key> <state>");
      return -1;
    }
    ok = swc_keyboard_inject_key(time, key, state);
  } else if (strcmp(argv[0], "motion") == 0) {
    if (argc != 3 || inject_parse_i32(argv[1], &dx) < 0 ||
        inject_parse_i32(argv[2], &dy) < 0) {
      snprintf(error, error_size, "usage: motion <dx> <dy>");
      return -1;
    }
    ok = swc_pointer_inject_relative_motion(time, dx, dy);
  } else if (strcmp(argv[0], "absolute") == 0) {
    if (argc != 3 || inject_parse_i32(argv[1], &x) < 0 ||
        inject_parse_i32(argv[2], &y) < 0) {
      snprintf(error, error_size, "usage: absolute <x> <y>");
      return -1;
    }
    ok = swc_pointer_inject_absolute_motion(time, x, y);
  } else if (strcmp(argv[0], "button") == 0) {
    if (argc != 3 || inject_parse_u32(argv[1], &button) < 0 ||
        inject_parse_u32(argv[2], &state) < 0) {
      snprintf(error, error_size, "usage: button <button> <state>");
      return -1;
    }
    ok = swc_pointer_inject_button(time, button, state);
  } else if (strcmp(argv[0], "axis") == 0) {
    if (argc != 5 || inject_parse_u32(argv[1], &axis) < 0 ||
        inject_parse_u32(argv[2], &source) < 0 ||
        inject_parse_i32(argv[3], &value) < 0 ||
        inject_parse_i32(argv[4], &value120) < 0) {
      snprintf(error, error_size,
               "usage: axis <axis> <source> <value> <value120>");
      return -1;
    }
    ok = swc_pointer_inject_axis(time, axis, source, value, value120);
  } else if (strcmp(argv[0], "frame") == 0) {
    if (argc != 1) {
      snprintf(error, error_size, "usage: frame");
      return -1;
    }
    ok = swc_pointer_inject_frame();
  } else {
    snprintf(error, error_size, "unknown command");
    return -1;
  }

  if (!ok) {
    snprintf(error, error_size, "hevel input injection unavailable");
    return -1;
  }

  return 0;
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
  struct sockaddr_un addr = {0};
  char response[256] = {0};
  int fd;
  ssize_t amount;

  if (strlen(socket_path) >= sizeof(addr.sun_path)) {
    fprintf(stderr, "hevel: injection socket path too long\n");
    return 1;
  }

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("hevel: socket");
    return 1;
  }

  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, socket_path);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("hevel: connect");
    close(fd);
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
