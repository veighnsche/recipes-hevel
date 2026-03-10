#include "hevel.h"

#include <errno.h>

#include <systemd/sd-bus.h>

#define HEVEL_REMOTEDESKTOP_RESPONSE_OTHER 2U
#define HEVEL_REMOTEDESKTOP_VERSION 2U

static int
remotedesktop_reply_response(sd_bus_message *m, uint32_t response)
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
remotedesktop_method_cancelled(sd_bus_message *m, void *userdata,
                               sd_bus_error *ret_error)
{
  (void)userdata;
  (void)ret_error;
  return remotedesktop_reply_response(m, HEVEL_REMOTEDESKTOP_RESPONSE_OTHER);
}

static int
remotedesktop_method_unsupported(sd_bus_message *m, void *userdata,
                                 sd_bus_error *ret_error)
{
  (void)m;
  (void)userdata;
  return sd_bus_error_set_const(ret_error, SD_BUS_ERROR_NOT_SUPPORTED,
                                "hevel RemoteDesktop is not implemented yet");
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
  return sd_bus_message_append(reply, "u", 0U);
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

const sd_bus_vtable remotedesktop_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CreateSession", "oosa{sv}", "ua{sv}",
                  remotedesktop_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SelectDevices", "oosa{sv}", "ua{sv}",
                  remotedesktop_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Start", "oossa{sv}", "ua{sv}",
                  remotedesktop_method_cancelled, SD_BUS_VTABLE_UNPRIVILEGED),
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
                  remotedesktop_method_unsupported,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("AvailableDeviceTypes", "u",
                    remotedesktop_property_available_device_types, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("version", "u", remotedesktop_property_version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};
