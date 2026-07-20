/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "core/windows-bios-boot.h"

static void
test_boot_records_preserve_layout(void)
{
    g_autofree gchar *disk_path = NULL;
    g_autofree gchar *partition_path = NULL;
    g_autoptr(GError) error = NULL;
    guint8 disk[512];
    guint8 partition[8192];
    int disk_fd = g_file_open_tmp("luc-bios-disk-XXXXXX", &disk_path, &error);
    int partition_fd;

    g_assert_no_error(error);
    g_assert_cmpint(disk_fd, >=, 0);
    memset(disk, 0xa5, sizeof(disk));
    g_assert_cmpint(write(disk_fd, disk, sizeof(disk)), ==, sizeof(disk));
    close(disk_fd);
    partition_fd = g_file_open_tmp("luc-bios-partition-XXXXXX",
                                   &partition_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(partition_fd, >=, 0);
    memset(partition, 0x5a, sizeof(partition));
    g_assert_cmpint(write(partition_fd, partition, sizeof(partition)),
                    ==, sizeof(partition));
    close(partition_fd);

    g_assert_true(luc_windows_bios_install_boot_records(
        disk_path, partition_path, &error));
    g_assert_no_error(error);
    disk_fd = g_open(disk_path, O_RDONLY, 0);
    partition_fd = g_open(partition_path, O_RDONLY, 0);
    g_assert_cmpint(read(disk_fd, disk, sizeof(disk)), ==, sizeof(disk));
    g_assert_cmpint(read(partition_fd, partition, sizeof(partition)),
                    ==, sizeof(partition));
    close(disk_fd);
    close(partition_fd);
    g_assert_cmphex(disk[0x1be], ==, 0xa5);
    g_assert_cmphex(disk[0x1fe], ==, 0x55);
    g_assert_cmphex(disk[0x1ff], ==, 0xaa);
    g_assert_cmphex(partition[0x0b], ==, 0x5a);
    g_assert_cmphex(partition[0x1c], ==, 0x00);
    g_assert_cmphex(partition[0x1d], ==, 0x08);
    g_assert_cmphex(partition[0x40], ==, 0x80);
    g_assert_cmphex(partition[0x1fe], ==, 0x55);
    g_assert_cmphex(partition[0x1ff], ==, 0xaa);
    g_assert_cmphex(partition[0x5fe], ==, 0x55);
    g_assert_cmphex(partition[0x5ff], ==, 0xaa);
    g_assert_cmpmem(partition, 512, partition + 6 * 512, 512);

    g_unlink(disk_path);
    g_unlink(partition_path);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/windows-bios/boot-records", test_boot_records_preserve_layout);
    return g_test_run();
}
