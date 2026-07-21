/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/linux-target-plan.h"

#include <string.h>

const gchar *
luc_linux_target_sfdisk_plan(void)
{
    return "label: gpt\n"
           "unit: sectors\n"
           "first-lba: 2048\n"
           "\n"
           "size=2097152, type=ebd0a0a2-b9e5-4433-87c0-68b6b72699c7, name=\"Linux USB boot\"\n"
           "type=0fc63daf-8483-4772-8e79-3d69d8477de4, name=\"Linux USB live\"\n";
}

gchar *
luc_linux_target_partition_path(const gchar *device_path,
                                guint partition_number,
                                GError **error)
{
    const gchar *separator;

    g_return_val_if_fail(device_path != NULL, NULL);
    if (partition_number < 1 || partition_number > 2 ||
        !g_str_has_prefix(device_path, "/dev/") ||
        strchr(device_path + 5, '/') != NULL ||
        strchr(device_path, '\\') != NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Linux target must be a whole direct /dev path");
        return NULL;
    }
    separator = g_ascii_isdigit(device_path[strlen(device_path) - 1]) ? "p" : "";
    return g_strdup_printf("%s%s%u", device_path, separator, partition_number);
}
