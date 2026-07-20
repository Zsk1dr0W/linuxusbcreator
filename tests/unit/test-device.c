/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include "core/device.h"

static LucDevice *
eligible_device(void)
{
    LucDevice *device = luc_device_new();
    device->device = g_strdup("/dev/sdb");
    device->model = g_strdup("Test USB");
    device->connection_bus = g_strdup("usb");
    device->size = G_GUINT64_CONSTANT(16) * 1024 * 1024 * 1024;
    device->removable = TRUE;
    device->media_available = TRUE;
    return device;
}

static void
test_eligible_usb(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_ELIGIBLE);
}

static void
test_system_device_is_rejected(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    device->system_device = TRUE;
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_REJECT_SYSTEM);
}

static void
test_non_usb_is_rejected(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    g_free(device->connection_bus);
    device->connection_bus = g_strdup("ata");
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_REJECT_NOT_USB);
}

static void
test_fixed_usb_is_rejected(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    device->removable = FALSE;
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_REJECT_NOT_REMOVABLE);
}

static void
test_mounted_device_is_rejected(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    device->mounted = TRUE;
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_REJECT_MOUNTED);
}

static void
test_mounted_device_is_write_candidate(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    device->serial = g_strdup("SERIAL-1");
    device->mounted = TRUE;
    g_assert_true(luc_device_is_write_candidate(device));
    device->read_only = TRUE;
    g_assert_false(luc_device_is_write_candidate(device));
}

static void
test_swap_precedes_mount_rejection(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    device->mounted = TRUE;
    device->active_swap = TRUE;
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_REJECT_ACTIVE_SWAP);
}

static void
test_empty_media_is_rejected_first(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    device->media_available = FALSE;
    device->system_device = TRUE;
    g_assert_cmpint(luc_device_get_eligibility(device), ==, LUC_DEVICE_REJECT_NO_MEDIA);
}

static void
test_copy_is_independent(void)
{
    g_autoptr(LucDevice) original = eligible_device();
    g_autoptr(LucDevice) copy = luc_device_copy(original);
    g_assert_cmpstr(original->device, ==, copy->device);
    g_assert_true(original->device != copy->device);
}

static void
test_confirmation_accepts_expected_device(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    g_autoptr(GError) error = NULL;

    device->serial = g_strdup("SERIAL-1");
    g_assert_true(luc_device_validate_confirmation(device, "SERIAL-1", device->size,
                                                   TRUE, &error));
    g_assert_no_error(error);
}

static void
test_confirmation_rejects_root_device(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    g_autoptr(GError) error = NULL;

    device->serial = g_strdup("SERIAL-1");
    device->system_device = TRUE;
    g_assert_false(luc_device_validate_confirmation(device, "SERIAL-1", device->size,
                                                    TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
}

static void
test_confirmation_rejects_identity_change(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    g_autoptr(GError) error = NULL;

    device->serial = g_strdup("SERIAL-1");
    g_assert_false(luc_device_validate_confirmation(device, "SERIAL-2", device->size,
                                                    TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
}

static void
test_confirmation_requires_unmounted_device(void)
{
    g_autoptr(LucDevice) device = eligible_device();
    g_autoptr(GError) error = NULL;

    device->serial = g_strdup("SERIAL-1");
    device->mounted = TRUE;
    g_assert_false(luc_device_validate_confirmation(device, "SERIAL-1", device->size,
                                                    TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/device/eligible-usb", test_eligible_usb);
    g_test_add_func("/device/reject-system", test_system_device_is_rejected);
    g_test_add_func("/device/reject-non-usb", test_non_usb_is_rejected);
    g_test_add_func("/device/reject-fixed-usb", test_fixed_usb_is_rejected);
    g_test_add_func("/device/reject-mounted", test_mounted_device_is_rejected);
    g_test_add_func("/device/write-candidate-mounted", test_mounted_device_is_write_candidate);
    g_test_add_func("/device/reject-active-swap", test_swap_precedes_mount_rejection);
    g_test_add_func("/device/reject-no-media", test_empty_media_is_rejected_first);
    g_test_add_func("/device/copy", test_copy_is_independent);
    g_test_add_func("/device/confirmation-accept", test_confirmation_accepts_expected_device);
    g_test_add_func("/device/confirmation-root", test_confirmation_rejects_root_device);
    g_test_add_func("/device/confirmation-identity", test_confirmation_rejects_identity_change);
    g_test_add_func("/device/confirmation-mounted", test_confirmation_requires_unmounted_device);
    return g_test_run();
}
