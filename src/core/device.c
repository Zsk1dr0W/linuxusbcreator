/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/device.h"

LucDevice *
luc_device_new(void)
{
    return g_new0(LucDevice, 1);
}

LucDevice *
luc_device_copy(const LucDevice *source)
{
    LucDevice *copy;

    g_return_val_if_fail(source != NULL, NULL);
    copy = luc_device_new();
    *copy = *source;
    copy->object_path = g_strdup(source->object_path);
    copy->drive_path = g_strdup(source->drive_path);
    copy->device = g_strdup(source->device);
    copy->vendor = g_strdup(source->vendor);
    copy->model = g_strdup(source->model);
    copy->serial = g_strdup(source->serial);
    copy->connection_bus = g_strdup(source->connection_bus);
    return copy;
}

void
luc_device_free(LucDevice *device)
{
    if (device == NULL)
        return;

    g_free(device->object_path);
    g_free(device->drive_path);
    g_free(device->device);
    g_free(device->vendor);
    g_free(device->model);
    g_free(device->serial);
    g_free(device->connection_bus);
    g_free(device);
}

LucDeviceEligibility
luc_device_get_eligibility(const LucDevice *device)
{
    g_return_val_if_fail(device != NULL, LUC_DEVICE_REJECT_UNSUPPORTED);

    if (!device->media_available || device->size == 0)
        return LUC_DEVICE_REJECT_NO_MEDIA;
    if (device->device == NULL || device->device[0] == '\0')
        return LUC_DEVICE_REJECT_NO_BLOCK_DEVICE;
    if (device->ignored_kind || device->optical)
        return LUC_DEVICE_REJECT_UNSUPPORTED;
    if (device->system_device)
        return LUC_DEVICE_REJECT_SYSTEM;
    if (g_strcmp0(device->connection_bus, "usb") != 0)
        return LUC_DEVICE_REJECT_NOT_USB;
    if (!device->removable && !device->media_removable)
        return LUC_DEVICE_REJECT_NOT_REMOVABLE;
    if (device->active_swap)
        return LUC_DEVICE_REJECT_ACTIVE_SWAP;
    if (device->mounted)
        return LUC_DEVICE_REJECT_MOUNTED;
    if (device->read_only)
        return LUC_DEVICE_REJECT_READ_ONLY;

    return LUC_DEVICE_ELIGIBLE;
}

const gchar *
luc_device_eligibility_to_string(LucDeviceEligibility eligibility)
{
    switch (eligibility) {
    case LUC_DEVICE_ELIGIBLE:
        return "eligible";
    case LUC_DEVICE_REJECT_NO_MEDIA:
        return "no-media";
    case LUC_DEVICE_REJECT_NO_BLOCK_DEVICE:
        return "no-block-device";
    case LUC_DEVICE_REJECT_NOT_USB:
        return "not-usb";
    case LUC_DEVICE_REJECT_NOT_REMOVABLE:
        return "not-removable";
    case LUC_DEVICE_REJECT_SYSTEM:
        return "system-device";
    case LUC_DEVICE_REJECT_MOUNTED:
        return "mounted";
    case LUC_DEVICE_REJECT_ACTIVE_SWAP:
        return "active-swap";
    case LUC_DEVICE_REJECT_READ_ONLY:
        return "read-only";
    case LUC_DEVICE_REJECT_UNSUPPORTED:
    default:
        return "unsupported";
    }
}

gchar *
luc_device_get_display_name(const LucDevice *device)
{
    const gchar *vendor;
    const gchar *model;

    g_return_val_if_fail(device != NULL, NULL);
    vendor = device->vendor != NULL ? device->vendor : "";
    model = device->model != NULL ? device->model : "";

    if (*vendor != '\0' && *model != '\0')
        return g_strdup_printf("%s %s", vendor, model);
    if (*model != '\0')
        return g_strdup(model);
    if (*vendor != '\0')
        return g_strdup(vendor);
    if (device->device != NULL)
        return g_path_get_basename(device->device);
    return g_strdup("Unknown USB device");
}

