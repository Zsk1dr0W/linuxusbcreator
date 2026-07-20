/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>
#include "core/device.h"

G_BEGIN_DECLS

#define LUC_TYPE_DEVICE_MONITOR (luc_device_monitor_get_type())
G_DECLARE_FINAL_TYPE(LucDeviceMonitor, luc_device_monitor, LUC, DEVICE_MONITOR, GObject)

LucDeviceMonitor *luc_device_monitor_new(GError **error);
GPtrArray *luc_device_monitor_dup_devices(LucDeviceMonitor *self);
LucDevice *luc_device_monitor_find_device(LucDeviceMonitor *self,
                                          const gchar *device_path);
gboolean luc_device_monitor_unmount_drive(LucDeviceMonitor *self,
                                          const gchar *drive_path,
                                          GError **error);

G_END_DECLS
