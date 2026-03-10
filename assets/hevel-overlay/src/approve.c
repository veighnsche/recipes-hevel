#include "hevel.h"

#include <ctype.h>
#include <errno.h>
#include <termios.h>

static int
approve_parse_fd(void)
{
  const char *value = getenv("HEVEL_APPROVE_FD");
  char *end = NULL;
  long parsed;

  if (!value || value[0] == '\0') return -1;

  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed < 0 || parsed > INT_MAX)
    return -1;

  return (int)parsed;
}

static int
approve_send_decision(int fd, bool allow)
{
  const char *message = allow ? "allow\n" : "deny\n";
  size_t remaining = strlen(message);
  const char *cursor = message;

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

static void
approve_print_sanitized(const char *value, const char *fallback)
{
  const unsigned char *cursor =
      (const unsigned char *)((value && value[0] != '\0') ? value : fallback);

  while (*cursor != '\0') {
    putchar(isprint(*cursor) ? (int)*cursor : '?');
    ++cursor;
  }
}

static void
approve_print_prompt(const char *app_id, const char *devices)
{
  printf("hevel RemoteDesktop request\n\n");
  printf("application: ");
  approve_print_sanitized(app_id, "(unknown)");
  printf("\n");
  printf("devices: ");
  approve_print_sanitized(devices, "(none)");
  printf("\n\n");
  printf("Allow [a]  Deny [d]  Esc = deny\n");
  fflush(stdout);
}

static int
approve_read_choice_stream(void)
{
  char line[32];

  if (!fgets(line, sizeof(line), stdin)) return 0;
  return line[0] == 'a' || line[0] == 'A';
}

static int
approve_read_choice_tty(void)
{
  struct termios oldt;
  struct termios newt;
  int choice = 0;

  if (tcgetattr(STDIN_FILENO, &oldt) < 0) return approve_read_choice_stream();

  newt = oldt;
  newt.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  newt.c_cc[VMIN] = 1;
  newt.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) < 0)
    return approve_read_choice_stream();

  for (;;) {
    unsigned char ch = 0;
    ssize_t amount = read(STDIN_FILENO, &ch, 1);

    if (amount < 0) {
      if (errno == EINTR) continue;
      choice = 0;
      break;
    }
    if (amount == 0) {
      choice = 0;
      break;
    }

    if (ch == 'a' || ch == 'A') {
      choice = 1;
      break;
    }
    if (ch == 'd' || ch == 'D' || ch == 27) {
      choice = 0;
      break;
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  printf("\n");
  fflush(stdout);
  return choice;
}

int
approve_ui_main(int argc, char **argv)
{
  const char *app_id;
  const char *devices;
  int decision_fd;
  int allow;

  if (argc != 2) {
    fprintf(stderr, "usage: hevel --approve-ui <app-id> <devices>\n");
    return 2;
  }

  decision_fd = approve_parse_fd();
  if (decision_fd < 0) {
    fprintf(stderr, "hevel: HEVEL_APPROVE_FD is missing or invalid\n");
    return 1;
  }

  app_id = argv[0];
  devices = argv[1];

  approve_print_prompt(app_id, devices);
  allow = isatty(STDIN_FILENO) ? approve_read_choice_tty()
                               : approve_read_choice_stream();

  if (approve_send_decision(decision_fd, allow != 0) < 0) {
    perror("hevel: write approval decision");
    close(decision_fd);
    return 1;
  }

  close(decision_fd);
  return allow ? 0 : 1;
}
