/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/windows-target-plan.h"

#include <string.h>

#define GPT_BASIC_DATA_GUID "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"

const gchar *
luc_windows_firmware_to_string(LucWindowsFirmware firmware)
{
    return firmware == LUC_WINDOWS_FIRMWARE_BIOS ? "bios" : "uefi";
}

gboolean
luc_windows_firmware_from_string(const gchar *text,
                                 LucWindowsFirmware *firmware)
{
    g_return_val_if_fail(firmware != NULL, FALSE);
    if (g_str_equal(text, "uefi"))
        *firmware = LUC_WINDOWS_FIRMWARE_UEFI;
    else if (g_str_equal(text, "bios"))
        *firmware = LUC_WINDOWS_FIRMWARE_BIOS;
    else
        return FALSE;
    return TRUE;
}

const gchar *
luc_windows_target_sfdisk_plan(LucWindowsFirmware firmware)
{
    if (firmware == LUC_WINDOWS_FIRMWARE_BIOS)
        return "label: dos\n"
               "unit: sectors\n"
               "\n"
               "start=2048, type=0c, bootable\n";
    return "label: gpt\n"
           "unit: sectors\n"
           "first-lba: 2048\n"
           "\n"
           "start=2048, type=" GPT_BASIC_DATA_GUID ", "
           "name=\"LINUX USB CREATOR\"\n";
}

gchar *
luc_windows_target_partition_path(const gchar *device_path, GError **error)
{
    g_autofree gchar *basename = NULL;

    g_return_val_if_fail(device_path != NULL, NULL);
    if (!g_str_has_prefix(device_path, "/dev/") ||
        device_path[5] == '\0' || strchr(device_path + 5, '/') != NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Target must be a direct /dev block path");
        return NULL;
    }
    basename = g_path_get_basename(device_path);
    return g_strdup_printf("%s%s1", device_path,
                           g_ascii_isdigit(basename[strlen(basename) - 1])
                               ? "p" : "");
}
