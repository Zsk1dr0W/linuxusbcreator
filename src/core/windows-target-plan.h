/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
    LUC_WINDOWS_FIRMWARE_UEFI,
    LUC_WINDOWS_FIRMWARE_BIOS,
} LucWindowsFirmware;

const gchar *luc_windows_firmware_to_string(LucWindowsFirmware firmware);
gboolean luc_windows_firmware_from_string(const gchar *text,
                                          LucWindowsFirmware *firmware);
const gchar *luc_windows_target_sfdisk_plan(LucWindowsFirmware firmware);
gchar *luc_windows_target_partition_path(const gchar *device_path,
                                         GError **error);

G_END_DECLS
