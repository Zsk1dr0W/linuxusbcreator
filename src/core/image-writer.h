/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef void (*LucImageProgressFunc)(goffset completed,
                                     goffset total,
                                     gpointer user_data);

typedef enum {
    LUC_IMAGE_PHASE_WRITING,
    LUC_IMAGE_PHASE_SYNCING,
    LUC_IMAGE_PHASE_VERIFYING,
} LucImagePhase;

typedef void (*LucImagePhaseFunc)(LucImagePhase phase, gpointer user_data);

/* Low-level primitive exposed so failure and short-write behaviour can be
 * tested without a real block device. */
gboolean luc_image_write_all_fd(int fd,
                                const guint8 *buffer,
                                gsize length,
                                GError **error);

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

/* Privileged callers must validate and unmount the target immediately before
 * calling this function. It accepts whole Linux block devices only. */
gboolean luc_image_write_block_device(const gchar *source_path,
                                      const gchar *device_path,
                                      gboolean verify,
                                      GCancellable *cancellable,
                                      LucImagePhaseFunc phase_changed,
                                      LucImageProgressFunc progress,
                                      gpointer user_data,
                                      GError **error);

G_END_DECLS
