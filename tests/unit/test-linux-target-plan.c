/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <string.h>

#include "core/linux-target-plan.h"

static void
test_plan(void)
{
    const gchar *plan = luc_linux_target_sfdisk_plan();

    g_assert_nonnull(strstr(plan, "label: gpt"));
    g_assert_nonnull(strstr(plan, "size=2097152"));
    g_assert_nonnull(strstr(plan, "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"));
    g_assert_nonnull(strstr(plan, "0fc63daf-8483-4772-8e79-3d69d8477de4"));
}

static void
test_paths(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *scsi_boot =
        luc_linux_target_partition_path("/dev/sdb", 1, &error);
    g_autofree gchar *nvme_data =
        luc_linux_target_partition_path("/dev/nvme0n1", 2, &error);

    g_assert_no_error(error);
    g_assert_cmpstr(scsi_boot, ==, "/dev/sdb1");
    g_assert_cmpstr(nvme_data, ==, "/dev/nvme0n1p2");
    g_assert_null(luc_linux_target_partition_path("/tmp/disk", 1, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/linux-target/plan", test_plan);
    g_test_add_func("/linux-target/paths", test_paths);
    return g_test_run();
}
