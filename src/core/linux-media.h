/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
    LUC_LINUX_MEDIA_PHASE_COPYING,
    LUC_LINUX_MEDIA_PHASE_CONFIGURING,
    LUC_LINUX_MEDIA_PHASE_SYNCING,
    LUC_LINUX_MEDIA_PHASE_VERIFYING,
} LucLinuxMediaPhase;

typedef void (*LucLinuxMediaPhaseFunc)(LucLinuxMediaPhase phase,
                                       gpointer user_data);
typedef void (*LucLinuxMediaProgressFunc)(guint64 completed,
                                          guint64 total,
                                          gpointer user_data);

gboolean luc_linux_media_validate_fedora(const gchar *source_root,
                                         guint64 *content_size,
                                         GError **error);
gboolean luc_linux_media_copy_fedora(const gchar *source_root,
                                     const gchar *boot_root,
                                     const gchar *data_root,
                                     gboolean persistence,
                                     GCancellable *cancellable,
                                     LucLinuxMediaPhaseFunc phase_func,
                                     LucLinuxMediaProgressFunc progress_func,
                                     gpointer user_data,
                                     GError **error);

G_END_DECLS
