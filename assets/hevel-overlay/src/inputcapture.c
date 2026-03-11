#include "hevel.h"
#include "inputcapture.h"

struct inputcapture_state inputcapture = {0};

static const sd_bus_vtable inputcapture_request_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Close", "", "", inputcapture_method_close_request,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable inputcapture_session_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Close", "", "", inputcapture_method_close_session,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Closed", "", 0),
    SD_BUS_PROPERTY("version", "u", inputcapture_property_session_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

#include "inputcapture-signals.c"
#include "inputcapture-barriers.c"
#include "inputcapture-session.c"
#include "inputcapture-zones.c"
#include "inputcapture-methods.c"

static int
inputcapture_property_supported_capabilities(sd_bus *bus, const char *path,
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
  return sd_bus_message_append(reply, "u",
                               HEVEL_INPUTCAPTURE_SUPPORTED_CAPABILITIES);
}

static int
inputcapture_property_version(sd_bus *bus, const char *path,
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
  return sd_bus_message_append(reply, "u", HEVEL_INPUTCAPTURE_VERSION);
}

static int
inputcapture_property_session_version(sd_bus *bus, const char *path,
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
                               HEVEL_INPUTCAPTURE_SESSION_VERSION);
}

const sd_bus_vtable inputcapture_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CreateSession", "oossa{sv}", "ua{sv}",
                  inputcapture_method_create_session,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetZones", "oosa{sv}", "ua{sv}",
                  inputcapture_method_get_zones,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPointerBarriers", "oosa{sv}aa{sv}u", "ua{sv}",
                  inputcapture_method_set_pointer_barriers,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Enable", "osa{sv}", "ua{sv}", inputcapture_method_enable,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Disable", "osa{sv}", "ua{sv}",
                  inputcapture_method_disable, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Release", "osa{sv}", "ua{sv}",
                  inputcapture_method_release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ConnectToEIS", "osa{sv}", "h",
                  inputcapture_method_connect_to_eis,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Disabled", "oa{sv}", 0),
    SD_BUS_SIGNAL("Activated", "oa{sv}", 0),
    SD_BUS_SIGNAL("Deactivated", "oa{sv}", 0),
    SD_BUS_SIGNAL("ZonesChanged", "oa{sv}", 0),
    SD_BUS_PROPERTY("SupportedCapabilities", "u",
                    inputcapture_property_supported_capabilities, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("version", "u", inputcapture_property_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};
