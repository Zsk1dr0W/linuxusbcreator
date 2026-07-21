/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define LUC_LINUX_BOOT_LABEL "LUC-BOOT"
#define LUC_LINUX_DATA_LABEL "LUC-LIVE"
#define LUC_LINUX_BOOT_SIZE_BYTES (1024ULL * 1024ULL * 1024ULL)
#define LUC_LINUX_FILESYSTEM_OVERHEAD (256ULL * 1024ULL * 1024ULL)

const gchar *luc_linux_target_sfdisk_plan(void);
gchar *luc_linux_target_partition_path(const gchar *device_path,
                                       guint partition_number,
                                       GError **error);

G_END_DECLS
