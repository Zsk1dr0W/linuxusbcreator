/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean luc_windows_bios_install_boot_records(const gchar *device_path,
                                               const gchar *partition_path,
                                               GError **error);

G_END_DECLS
