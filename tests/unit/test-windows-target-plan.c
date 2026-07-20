/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>

#include "core/windows-target-plan.h"

static void
test_partition_paths(void)
{
    g_autofree gchar *scsi = luc_windows_target_partition_path("/dev/sdb", NULL);
    g_autofree gchar *nvme =
        luc_windows_target_partition_path("/dev/nvme0n1", NULL);

    g_assert_cmpstr(scsi, ==, "/dev/sdb1");
    g_assert_cmpstr(nvme, ==, "/dev/nvme0n1p1");
}

static void
test_rejects_unsafe_path(void)
{
    g_autoptr(GError) error = NULL;

    g_assert_null(luc_windows_target_partition_path("/dev/disk/by-id/x", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_fixed_gpt_plan(void)
{
    const gchar *plan = luc_windows_target_sfdisk_plan(LUC_WINDOWS_FIRMWARE_UEFI);

    g_assert_nonnull(strstr(plan, "label: gpt"));
    g_assert_nonnull(strstr(plan, "first-lba: 2048"));
    g_assert_nonnull(strstr(plan,
        "type=ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"));
    g_assert_null(strstr(plan,
        "type=c12a7328-f81f-11d2-ba4b-00a0c93ec93b"));
    g_assert_null(strstr(plan, "attrs="));
    g_assert_nonnull(strstr(plan, "name=\"LINUX USB CREATOR\""));
}

static void
test_fixed_bios_plan(void)
{
    const gchar *plan = luc_windows_target_sfdisk_plan(LUC_WINDOWS_FIRMWARE_BIOS);

    g_assert_nonnull(strstr(plan, "label: dos"));
    g_assert_nonnull(strstr(plan, "type=0c"));
    g_assert_nonnull(strstr(plan, "bootable"));
}

static void
test_firmware_names(void)
{
    LucWindowsFirmware firmware;

    g_assert_true(luc_windows_firmware_from_string("bios", &firmware));
    g_assert_cmpint(firmware, ==, LUC_WINDOWS_FIRMWARE_BIOS);
    g_assert_cmpstr(luc_windows_firmware_to_string(firmware), ==, "bios");
    g_assert_false(luc_windows_firmware_from_string("legacy", &firmware));
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/windows-target/partition-paths", test_partition_paths);
    g_test_add_func("/windows-target/reject-path", test_rejects_unsafe_path);
    g_test_add_func("/windows-target/gpt-plan", test_fixed_gpt_plan);
    g_test_add_func("/windows-target/bios-plan", test_fixed_bios_plan);
    g_test_add_func("/windows-target/firmware-names", test_firmware_names);
    return g_test_run();
}
