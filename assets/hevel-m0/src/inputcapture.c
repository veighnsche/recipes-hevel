#include "hevel.h"

#include <errno.h>

#include <systemd/sd-bus.h>

#define HEVEL_INPUTCAPTURE_RESPONSE_OTHER 2U
#define HEVEL_INPUTCAPTURE_VERSION 1U

static int
inputcapture_reply_response(sd_bus_message *m, uint32_t response)
{
  sd_bus_message *reply = NULL;
  int r;

  r = sd_bus_message_new_method_return(m, &reply);
  if (r < 0) return r;

  r = sd_bus_message_append(reply, "u", response);
  if (r < 0) goto out;

  r = sd_bus_message_open_container(reply, 'a', "{sv}");
  if (r < 0) goto out;

  r = sd_bus_message_close_container(reply);
  if (r < 0) goto out;

  r = sd_bus_send(sd_bus_message_get_bus(m), reply, NULL);

out:
  sd_bus_message_unref(reply);
  return r;
}

static int
inputcapture_method_cancelled(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error)
{
  (void)userdata;
  (void)ret_error;
  return inputcapture_reply_response(m, HEVEL_INPUTCAPTURE_RESPONSE_OTHER);
}

static int
inputcapture_connect_to_eis(sd_bus_message *m, void *userdata,
                            sd_bus_error *ret_error)
{
  (void)m;
  (void)userdata;
  return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_NOT_SUPPORTED,
                                "hevel InputCapture is not implemented yet");
}

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
  return sd_bus_message_append(reply, "u", 0U);
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

const sd_bus_vtable inputcapture_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CreateSession", "oossa{sv}", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetZones", "oosa{sv}", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPointerBarriers", "oosa{sv}aa{sv}u", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Enable", "osa{sv}", "ua{sv}", inputcapture_method_cancelled,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Disable", "osa{sv}", "ua{sv}",
                  inputcapture_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Release", "osa{sv}", "ua{sv}", inputcapture_method_cancelled,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ConnectToEIS", "osa{sv}", "h", inputcapture_connect_to_eis,
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
