/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _LucIsoMount LucIsoMount;

LucIsoMount *luc_iso_mount_open(const gchar *image_path,
                                GCancellable *cancellable,
                                GError **error);
const gchar *luc_iso_mount_get_path(const LucIsoMount *mount);
gboolean luc_iso_mount_close(LucIsoMount *mount,
                             GCancellable *cancellable,
                             GError **error);
void luc_iso_mount_free(LucIsoMount *mount);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LucIsoMount, luc_iso_mount_free)

G_END_DECLS
