/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _LucFilesystemMount LucFilesystemMount;

LucFilesystemMount *luc_filesystem_mount_open(const gchar *device_path,
                                               GCancellable *cancellable,
                                               GError **error);
LucFilesystemMount *luc_filesystem_mount_open_type(const gchar *device_path,
                                                   const gchar *filesystem_type,
                                                   GCancellable *cancellable,
                                                   GError **error);
const gchar *luc_filesystem_mount_get_path(const LucFilesystemMount *mount);
gboolean luc_filesystem_mount_close(LucFilesystemMount *mount,
                                    GCancellable *cancellable,
                                    GError **error);
void luc_filesystem_mount_free(LucFilesystemMount *mount);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LucFilesystemMount, luc_filesystem_mount_free)

G_END_DECLS
