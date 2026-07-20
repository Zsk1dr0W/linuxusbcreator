/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean luc_operation_log_append(const gchar *path,
                                  const gchar *operation,
                                  const gchar *status,
                                  const gchar *source,
                                  const gchar *destination,
                                  guint64 bytes,
                                  const gchar *sha256,
                                  GError **error);

G_END_DECLS
