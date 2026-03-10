#include "hevel.h"

#include <errno.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

extern const sd_bus_vtable inputcapture_vtable[];
extern const sd_bus_vtable remotedesktop_vtable[];

static const char *portal_bus_name = "org.freedesktop.impl.portal.desktop.hevel";
static const char *portal_object_path = "/org/freedesktop/portal/desktop";

struct portal_state portal = {0};

int
portal_initialize(void)
{
  wl_list_init(&portal.requests);
  wl_list_init(&portal.remotedesktop_sessions);
  portal.bus = NULL;
  portal.event = NULL;
  portal.pending_start = NULL;
  portal.next_session_id = 0;
  portal.available = true;
  return 0;
}

void
portal_finalize(void)
{
  remotedesktop_finalize();
  if (portal.bus && portal.event) sd_bus_detach_event(portal.bus);
  if (portal.bus) {
    sd_bus_unref(portal.bus);
    portal.bus = NULL;
  }
  if (portal.event) {
    sd_event_unref(portal.event);
    portal.event = NULL;
  }
  portal.pending_start = NULL;
  portal.available = false;
}

int
portal_service_main(void)
{
  sd_bus *bus = NULL;
  sd_event *event = NULL;
  sigset_t mask;
  int r;

  portal_initialize();

  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
    perror("hevel: cannot block SIGCHLD for portal service");
    portal_finalize();
    return 1;
  }

  r = sd_event_default(&event);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot create portal event loop: %s\n",
            strerror(-r));
    portal_finalize();
    return 1;
  }
  portal.event = event;

  r = sd_bus_default_user(&bus);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot open user bus: %s\n", strerror(-r));
    portal_finalize();
    return 1;
  }
  portal.bus = bus;

  r = sd_bus_attach_event(bus, event, 0);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot attach portal bus to event loop: %s\n",
            strerror(-r));
    portal_finalize();
    return 1;
  }

  r = sd_bus_add_object_vtable(bus, NULL, portal_object_path,
                               "org.freedesktop.impl.portal.InputCapture",
                               inputcapture_vtable, NULL);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot export InputCapture scaffold: %s\n",
            strerror(-r));
    portal_finalize();
    return 1;
  }

  r = sd_bus_add_object_vtable(bus, NULL, portal_object_path,
                               "org.freedesktop.impl.portal.RemoteDesktop",
                               remotedesktop_vtable, NULL);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot export RemoteDesktop backend: %s\n",
            strerror(-r));
    portal_finalize();
    return 1;
  }

  r = sd_bus_request_name(bus, portal_bus_name, 0);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot acquire portal bus name: %s\n",
            strerror(-r));
    portal_finalize();
    return 1;
  }

  fprintf(stderr, "hevel: portal backend active on %s\n", portal_bus_name);

  r = sd_event_loop(event);
  if (r < 0) {
    fprintf(stderr, "hevel: portal event loop failed: %s\n", strerror(-r));
    portal_finalize();
    return 1;
  }

  portal_finalize();
  return r;
}
