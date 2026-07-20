/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

#include "core/windows-image.h"

G_BEGIN_DECLS

typedef enum {
    LUC_WINDOWS_MEDIA_PHASE_COPYING,
    LUC_WINDOWS_MEDIA_PHASE_SPLITTING,
    LUC_WINDOWS_MEDIA_PHASE_SYNCING,
    LUC_WINDOWS_MEDIA_PHASE_VERIFYING,
} LucWindowsMediaPhase;

typedef void (*LucWindowsMediaPhaseFunc)(LucWindowsMediaPhase phase,
                                         gpointer user_data);
typedef void (*LucWindowsMediaProgressFunc)(guint64 completed,
                                            guint64 total,
                                            gpointer user_data);

gboolean luc_windows_media_copy(const gchar *source_root,
                                const gchar *destination_root,
                                const LucWindowsImageInfo *info,
                                GCancellable *cancellable,
                                LucWindowsMediaPhaseFunc phase_func,
                                LucWindowsMediaProgressFunc progress_func,
                                gpointer user_data,
                                GError **error);

G_END_DECLS
