/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "core/image-writer.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LUC_COPY_BUFFER_SIZE (1024 * 1024)

static gboolean
check_cancelled(GCancellable *cancellable, GError **error)
{
    if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Operation cancelled");
        return FALSE;
    }
    return TRUE;
}

static gboolean
open_regular_source(const gchar *path, int *fd, struct stat *metadata, GError **error)
{
    *fd = g_open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (*fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open image '%s': %s", path, g_strerror(errno));
        return FALSE;
    }
    if (fstat(*fd, metadata) != 0 || !S_ISREG(metadata->st_mode)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Image source '%s' is not a regular file", path);
        close(*fd);
        *fd = -1;
        return FALSE;
    }
    return TRUE;
}

static gboolean
read_exact(int fd, guchar *buffer, gsize requested, gsize *received, GError **error)
{
    ssize_t count;

    do {
        count = read(fd, buffer, requested);
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to read image: %s", g_strerror(errno));
        return FALSE;
    }
    *received = (gsize)count;
    return TRUE;
}

static gboolean
hash_fd_prefix(int fd,
               guint64 length,
               GCancellable *cancellable,
               gchar **digest,
               GError **error)
{
    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
    g_autofree guchar *buffer = g_malloc(LUC_COPY_BUFFER_SIZE);
    guint64 completed = 0;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to seek for verification: %s", g_strerror(errno));
        return FALSE;
    }
    while (completed < length) {
        gsize requested = (gsize)MIN((guint64)LUC_COPY_BUFFER_SIZE, length - completed);
        gsize received = 0;
        if (!check_cancelled(cancellable, error) ||
            !read_exact(fd, buffer, requested, &received, error))
            return FALSE;
        if (received == 0) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                                "Device ended before verification completed");
            return FALSE;
        }
        g_checksum_update(checksum, buffer, received);
        completed += received;
    }
    *digest = g_strdup(g_checksum_get_string(checksum));
    return TRUE;
}

gchar *
luc_image_sha256(const gchar *path, GCancellable *cancellable, GError **error)
{
    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
    g_autofree guchar *buffer = g_malloc(LUC_COPY_BUFFER_SIZE);
    struct stat metadata;
    int fd = -1;

    g_return_val_if_fail(path != NULL, NULL);
    if (!open_regular_source(path, &fd, &metadata, error))
        return NULL;

    while (TRUE) {
        gsize received = 0;
        if (!check_cancelled(cancellable, error) ||
            !read_exact(fd, buffer, LUC_COPY_BUFFER_SIZE, &received, error)) {
            close(fd);
            return NULL;
        }
        if (received == 0)
            break;
        g_checksum_update(checksum, buffer, received);
    }
    close(fd);
    return g_strdup(g_checksum_get_string(checksum));
}

gboolean
luc_image_write_all_fd(int fd, const guint8 *buffer, gsize length, GError **error)
{
    gsize offset = 0;
    while (offset < length) {
        ssize_t count = write(fd, buffer + offset, length - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Unable to write image: %s", g_strerror(errno));
            return FALSE;
        }
        if (count == 0) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Image write made no progress");
            return FALSE;
        }
        offset += (gsize)count;
    }
    return TRUE;
}

gboolean
luc_image_copy_regular_file(const gchar *source_path,
                            const gchar *destination_path,
                            gboolean verify,
                            GCancellable *cancellable,
                            LucImageProgressFunc progress,
                            gpointer user_data,
                            GError **error)
{
    struct stat source_metadata;
    struct stat destination_metadata;
    g_autofree guchar *buffer = g_malloc(LUC_COPY_BUFFER_SIZE);
    g_autofree gchar *source_digest = NULL;
    g_autofree gchar *destination_digest = NULL;
    goffset completed = 0;
    int source_fd = -1;
    int destination_fd = -1;
    gboolean success = FALSE;

    g_return_val_if_fail(source_path != NULL, FALSE);
    g_return_val_if_fail(destination_path != NULL, FALSE);
    if (!open_regular_source(source_path, &source_fd, &source_metadata, error))
        return FALSE;

    destination_fd = g_open(destination_path,
                             O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                             0600);
    if (destination_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open destination '%s': %s",
                    destination_path, g_strerror(errno));
        close(source_fd);
        return FALSE;
    }
    if (fstat(destination_fd, &destination_metadata) != 0 ||
        !S_ISREG(destination_metadata.st_mode)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Destination must be a regular file; block devices are refused");
        goto cleanup;
    }

    if (progress != NULL)
        progress(0, source_metadata.st_size, user_data);
    while (completed < source_metadata.st_size) {
        gsize received = 0;
        if (!check_cancelled(cancellable, error) ||
            !read_exact(source_fd, buffer, LUC_COPY_BUFFER_SIZE, &received, error) ||
            !luc_image_write_all_fd(destination_fd, buffer, received, error))
            goto cleanup;
        completed += (goffset)received;
        if (progress != NULL)
            progress(completed, source_metadata.st_size, user_data);
    }
    if (fsync(destination_fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to flush destination: %s", g_strerror(errno));
        goto cleanup;
    }
    if (verify) {
        source_digest = luc_image_sha256(source_path, cancellable, error);
        destination_digest = luc_image_sha256(destination_path, cancellable, error);
        if (source_digest == NULL || destination_digest == NULL)
            goto cleanup;
        if (!g_str_equal(source_digest, destination_digest)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Verification failed: image checksums differ");
            goto cleanup;
        }
    }
    success = TRUE;

cleanup:
    if (destination_fd >= 0)
        close(destination_fd);
    if (source_fd >= 0)
        close(source_fd);
    return success;
}

gboolean
luc_image_write_block_device(const gchar *source_path,
                             const gchar *device_path,
                             gboolean verify,
                             GCancellable *cancellable,
                             LucImageProgressFunc progress,
                             gpointer user_data,
                             GError **error)
{
    struct stat source_metadata;
    struct stat device_metadata;
    g_autofree guchar *buffer = g_malloc(LUC_COPY_BUFFER_SIZE);
    g_autofree gchar *source_digest = NULL;
    g_autofree gchar *device_digest = NULL;
    guint64 device_size = 0;
    guint64 completed = 0;
    int source_fd = -1;
    int device_fd = -1;
    gboolean success = FALSE;

    g_return_val_if_fail(source_path != NULL, FALSE);
    g_return_val_if_fail(device_path != NULL, FALSE);
    if (!open_regular_source(source_path, &source_fd, &source_metadata, error))
        return FALSE;
    device_fd = g_open(device_path, O_WRONLY | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0);
    if (device_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to exclusively open device '%s': %s",
                    device_path, g_strerror(errno));
        goto cleanup;
    }
    if (fstat(device_fd, &device_metadata) != 0 || !S_ISBLK(device_metadata.st_mode)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Destination is not a block device");
        goto cleanup;
    }
    if (ioctl(device_fd, BLKGETSIZE64, &device_size) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to determine device capacity: %s", g_strerror(errno));
        goto cleanup;
    }
    if ((guint64)source_metadata.st_size > device_size) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Image is larger than the destination device");
        goto cleanup;
    }
    if (progress != NULL)
        progress(0, source_metadata.st_size, user_data);
    while (completed < (guint64)source_metadata.st_size) {
        gsize requested = (gsize)MIN((guint64)LUC_COPY_BUFFER_SIZE,
                                     (guint64)source_metadata.st_size - completed);
        gsize received = 0;
        if (!check_cancelled(cancellable, error) ||
            !read_exact(source_fd, buffer, requested, &received, error))
            goto cleanup;
        if (received == 0) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                                "Image ended during write");
            goto cleanup;
        }
        if (!luc_image_write_all_fd(device_fd, buffer, received, error))
            goto cleanup;
        completed += received;
        if (progress != NULL)
            progress(completed, source_metadata.st_size, user_data);
    }
    if (fsync(device_fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to flush device: %s", g_strerror(errno));
        goto cleanup;
    }
    close(device_fd);
    device_fd = -1;

    if (verify) {
        source_digest = luc_image_sha256(source_path, cancellable, error);
        if (source_digest == NULL)
            goto cleanup;
        device_fd = g_open(device_path, O_RDONLY | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0);
        if (device_fd < 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Unable to reopen device for verification: %s", g_strerror(errno));
            goto cleanup;
        }
        if (!hash_fd_prefix(device_fd, source_metadata.st_size, cancellable,
                            &device_digest, error))
            goto cleanup;
        if (!g_str_equal(source_digest, device_digest)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Verification failed: device checksum differs");
            goto cleanup;
        }
    }
    success = TRUE;

cleanup:
    if (device_fd >= 0)
        close(device_fd);
    if (source_fd >= 0)
        close(source_fd);
    return success;
}
