/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The boot-record byte sequences are from ms-sys 2.7.1, commit
 * 13c892034fbb6e9bb4302c977e7f88b59df4a0fe, by Henrik Carlqvist and
 * contributors, licensed GPL-2.0-or-later. See third_party/ms-sys/NOTICE.
 */
#define _GNU_SOURCE
#include "core/windows-bios-boot.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "third_party/ms-sys/mbr_win7.h"
#include "third_party/ms-sys/br_fat32nt6_0x0.h"
#include "third_party/ms-sys/br_fat32nt6_0x52.h"
#include "third_party/ms-sys/br_fat32nt6_0x5fe.h"
#include "third_party/ms-sys/br_fat32nt6_0x1800.h"

G_STATIC_ASSERT(sizeof(mbr_win7_0x0) == 440);
G_STATIC_ASSERT(sizeof(br_fat32nt6_0x0) == 11);
G_STATIC_ASSERT(sizeof(br_fat32nt6_0x52) == 430);
G_STATIC_ASSERT(sizeof(br_fat32nt6_0x5fe) == 2);
G_STATIC_ASSERT(sizeof(br_fat32nt6_0x1800) == 512);

static gboolean
write_all_at(int fd, const guint8 *data, gsize length, off_t offset,
             GError **error)
{
    gsize written = 0;

    while (written < length) {
        ssize_t result = pwrite(fd, data + written, length - written,
                                offset + (off_t)written);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0) {
            g_set_error(error, G_IO_ERROR,
                        result < 0 ? g_io_error_from_errno(errno)
                                   : G_IO_ERROR_FAILED,
                        "Unable to write Windows BIOS boot record: %s",
                        result < 0 ? g_strerror(errno) : "short write");
            return FALSE;
        }
        written += (gsize)result;
    }
    return TRUE;
}

static gboolean
read_all_at(int fd, guint8 *data, gsize length, off_t offset, GError **error)
{
    gsize received = 0;

    while (received < length) {
        ssize_t result = pread(fd, data + received, length - received,
                               offset + (off_t)received);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0) {
            g_set_error(error, G_IO_ERROR,
                        result < 0 ? g_io_error_from_errno(errno)
                                   : G_IO_ERROR_FAILED,
                        "Unable to read Windows BIOS boot record: %s",
                        result < 0 ? g_strerror(errno) : "short read");
            return FALSE;
        }
        received += (gsize)result;
    }
    return TRUE;
}

static gboolean
install_mbr(const gchar *path, GError **error)
{
    static const guint8 marker[] = {0x55, 0xaa};
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    gboolean result;

    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open target disk for BIOS boot: %s",
                    g_strerror(errno));
        return FALSE;
    }
    result = write_all_at(fd, mbr_win7_0x0, sizeof(mbr_win7_0x0), 0, error) &&
             write_all_at(fd, marker, sizeof(marker), 0x1fe, error) &&
             fsync(fd) == 0;
    if (!result && error != NULL && *error == NULL)
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to synchronize Windows BIOS MBR: %s",
                    g_strerror(errno));
    close(fd);
    return result;
}

static gboolean
install_fat32_pbr(const gchar *path, GError **error)
{
    static const guint8 hidden_sectors[] = {0x00, 0x08, 0x00, 0x00};
    static const guint8 bios_drive = 0x80;
    guint8 primary_sector[512];
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    gboolean result;

    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open FAT32 partition for BIOS boot: %s",
                    g_strerror(errno));
        return FALSE;
    }
    result = write_all_at(fd, br_fat32nt6_0x0,
                          sizeof(br_fat32nt6_0x0), 0, error) &&
             write_all_at(fd, hidden_sectors, sizeof(hidden_sectors),
                          0x1c, error) &&
             write_all_at(fd, &bios_drive, sizeof(bios_drive), 0x40, error) &&
             write_all_at(fd, br_fat32nt6_0x52,
                          sizeof(br_fat32nt6_0x52), 0x52, error) &&
             write_all_at(fd, br_fat32nt6_0x5fe,
                          sizeof(br_fat32nt6_0x5fe), 0x5fe, error) &&
             write_all_at(fd, br_fat32nt6_0x1800,
                          sizeof(br_fat32nt6_0x1800), 0x1800, error) &&
             read_all_at(fd, primary_sector, sizeof(primary_sector), 0,
                         error) &&
             write_all_at(fd, primary_sector, sizeof(primary_sector),
                          6 * 512, error) &&
             fsync(fd) == 0;
    if (!result && error != NULL && *error == NULL)
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to synchronize Windows BIOS PBR: %s",
                    g_strerror(errno));
    close(fd);
    return result;
}

gboolean
luc_windows_bios_install_boot_records(const gchar *device_path,
                                      const gchar *partition_path,
                                      GError **error)
{
    g_return_val_if_fail(device_path != NULL, FALSE);
    g_return_val_if_fail(partition_path != NULL, FALSE);
    return install_mbr(device_path, error) &&
           install_fat32_pbr(partition_path, error);
}
