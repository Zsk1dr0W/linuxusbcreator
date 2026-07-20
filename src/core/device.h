/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
    LUC_DEVICE_ELIGIBLE,
    LUC_DEVICE_REJECT_NO_MEDIA,
    LUC_DEVICE_REJECT_NO_BLOCK_DEVICE,
    LUC_DEVICE_REJECT_NOT_USB,
    LUC_DEVICE_REJECT_NOT_REMOVABLE,
    LUC_DEVICE_REJECT_SYSTEM,
    LUC_DEVICE_REJECT_MOUNTED,
    LUC_DEVICE_REJECT_ACTIVE_SWAP,
    LUC_DEVICE_REJECT_READ_ONLY,
    LUC_DEVICE_REJECT_UNSUPPORTED,
} LucDeviceEligibility;

typedef struct {
    gchar *object_path;
    gchar *drive_path;
    gchar *device;
    gchar *vendor;
    gchar *model;
    gchar *serial;
    gchar *connection_bus;
    guint64 size;
    gboolean removable;
    gboolean media_removable;
    gboolean media_available;
    gboolean optical;
    gboolean system_device;
    gboolean mounted;
    gboolean active_swap;
    gboolean read_only;
    gboolean ignored_kind;
} LucDevice;

LucDevice *luc_device_new(void);
LucDevice *luc_device_copy(const LucDevice *device);
void luc_device_free(LucDevice *device);

LucDeviceEligibility luc_device_get_eligibility(const LucDevice *device);
const gchar *luc_device_eligibility_to_string(LucDeviceEligibility eligibility);
gchar *luc_device_get_display_name(const LucDevice *device);
gboolean luc_device_validate_confirmation(const LucDevice *device,
                                          const gchar *expected_serial,
                                          guint64 expected_size,
                                          gboolean require_unmounted,
                                          GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LucDevice, luc_device_free)

G_END_DECLS
