/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define LUC_TYPE_WRITE_OPERATION (luc_write_operation_get_type())
G_DECLARE_FINAL_TYPE(LucWriteOperation, luc_write_operation, LUC, WRITE_OPERATION, GObject)

LucWriteOperation *luc_write_operation_new(const gchar *image_path,
                                           const gchar *device_path,
                                           const gchar *serial,
                                           guint64 device_size,
                                           gboolean verify);
void luc_write_operation_start(LucWriteOperation *self);
void luc_write_operation_cancel(LucWriteOperation *self);
gboolean luc_write_operation_is_running(LucWriteOperation *self);

G_END_DECLS
