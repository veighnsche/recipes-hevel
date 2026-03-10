#include "hevel.h"

#include <errno.h>
#include <sys/wait.h>

#include <systemd/sd-bus.h>

#define HEVEL_REMOTEDESKTOP_RESPONSE_SUCCESS 0U
#define HEVEL_REMOTEDESKTOP_RESPONSE_OTHER 2U
#define HEVEL_REMOTEDESKTOP_VERSION 2U
#define HEVEL_REMOTEDESKTOP_SESSION_VERSION 1U

#define HEVEL_RD_DEVICE_KEYBOARD 1U
#define HEVEL_RD_DEVICE_POINTER 2U
#define HEVEL_RD_AVAILABLE_DEVICE_TYPES                                          \
  (HEVEL_RD_DEVICE_KEYBOARD | HEVEL_RD_DEVICE_POINTER)
#define HEVEL_RD_APPROVAL_WIDTH 720U
#define HEVEL_RD_APPROVAL_HEIGHT 220U
#define HEVEL_RD_APPROVAL_TITLE "hevel approval"
#define HEVEL_RD_APPROVAL_TERMINAL "/usr/local/bin/st-wl"
#define HEVEL_RD_APPROVAL_HELPER "/usr/local/bin/hevel"

static const char *remotedesktop_request_interface =
    "org.freedesktop.impl.portal.Request";
static const char *remotedesktop_session_interface =
    "org.freedesktop.impl.portal.Session";
static const char *remotedesktop_portal_error =
    "org.freedesktop.portal.Error.Failed";

static int remotedesktop_method_close_request(sd_bus_message *m, void *userdata,
                                              sd_bus_error *ret_error);
static int remotedesktop_method_close_session(sd_bus_message *m, void *userdata,
                                              sd_bus_error *ret_error);
static int remotedesktop_property_session_version(sd_bus *bus, const char *path,
                                                  const char *interface,
                                                  const char *property,
                                                  sd_bus_message *reply,
                                                  void *userdata,
                                                  sd_bus_error *ret_error);
static void remotedesktop_destroy_request(struct hevel_portal_request *request);
static void remotedesktop_stop_prompt(struct hevel_rd_session *session);

static const sd_bus_vtable remotedesktop_request_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Close", "", "", remotedesktop_method_close_request,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable remotedesktop_session_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Close", "", "", remotedesktop_method_close_session,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Closed", "", 0),
    SD_BUS_PROPERTY("version", "u", remotedesktop_property_session_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static const char *
remotedesktop_state_name(enum hevel_rd_session_state state)
{
  switch (state) {
    case HEVEL_RD_CREATED:
      return "created";
    case HEVEL_RD_SELECTED:
      return "selected";
    case HEVEL_RD_PENDING_APPROVAL:
      return "pending-approval";
    case HEVEL_RD_STARTED:
      return "started";
    case HEVEL_RD_CONNECTED:
      return "connected";
    case HEVEL_RD_CLOSED:
      return "closed";
    default:
      return "unknown";
  }
}

static void
remotedesktop_log(const char *message, const char *handle, const char *detail)
{
  fprintf(stderr, "hevel: RemoteDesktop %s%s%s%s\n", message,
          handle && handle[0] != '\0' ? " " : "",
          handle && handle[0] != '\0' ? handle : "",
          detail && detail[0] != '\0' ? detail : "");
}

static int
remotedesktop_append_dict_u32(sd_bus_message *reply, const char *key,
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
remotedesktop_append_dict_string(sd_bus_message *reply, const char *key,
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
remotedesktop_send_response(sd_bus_message *m, uint32_t response,
                            const char *session_id, uint32_t devices)
{
  sd_bus_message *reply = NULL;
  int r;

  r = sd_bus_message_new_method_return(m, &reply);
  if (r < 0) return r;

  r = sd_bus_message_append(reply, "u", response);
  if (r < 0) goto out;

  r = sd_bus_message_open_container(reply, 'a', "{sv}");
  if (r < 0) goto out;

  if (session_id && session_id[0] != '\0') {
    r = remotedesktop_append_dict_string(reply, "session", session_id);
    if (r < 0) goto out;
  }

  if (devices != UINT32_MAX) {
    r = remotedesktop_append_dict_u32(reply, "devices", devices);
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
remotedesktop_reply_session_created(sd_bus_message *m,
                                    const struct hevel_rd_session *session)
{
  return remotedesktop_send_response(m, HEVEL_REMOTEDESKTOP_RESPONSE_SUCCESS,
                                     session->session_id, UINT32_MAX);
}

static int
remotedesktop_reply_devices(sd_bus_message *m, uint32_t devices)
{
  return remotedesktop_send_response(m, HEVEL_REMOTEDESKTOP_RESPONSE_SUCCESS,
                                     NULL, devices);
}

static int
remotedesktop_reply_rejected(sd_bus_message *m, const char *context,
                             const char *detail)
{
  remotedesktop_log(context, NULL, detail);
  return remotedesktop_send_response(m, HEVEL_REMOTEDESKTOP_RESPONSE_OTHER,
                                     NULL, UINT32_MAX);
}

static int
remotedesktop_destroy_request_and_reject(sd_bus_message *m,
                                         struct hevel_portal_request *request,
                                         const char *context,
                                         const char *detail)
{
  int r = remotedesktop_reply_rejected(m, context, detail);
  remotedesktop_destroy_request(request);
  return r;
}

static int
remotedesktop_parse_options(sd_bus_message *m, uint32_t *types,
                            bool *have_types, uint32_t *persist_mode,
                            bool *have_persist_mode)
{
  int r;

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char *key = NULL;
    const char *contents = NULL;
    char type;

    r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;

    r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (type != 'v') return -EINVAL;

    r = sd_bus_message_enter_container(m, 'v', contents);
    if (r < 0) return r;

    if (types && have_types && strcmp(key, "types") == 0) {
      if (strcmp(contents, "u") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "u", types);
      if (r < 0) return r;
      *have_types = true;
    } else if (persist_mode && have_persist_mode &&
               strcmp(key, "persist_mode") == 0) {
      if (strcmp(contents, "u") != 0) return -EINVAL;
      r = sd_bus_message_read(m, "u", persist_mode);
      if (r < 0) return r;
      *have_persist_mode = true;
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

static struct hevel_portal_request *
remotedesktop_find_request(const char *handle)
{
  struct hevel_portal_request *request;

  wl_list_for_each(request, &portal.requests, link)
  {
    if (strcmp(request->handle, handle) == 0) return request;
  }

  return NULL;
}

static struct hevel_rd_session *
remotedesktop_find_session(const char *session_handle)
{
  struct hevel_rd_session *session;

  wl_list_for_each(session, &portal.remotedesktop_sessions, link)
  {
    if (strcmp(session->session_handle, session_handle) == 0) return session;
  }

  return NULL;
}

static struct hevel_rd_session *
remotedesktop_find_active_session(void)
{
  struct hevel_rd_session *session;

  wl_list_for_each(session, &portal.remotedesktop_sessions, link)
  {
    if (session->state != HEVEL_RD_CLOSED) return session;
  }

  return NULL;
}

static void
remotedesktop_destroy_request(struct hevel_portal_request *request)
{
  if (!request) return;

  wl_list_remove(&request->link);
  wl_list_init(&request->link);

  if (request->slot) {
    sd_bus_slot_unref(request->slot);
    request->slot = NULL;
  }

  free(request);
}

static void
remotedesktop_destroy_requests_for_session(const char *session_handle)
{
  struct hevel_portal_request *request, *tmp;

  wl_list_for_each_safe(request, tmp, &portal.requests, link)
  {
    if (strcmp(request->session_handle, session_handle) != 0) continue;
    remotedesktop_destroy_request(request);
  }
}

static void
remotedesktop_destroy_session(struct hevel_rd_session *session, bool emit_closed)
{
  if (!session) return;

  session->state = HEVEL_RD_CLOSED;
  remotedesktop_log("closing session", session->session_handle, "");
  remotedesktop_stop_prompt(session);

  if (session->eis_fd_issued && session->session_id[0] != '\0' &&
      inject_close_eis_session(NULL, session->session_id) < 0)
    fprintf(stderr, "hevel: failed to revoke EIS session %s\n",
            session->session_id);

  if (emit_closed && portal.bus)
    sd_bus_emit_signal(portal.bus, session->session_handle,
                       remotedesktop_session_interface, "Closed", "");

  remotedesktop_destroy_requests_for_session(session->session_handle);

  wl_list_remove(&session->link);
  wl_list_init(&session->link);

  if (session->slot) {
    sd_bus_slot_unref(session->slot);
    session->slot = NULL;
  }

  free(session);
}

static int
remotedesktop_new_request(const char *handle, const char *session_handle,
                          const char *app_id,
                          struct hevel_portal_request **out_request)
{
  struct hevel_portal_request *request;
  int r;

  if (!portal.bus) return -ENOTCONN;
  if (remotedesktop_find_request(handle)) return -EEXIST;

  request = calloc(1, sizeof(*request));
  if (!request) return -ENOMEM;

  wl_list_init(&request->link);
  snprintf(request->handle, sizeof(request->handle), "%s", handle);
  snprintf(request->session_handle, sizeof(request->session_handle), "%s",
           session_handle ? session_handle : "");
  snprintf(request->app_id, sizeof(request->app_id), "%s",
           app_id ? app_id : "");

  r = sd_bus_add_object_vtable(portal.bus, &request->slot, request->handle,
                               remotedesktop_request_interface,
                               remotedesktop_request_vtable, request);
  if (r < 0) {
    free(request);
    return r;
  }

  wl_list_insert(portal.requests.prev, &request->link);
  *out_request = request;
  return 0;
}

static int
remotedesktop_generate_session_id(char *buffer, size_t buffer_size)
{
  ++portal.next_session_id;
  return snprintf(buffer, buffer_size, "hevel-rd-%u", portal.next_session_id) <
                 (int)buffer_size
             ? 0
             : -ENAMETOOLONG;
}

static int
remotedesktop_new_session(const char *session_handle, const char *app_id,
                          struct hevel_rd_session **out_session)
{
  struct hevel_rd_session *session;
  int r;

  if (!portal.bus) return -ENOTCONN;
  if (remotedesktop_find_session(session_handle)) return -EEXIST;
  if (remotedesktop_find_active_session()) return -EBUSY;

  session = calloc(1, sizeof(*session));
  if (!session) return -ENOMEM;

  wl_list_init(&session->link);
  snprintf(session->session_handle, sizeof(session->session_handle), "%s",
           session_handle);
  snprintf(session->app_id, sizeof(session->app_id), "%s", app_id ? app_id : "");
  session->approval_state = HEVEL_RD_APPROVAL_UNKNOWN;
  session->state = HEVEL_RD_CREATED;

  r = remotedesktop_generate_session_id(session->session_id,
                                        sizeof(session->session_id));
  if (r < 0) {
    free(session);
    return r;
  }

  r = sd_bus_add_object_vtable(portal.bus, &session->slot,
                               session->session_handle,
                               remotedesktop_session_interface,
                               remotedesktop_session_vtable, session);
  if (r < 0) {
    free(session);
    return r;
  }

  wl_list_insert(portal.remotedesktop_sessions.prev, &session->link);
  *out_session = session;
  return 0;
}

static int
remotedesktop_require_session(sd_bus_message *m, const char *session_handle,
                              const char *app_id,
                              struct hevel_rd_session **out_session)
{
  struct hevel_rd_session *session =
      remotedesktop_find_session(session_handle);

  if (!session)
    return remotedesktop_reply_rejected(m, "rejecting unknown session",
                                        session_handle);

  if (strcmp(session->app_id, app_id ? app_id : "") != 0)
    return remotedesktop_reply_rejected(m, "rejecting app-id mismatch for",
                                        session_handle);

  *out_session = session;
  return 0;
}

static int
remotedesktop_require_lifecycle(sd_bus_message *m,
                                const struct hevel_rd_session *session,
                                enum hevel_rd_session_state expected,
                                const char *method)
{
  char detail[256];

  if (session->state == expected) return 0;

  snprintf(detail, sizeof(detail), " %s in %s", method,
           remotedesktop_state_name(session->state));
  return remotedesktop_reply_rejected(m, "rejecting lifecycle transition",
                                      detail);
}

static void
remotedesktop_transition(struct hevel_rd_session *session,
                         enum hevel_rd_session_state state)
{
  fprintf(stderr, "hevel: RemoteDesktop session %s %s -> %s\n",
          session->session_handle, remotedesktop_state_name(session->state),
          remotedesktop_state_name(state));
  session->state = state;
}

static uint32_t
remotedesktop_map_eis_capabilities(uint32_t device_types)
{
  uint32_t capabilities = 0;

  if ((device_types & HEVEL_RD_DEVICE_KEYBOARD) != 0)
    capabilities |= EIS_DEVICE_CAP_KEYBOARD;

  if ((device_types & HEVEL_RD_DEVICE_POINTER) != 0) {
    capabilities |= EIS_DEVICE_CAP_POINTER;
    capabilities |= EIS_DEVICE_CAP_POINTER_ABSOLUTE;
    capabilities |= EIS_DEVICE_CAP_BUTTON;
    capabilities |= EIS_DEVICE_CAP_SCROLL;
  }

  return capabilities;
}

static void
remotedesktop_stop_prompt(struct hevel_rd_session *session)
{
  int status;

  if (!session) return;

  session->prompt_visible = false;
  if (session->prompt_pid <= 0) {
    session->prompt_pid = 0;
    return;
  }

  if (kill(session->prompt_pid, SIGTERM) < 0 && errno != ESRCH)
    perror("hevel: kill approval prompt");

  while (waitpid(session->prompt_pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    if (errno != ECHILD) perror("hevel: waitpid approval prompt");
    break;
  }

  session->prompt_pid = 0;
}

static int
remotedesktop_format_devices(uint32_t types, char *buffer, size_t buffer_size)
{
  const bool have_keyboard = (types & HEVEL_RD_DEVICE_KEYBOARD) != 0;
  const bool have_pointer = (types & HEVEL_RD_DEVICE_POINTER) != 0;
  int written;

  if (have_keyboard && have_pointer)
    written = snprintf(buffer, buffer_size, "keyboard, pointer");
  else if (have_keyboard)
    written = snprintf(buffer, buffer_size, "keyboard");
  else if (have_pointer)
    written = snprintf(buffer, buffer_size, "pointer");
  else
    written = snprintf(buffer, buffer_size, "none");

  return (written < 0 || (size_t)written >= buffer_size) ? -ENAMETOOLONG : 0;
}

static int
remotedesktop_read_approval_decision(int fd, bool *approved)
{
  char decision[32];
  size_t used = 0;

  while (used < sizeof(decision) - 1) {
    ssize_t amount = read(fd, decision + used, sizeof(decision) - 1 - used);

    if (amount < 0) {
      if (errno == EINTR) continue;
      return -errno;
    }
    if (amount == 0) break;

    used += (size_t)amount;
    if (memchr(decision, '\n', used)) break;
  }

  while (used > 0 &&
         (decision[used - 1] == '\n' || decision[used - 1] == '\r' ||
          decision[used - 1] == ' ' || decision[used - 1] == '\t'))
    --used;
  decision[used] = '\0';

  if (strcmp(decision, "allow") == 0) {
    *approved = true;
    return 0;
  }
  if (strcmp(decision, "deny") == 0) {
    *approved = false;
    return 0;
  }

  return used == 0 ? -ECANCELED : -EPROTO;
}

static int
remotedesktop_prompt_for_approval(struct hevel_rd_session *session,
                                  bool *approved)
{
  char devices[64];
  char decision_fd[32];
  const char *app_id =
      session->app_id[0] != '\0' ? session->app_id : "(unknown)";
  int pipefd[2];
  pid_t pid;
  bool spawn_prepared = false;
  int r;

  r = remotedesktop_format_devices(session->selected_device_types, devices,
                                   sizeof(devices));
  if (r < 0) return r;

  if (pipe(pipefd) < 0) return -errno;

  r = inject_prepare_approval_window(NULL, select_term_app_id,
                                     HEVEL_RD_APPROVAL_WIDTH,
                                     HEVEL_RD_APPROVAL_HEIGHT);
  if (r < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -EIO;
  }
  spawn_prepared = true;

  pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    if (spawn_prepared) inject_cancel_prepared_spawn(NULL);
    return -errno;
  }

  if (pid == 0) {
    close(pipefd[0]);

    if (snprintf(decision_fd, sizeof(decision_fd), "%d", pipefd[1]) >=
        (int)sizeof(decision_fd))
      _exit(127);
    if (setenv("HEVEL_APPROVE_FD", decision_fd, 1) < 0) _exit(127);

    execl(HEVEL_RD_APPROVAL_TERMINAL, term, term_flag, select_term_app_id, "-T",
          HEVEL_RD_APPROVAL_TITLE, "-e", HEVEL_RD_APPROVAL_HELPER,
          "--approve-ui", app_id, devices, NULL);
    _exit(127);
  }

  close(pipefd[1]);
  session->prompt_pid = pid;
  session->prompt_visible = true;

  r = remotedesktop_read_approval_decision(pipefd[0], approved);
  close(pipefd[0]);
  remotedesktop_stop_prompt(session);
  if (spawn_prepared && inject_cancel_prepared_spawn(NULL) < 0)
    fprintf(stderr, "hevel: failed to roll back prepared approval spawn\n");
  return r;
}

static int
remotedesktop_method_close_request(sd_bus_message *m, void *userdata,
                                   sd_bus_error *ret_error)
{
  struct hevel_portal_request *request = userdata;
  int r;

  (void)ret_error;

  r = sd_bus_reply_method_return(m, "");
  remotedesktop_destroy_request(request);
  return r;
}

static int
remotedesktop_method_close_session(sd_bus_message *m, void *userdata,
                                   sd_bus_error *ret_error)
{
  struct hevel_rd_session *session = userdata;
  int r;

  (void)ret_error;

  r = sd_bus_reply_method_return(m, "");
  remotedesktop_destroy_session(session, true);
  return r;
}

static int
remotedesktop_method_create_session(sd_bus_message *m, void *userdata,
                                    sd_bus_error *ret_error)
{
  const char *handle;
  const char *session_handle;
  const char *app_id;
  struct hevel_portal_request *request = NULL;
  struct hevel_rd_session *session = NULL;
  int r;

  (void)userdata;
  (void)ret_error;

  r = sd_bus_message_read(m, "oos", &handle, &session_handle, &app_id);
  if (r < 0) return r;

  r = remotedesktop_parse_options(m, NULL, NULL, NULL, NULL);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed CreateSession options");

  r = remotedesktop_new_request(handle, session_handle, app_id, &request);
  if (r == -EEXIST)
    return remotedesktop_reply_rejected(m, "rejecting duplicate request",
                                        handle);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "cannot export request object");

  r = remotedesktop_new_session(session_handle, app_id, &session);
  if (r == -EEXIST)
    return remotedesktop_destroy_request_and_reject(
        m, request, "rejecting duplicate session", session_handle);
  if (r == -EBUSY)
    return remotedesktop_destroy_request_and_reject(
        m, request, "rejecting second active session", session_handle);
  if (r < 0) {
    remotedesktop_destroy_request(request);
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "cannot export session object");
  }

  remotedesktop_log("created session", session_handle, "");
  r = remotedesktop_reply_session_created(m, session);
  remotedesktop_destroy_request(request);
  return r;
}

static int
remotedesktop_method_select_devices(sd_bus_message *m, void *userdata,
                                    sd_bus_error *ret_error)
{
  const char *handle;
  const char *session_handle;
  const char *app_id;
  struct hevel_portal_request *request = NULL;
  struct hevel_rd_session *session = NULL;
  uint32_t types = 0;
  uint32_t selected;
  bool have_types = false;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "oos", &handle, &session_handle, &app_id);
  if (r < 0) return r;

  r = remotedesktop_parse_options(m, &types, &have_types, NULL, NULL);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed SelectDevices options");
  if (!have_types)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "missing SelectDevices types");

  r = remotedesktop_new_request(handle, session_handle, app_id, &request);
  if (r == -EEXIST)
    return remotedesktop_reply_rejected(m, "rejecting duplicate request",
                                        handle);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "cannot export request object");

  r = remotedesktop_require_session(m, session_handle, app_id, &session);
  if (r != 0) {
    remotedesktop_destroy_request(request);
    return r;
  }

  selected = types & HEVEL_RD_AVAILABLE_DEVICE_TYPES;
  if (selected == 0)
    return remotedesktop_destroy_request_and_reject(
        m, request, "rejecting unsupported device mask", session_handle);

  if (session->state == HEVEL_RD_SELECTED &&
      session->selected_device_types == selected)
  {
    r = remotedesktop_reply_devices(m, selected);
    remotedesktop_destroy_request(request);
    return r;
  }

  r = remotedesktop_require_lifecycle(m, session, HEVEL_RD_CREATED,
                                      "SelectDevices");
  if (r != 0) {
    remotedesktop_destroy_request(request);
    return r;
  }

  session->selected_device_types = selected;
  remotedesktop_transition(session, HEVEL_RD_SELECTED);
  r = remotedesktop_reply_devices(m, selected);
  remotedesktop_destroy_request(request);
  return r;
}

static int
remotedesktop_method_start(sd_bus_message *m, void *userdata,
                           sd_bus_error *ret_error)
{
  const char *handle;
  const char *session_handle;
  const char *app_id;
  const char *parent_window;
  struct hevel_portal_request *request = NULL;
  struct hevel_rd_session *session = NULL;
  uint32_t persist_mode = 0;
  bool have_persist_mode = false;
  bool approved = false;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "ooss", &handle, &session_handle, &app_id,
                          &parent_window);
  if (r < 0) return r;

  r = remotedesktop_parse_options(m, NULL, NULL, &persist_mode,
                                  &have_persist_mode);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed Start options");
  if (have_persist_mode && persist_mode != 0)
    return remotedesktop_reply_rejected(m, "rejecting unsupported persist_mode",
                                        session_handle);

  r = remotedesktop_new_request(handle, session_handle, app_id, &request);
  if (r == -EEXIST)
    return remotedesktop_reply_rejected(m, "rejecting duplicate request",
                                        handle);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "cannot export request object");

  r = remotedesktop_require_session(m, session_handle, app_id, &session);
  if (r != 0) {
    remotedesktop_destroy_request(request);
    return r;
  }

  r = remotedesktop_require_lifecycle(m, session, HEVEL_RD_SELECTED, "Start");
  if (r != 0) {
    remotedesktop_destroy_request(request);
    return r;
  }
  if (session->selected_device_types == 0)
    return remotedesktop_destroy_request_and_reject(
        m, request, "rejecting Start without devices", session_handle);

  snprintf(session->parent_window, sizeof(session->parent_window), "%s",
           parent_window ? parent_window : "");
  session->approved = false;
  session->approval_state = HEVEL_RD_APPROVAL_PENDING;
  remotedesktop_transition(session, HEVEL_RD_PENDING_APPROVAL);

  r = remotedesktop_prompt_for_approval(session, &approved);
  if (r < 0) {
    session->approval_state = HEVEL_RD_APPROVAL_DENIED;
    r = remotedesktop_reply_rejected(m, "rejecting Start after approval failure",
                                     session_handle);
    remotedesktop_destroy_session(session, true);
    return r;
  }
  if (!approved) {
    session->approval_state = HEVEL_RD_APPROVAL_DENIED;
    r = remotedesktop_reply_rejected(m, "rejecting Start after approval denial",
                                     session_handle);
    remotedesktop_destroy_session(session, true);
    return r;
  }

  session->approved = true;
  session->approval_state = HEVEL_RD_APPROVAL_ALLOWED;
  remotedesktop_transition(session, HEVEL_RD_STARTED);
  r = remotedesktop_reply_devices(m, session->selected_device_types);
  remotedesktop_destroy_request(request);
  return r;
}

static int
remotedesktop_method_connect_to_eis(sd_bus_message *m, void *userdata,
                                    sd_bus_error *ret_error)
{
  const char *session_handle;
  const char *app_id;
  struct hevel_rd_session *session = NULL;
  uint32_t capabilities;
  int fd;
  int r;

  (void)userdata;

  r = sd_bus_message_read(m, "os", &session_handle, &app_id);
  if (r < 0) return r;

  r = remotedesktop_parse_options(m, NULL, NULL, NULL, NULL);
  if (r < 0)
    return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                  "malformed ConnectToEIS options");

  session = remotedesktop_find_session(session_handle);
  if (!session)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "unknown RemoteDesktop session");
  if (strcmp(session->app_id, app_id ? app_id : "") != 0)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "RemoteDesktop app id mismatch");
  if (session->eis_fd_issued)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "RemoteDesktop EIS fd already issued");
  if (session->state == HEVEL_RD_PENDING_APPROVAL ||
      session->approval_state == HEVEL_RD_APPROVAL_PENDING)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "RemoteDesktop session is pending approval");
  if (session->approval_state == HEVEL_RD_APPROVAL_DENIED)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "RemoteDesktop session was denied");
  if (session->state != HEVEL_RD_STARTED)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "RemoteDesktop session is not started");
  if (!session->approved)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "RemoteDesktop session is not approved");

  capabilities = remotedesktop_map_eis_capabilities(
      session->selected_device_types);
  fd = inject_open_eis_fd(NULL, session->session_id, capabilities,
                          session->app_id[0] != '\0' ? session->app_id
                                                     : "hevel-portal");
  if (fd < 0)
    return sd_bus_error_set_const(ret_error, remotedesktop_portal_error,
                                  "cannot open compositor EIS fd");

  session->eis_fd_issued = true;
  remotedesktop_transition(session, HEVEL_RD_CONNECTED);
  r = sd_bus_reply_method_return(m, "h", fd);
  close(fd);
  return r;
}

static int
remotedesktop_method_unsupported(sd_bus_message *m, void *userdata,
                                 sd_bus_error *ret_error)
{
  (void)m;
  (void)userdata;
  return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_NOT_SUPPORTED,
                                "hevel RemoteDesktop notify path is not implemented");
}

static int
remotedesktop_property_available_device_types(sd_bus *bus, const char *path,
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
  return sd_bus_message_append(reply, "u", HEVEL_RD_AVAILABLE_DEVICE_TYPES);
}

static int
remotedesktop_property_session_version(sd_bus *bus, const char *path,
                                       const char *interface,
                                       const char *property,
                                       sd_bus_message *reply, void *userdata,
                                       sd_bus_error *ret_error)
{
  (void)bus;
  (void)path;
  (void)interface;
  (void)property;
  (void)userdata;
  (void)ret_error;
  return sd_bus_message_append(reply, "u",
                               HEVEL_REMOTEDESKTOP_SESSION_VERSION);
}

static int
remotedesktop_property_version(sd_bus *bus, const char *path,
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
  return sd_bus_message_append(reply, "u", HEVEL_REMOTEDESKTOP_VERSION);
}

void
remotedesktop_finalize(void)
{
  if (!portal.remotedesktop_sessions.next || !portal.requests.next) return;

  while (!wl_list_empty(&portal.remotedesktop_sessions)) {
    struct hevel_rd_session *session =
        wl_container_of(portal.remotedesktop_sessions.next, session, link);
    remotedesktop_destroy_session(session, false);
  }

  while (!wl_list_empty(&portal.requests)) {
    struct hevel_portal_request *request =
        wl_container_of(portal.requests.next, request, link);
    remotedesktop_destroy_request(request);
  }

  wl_list_init(&portal.remotedesktop_sessions);
  wl_list_init(&portal.requests);
}

const sd_bus_vtable remotedesktop_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CreateSession", "oosa{sv}", "ua{sv}",
                  remotedesktop_method_create_session,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SelectDevices", "oosa{sv}", "ua{sv}",
                  remotedesktop_method_select_devices,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Start", "oossa{sv}", "ua{sv}",
                  remotedesktop_method_start, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyPointerMotion", "oa{sv}dd", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyPointerMotionAbsolute", "oa{sv}udd", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyPointerButton", "oa{sv}iu", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyPointerAxis", "oa{sv}dd", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyPointerAxisDiscrete", "oa{sv}ui", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyKeyboardKeycode", "oa{sv}iu", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyKeyboardKeysym", "oa{sv}iu", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyTouchDown", "oa{sv}uudd", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyTouchMotion", "oa{sv}uudd", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyTouchUp", "oa{sv}u", "",
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ConnectToEIS", "osa{sv}", "h",
                  remotedesktop_method_connect_to_eis,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("AvailableDeviceTypes", "u",
                    remotedesktop_property_available_device_types, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("version", "u", remotedesktop_property_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};
