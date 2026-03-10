#include "hevel.h"

#include <errno.h>

#include <systemd/sd-bus.h>

extern const sd_bus_vtable inputcapture_vtable[];
extern const sd_bus_vtable remotedesktop_vtable[];

static const char *portal_bus_name = "org.freedesktop.impl.portal.desktop.hevel";
static const char *portal_object_path = "/org/freedesktop/portal/desktop";

struct portal_state portal = {0};

int
portal_initialize(void)
{
  portal.available = true;

  if (eis_initialize() < 0)
    fprintf(stderr, "hevel: continuing without an active EIS context\n");

  return 0;
}

void
portal_finalize(void)
{
  eis_finalize();
  portal.available = false;
}

int
portal_service_main(void)
{
  sd_bus *bus = NULL;
  int r;

  portal_initialize();

  r = sd_bus_default_user(&bus);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot open user bus: %s\n", strerror(-r));
    portal_finalize();
    return 1;
  }

  r = sd_bus_add_object_vtable(bus, NULL, portal_object_path,
                               "org.freedesktop.impl.portal.InputCapture",
                               inputcapture_vtable, NULL);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot export InputCapture scaffold: %s\n",
            strerror(-r));
    sd_bus_unref(bus);
    portal_finalize();
    return 1;
  }

  r = sd_bus_add_object_vtable(bus, NULL, portal_object_path,
                               "org.freedesktop.impl.portal.RemoteDesktop",
                               remotedesktop_vtable, NULL);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot export RemoteDesktop scaffold: %s\n",
            strerror(-r));
    sd_bus_unref(bus);
    portal_finalize();
    return 1;
  }

  r = sd_bus_request_name(bus, portal_bus_name, 0);
  if (r < 0) {
    fprintf(stderr, "hevel: cannot acquire portal bus name: %s\n",
            strerror(-r));
    sd_bus_unref(bus);
    portal_finalize();
    return 1;
  }

  fprintf(stderr, "hevel: portal scaffold active on %s\n", portal_bus_name);

  while (true) {
    r = sd_bus_process(bus, NULL);
    if (r < 0) {
      fprintf(stderr, "hevel: portal service failed: %s\n", strerror(-r));
      sd_bus_unref(bus);
      portal_finalize();
      return 1;
    }
    if (r > 0) continue;

    r = sd_bus_wait(bus, UINT64_MAX);
    if (r < 0) {
      fprintf(stderr, "hevel: portal wait failed: %s\n", strerror(-r));
      sd_bus_unref(bus);
      portal_finalize();
      return 1;
    }
  }
}
