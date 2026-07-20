/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef void (*LucImageProgressFunc)(goffset completed,
                                     goffset total,
                                     gpointer user_data);

gchar *luc_image_sha256(const gchar *path,
                        GCancellable *cancellable,
                        GError **error);

gboolean luc_image_copy_regular_file(const gchar *source_path,
                                     const gchar *destination_path,
                                     gboolean verify,
                                     GCancellable *cancellable,
                                     LucImageProgressFunc progress,
                                     gpointer user_data,
                                     GError **error);

G_END_DECLS

