/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "core/linux-media.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/linux-target-plan.h"

#define COPY_BUFFER_SIZE (4U * 1024U * 1024U)
#define MAX_GRUB_CONFIG_SIZE (1024U * 1024U)

typedef struct {
    const gchar *source_root;
    const gchar *boot_root;
    const gchar *data_root;
    gboolean persistence;
    GCancellable *cancellable;
    LucLinuxMediaProgressFunc progress_func;
    gpointer user_data;
    guint64 completed;
    guint64 total;
    guint64 last_reported;
} CopyContext;

static gboolean
check_cancelled(GCancellable *cancellable, GError **error)
{
    if (cancellable == NULL || !g_cancellable_is_cancelled(cancellable))
        return TRUE;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "Linux media creation was cancelled");
    return FALSE;
}

static void
report_progress(CopyContext *context, guint64 amount)
{
    guint64 threshold = MAX(context->total / 100, 1);

    context->completed = MIN(context->completed + amount, context->total);
    if (context->progress_func != NULL &&
        (context->completed == context->total ||
         context->completed - context->last_reported >= threshold)) {
        context->progress_func(context->completed, context->total,
                               context->user_data);
        context->last_reported = context->completed;
    }
}

static gboolean
is_safe_name(const gchar *name)
{
    return name[0] != '\0' && !g_str_equal(name, ".") &&
           !g_str_equal(name, "..") && strchr(name, '/') == NULL &&
           strchr(name, '\\') == NULL;
}

static gboolean
tree_size(const gchar *path, guint64 *total, GError **error)
{
    g_autoptr(GDir) directory = NULL;
    const gchar *name;

    directory = g_dir_open(path, 0, error);
    if (directory == NULL)
        return FALSE;
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *child = NULL;
        struct stat metadata;

        if (!is_safe_name(name)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unsafe Fedora media entry: %s", name);
            return FALSE;
        }
        child = g_build_filename(path, name, NULL);
        if (g_lstat(child, &metadata) != 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Unable to inspect Fedora media entry '%s': %s",
                        child, g_strerror(errno));
            return FALSE;
        }
        if (S_ISDIR(metadata.st_mode)) {
            if (!tree_size(child, total, error))
                return FALSE;
        } else if (S_ISREG(metadata.st_mode)) {
            if (G_MAXUINT64 - *total < (guint64)metadata.st_size) {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TOO_MANY_LINKS,
                                    "Fedora media content size overflow");
                return FALSE;
            }
            *total += (guint64)metadata.st_size;
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unsupported Fedora media entry type: %s", child);
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
luc_linux_media_validate_fedora(const gchar *source_root,
                                guint64 *content_size,
                                GError **error)
{
    static const gchar *const required_files[] = {
        "EFI/BOOT/BOOTX64.EFI",
        "EFI/BOOT/grub.cfg",
        "boot/grub2/grub.cfg",
        "LiveOS/squashfs.img",
        NULL,
    };
    static const gchar *const trees[] = {"EFI", "boot", "LiveOS", NULL};
    guint64 total = 0;

    g_return_val_if_fail(source_root != NULL, FALSE);
    for (guint i = 0; required_files[i] != NULL; i++) {
        g_autofree gchar *path =
            g_build_filename(source_root, required_files[i], NULL);
        struct stat metadata;

        if (g_lstat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                        "Fedora Live profile is missing required file: %s",
                        required_files[i]);
            return FALSE;
        }
    }
    for (guint i = 0; trees[i] != NULL; i++) {
        g_autofree gchar *path = g_build_filename(source_root, trees[i], NULL);
        if (!tree_size(path, &total, error))
            return FALSE;
    }
    if (content_size != NULL)
        *content_size = total;
    return TRUE;
}

static gboolean
files_equal(const gchar *first, const gchar *second, guint64 size, GError **error)
{
    g_autofree guint8 *a = g_malloc(COPY_BUFFER_SIZE);
    g_autofree guint8 *b = g_malloc(COPY_BUFFER_SIZE);
    guint64 compared = 0;
    int first_fd = -1;
    int second_fd = -1;
    gboolean equal = FALSE;

    first_fd = g_open(first, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    second_fd = g_open(second, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (first_fd < 0 || second_fd < 0)
        goto out;
    while (compared < size) {
        gsize request = (gsize)MIN((guint64)COPY_BUFFER_SIZE, size - compared);
        ssize_t ca = read(first_fd, a, request);
        ssize_t cb = read(second_fd, b, request);
        if (ca <= 0 || cb != ca || memcmp(a, b, (gsize)ca) != 0)
            goto out;
        compared += (guint64)ca;
    }
    equal = TRUE;
out:
    if (second_fd >= 0)
        close(second_fd);
    if (first_fd >= 0)
        close(first_fd);
    if (!equal && error != NULL && *error == NULL)
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "FAT case-collision contains different data: %s", first);
    return equal;
}

static gboolean
copy_file(CopyContext *context,
          const gchar *source,
          const gchar *destination,
          guint64 size,
          GError **error)
{
    g_autofree guint8 *buffer = g_malloc(COPY_BUFFER_SIZE);
    guint64 copied = 0;
    int source_fd = -1;
    int destination_fd = -1;
    gboolean success = FALSE;

    source_fd = g_open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (source_fd < 0)
        goto io_error;
    destination_fd = g_open(destination,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                            0644);
    if (destination_fd < 0 && errno == EEXIST) {
        gboolean equal = files_equal(source, destination, size, error);
        close(source_fd);
        source_fd = -1;
        if (!equal)
            goto out;
        report_progress(context, size);
        return TRUE;
    }
    if (destination_fd < 0)
        goto io_error;
    while (copied < size) {
        gsize request = (gsize)MIN((guint64)COPY_BUFFER_SIZE, size - copied);
        ssize_t count;

        if (!check_cancelled(context->cancellable, error))
            goto out;
        do {
            count = read(source_fd, buffer, request);
        } while (count < 0 && errno == EINTR);
        if (count <= 0)
            goto io_error;
        for (ssize_t offset = 0; offset < count;) {
            ssize_t written;
            do {
                written = write(destination_fd, buffer + offset,
                                (gsize)(count - offset));
            } while (written < 0 && errno == EINTR);
            if (written <= 0)
                goto io_error;
            offset += written;
        }
        copied += (guint64)count;
        report_progress(context, (guint64)count);
    }
    if (fsync(destination_fd) != 0)
        goto io_error;
    success = TRUE;
    goto out;

io_error:
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "Unable to copy Fedora media file '%s': %s",
                source, g_strerror(errno));
out:
    if (destination_fd >= 0)
        close(destination_fd);
    if (source_fd >= 0)
        close(source_fd);
    if (!success && destination_fd >= 0)
        g_unlink(destination);
    return success;
}

static gboolean
copy_tree(CopyContext *context,
          const gchar *source,
          const gchar *destination,
          GError **error)
{
    g_autoptr(GDir) directory = NULL;
    const gchar *name;

    if (g_mkdir(destination, 0755) != 0 && errno != EEXIST) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to create Linux media directory '%s': %s",
                    destination, g_strerror(errno));
        return FALSE;
    }
    directory = g_dir_open(source, 0, error);
    if (directory == NULL)
        return FALSE;
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *source_child = NULL;
        g_autofree gchar *destination_child = NULL;
        struct stat metadata;

        if (!check_cancelled(context->cancellable, error))
            return FALSE;
        source_child = g_build_filename(source, name, NULL);
        destination_child = g_build_filename(destination, name, NULL);
        if (!is_safe_name(name) || g_lstat(source_child, &metadata) != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unsafe Fedora media entry: %s", source_child);
            return FALSE;
        }
        if (S_ISDIR(metadata.st_mode)) {
            if (!copy_tree(context, source_child, destination_child, error))
                return FALSE;
        } else if (S_ISREG(metadata.st_mode)) {
            if (!copy_file(context, source_child, destination_child,
                           (guint64)metadata.st_size, error))
                return FALSE;
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unsupported Fedora media entry: %s", source_child);
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
configure_grub(CopyContext *context, GError **error)
{
    g_autofree gchar *path =
        g_build_filename(context->boot_root, "boot/grub2/grub.cfg", NULL);
    g_autofree gchar *contents = NULL;
    g_autofree gchar *root_replaced = NULL;
    g_autofree gchar *configured = NULL;
    g_autoptr(GRegex) root_regex = NULL;
    g_autoptr(GRegex) live_image_regex = NULL;
    gsize length = 0;

    if (!g_file_get_contents(path, &contents, &length, error))
        return FALSE;
    if (length == 0 || length > MAX_GRUB_CONFIG_SIZE) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Fedora GRUB configuration has an unsafe size");
        return FALSE;
    }
    root_regex = g_regex_new("root=live:CDLABEL=[^[:space:]]+", 0, 0, error);
    if (root_regex == NULL)
        return FALSE;
    root_replaced = g_regex_replace_literal(
        root_regex, contents, -1, 0, "root=live:LABEL=" LUC_LINUX_DATA_LABEL,
        0, error);
    if (root_replaced == NULL)
        return FALSE;
    if (g_str_equal(root_replaced, contents)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Fedora GRUB configuration has no supported live root");
        return FALSE;
    }
    if (context->persistence) {
        live_image_regex = g_regex_new("rd\\.live\\.image", 0, 0, error);
        if (live_image_regex == NULL)
            return FALSE;
        configured = g_regex_replace_literal(
            live_image_regex, root_replaced,
            -1, 0,
            "rd.live.image rw rd.live.overlay=LABEL=" LUC_LINUX_DATA_LABEL
            ":/overlayfs rd.live.overlay.overlayfs",
            0, error);
    } else {
        configured = g_strdup(root_replaced);
    }
    if (configured == NULL)
        return FALSE;
    return g_file_set_contents_full(path, configured, -1,
                                    G_FILE_SET_CONTENTS_CONSISTENT, 0644, error);
}

static gboolean
sync_root(const gchar *path, GError **error)
{
    int fd = g_open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY, 0);
    if (fd < 0 || fsync(fd) != 0) {
        int saved_errno = errno;
        if (fd >= 0)
            close(fd);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "Unable to synchronize Linux media: %s",
                    g_strerror(saved_errno));
        return FALSE;
    }
    close(fd);
    return TRUE;
}

static gboolean
verify_tree(CopyContext *context,
            const gchar *source,
            const gchar *destination,
            gboolean boot_tree,
            const gchar *relative,
            GError **error)
{
    g_autoptr(GDir) directory = g_dir_open(source, 0, error);
    const gchar *name;

    if (directory == NULL)
        return FALSE;
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *source_child = g_build_filename(source, name, NULL);
        g_autofree gchar *destination_child =
            g_build_filename(destination, name, NULL);
        g_autofree gchar *child_relative = relative[0] == '\0'
                                               ? g_strdup(name)
                                               : g_build_filename(relative, name, NULL);
        struct stat source_metadata;
        struct stat destination_metadata;

        if (!check_cancelled(context->cancellable, error))
            return FALSE;
        if (g_lstat(source_child, &source_metadata) != 0 ||
            g_lstat(destination_child, &destination_metadata) != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Linux media verification entry is missing: %s",
                        destination_child);
            return FALSE;
        }
        if (S_ISDIR(source_metadata.st_mode)) {
            if (!S_ISDIR(destination_metadata.st_mode) ||
                !verify_tree(context, source_child, destination_child,
                             boot_tree, child_relative, error))
                return FALSE;
        } else if (boot_tree &&
                   g_str_equal(child_relative, "boot/grub2/grub.cfg")) {
            g_autofree gchar *configured = NULL;
            if (!g_file_get_contents(destination_child, &configured, NULL, error) ||
                strstr(configured, "root=live:LABEL=" LUC_LINUX_DATA_LABEL) == NULL ||
                (context->persistence &&
                 strstr(configured, "rd.live.overlay=LABEL=" LUC_LINUX_DATA_LABEL
                                    ":/overlayfs") == NULL)) {
                if (error == NULL || *error == NULL)
                    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                        "Configured Fedora GRUB file failed verification");
                return FALSE;
            }
            report_progress(context, (guint64)source_metadata.st_size);
        } else {
            if (!S_ISREG(destination_metadata.st_mode) ||
                (guint64)source_metadata.st_size !=
                    (guint64)destination_metadata.st_size ||
                !files_equal(source_child, destination_child,
                             (guint64)source_metadata.st_size, error))
                return FALSE;
            report_progress(context, (guint64)source_metadata.st_size);
        }
    }
    return TRUE;
}

gboolean
luc_linux_media_copy_fedora(const gchar *source_root,
                            const gchar *boot_root,
                            const gchar *data_root,
                            gboolean persistence,
                            GCancellable *cancellable,
                            LucLinuxMediaPhaseFunc phase_func,
                            LucLinuxMediaProgressFunc progress_func,
                            gpointer user_data,
                            GError **error)
{
    static const gchar *const boot_trees[] = {"EFI", "boot", NULL};
    guint64 total = 0;
    CopyContext context = {
        .source_root = source_root,
        .boot_root = boot_root,
        .data_root = data_root,
        .persistence = persistence,
        .cancellable = cancellable,
        .progress_func = progress_func,
        .user_data = user_data,
    };

    if (!luc_linux_media_validate_fedora(source_root, &total, error))
        return FALSE;
    context.total = total;
    if (phase_func != NULL)
        phase_func(LUC_LINUX_MEDIA_PHASE_COPYING, user_data);
    if (progress_func != NULL)
        progress_func(0, total, user_data);
    for (guint i = 0; boot_trees[i] != NULL; i++) {
        g_autofree gchar *source =
            g_build_filename(source_root, boot_trees[i], NULL);
        g_autofree gchar *destination =
            g_build_filename(boot_root, boot_trees[i], NULL);
        if (!copy_tree(&context, source, destination, error))
            return FALSE;
    }
    {
        g_autofree gchar *source = g_build_filename(source_root, "LiveOS", NULL);
        g_autofree gchar *destination = g_build_filename(data_root, "LiveOS", NULL);
        if (!copy_tree(&context, source, destination, error))
            return FALSE;
    }
    if (phase_func != NULL)
        phase_func(LUC_LINUX_MEDIA_PHASE_CONFIGURING, user_data);
    if (progress_func != NULL)
        progress_func(0, 1, user_data);
    if (!configure_grub(&context, error))
        return FALSE;
    if (persistence) {
        g_autofree gchar *overlay =
            g_build_filename(data_root, "overlayfs", NULL);
        g_autofree gchar *work = g_build_filename(data_root, "ovlwork", NULL);

        if ((g_mkdir(overlay, 0755) != 0 && errno != EEXIST) ||
            (g_mkdir(work, 0755) != 0 && errno != EEXIST)) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Unable to create Fedora persistence directories: %s",
                        g_strerror(errno));
            return FALSE;
        }
    }
    if (progress_func != NULL)
        progress_func(1, 1, user_data);
    if (phase_func != NULL)
        phase_func(LUC_LINUX_MEDIA_PHASE_SYNCING, user_data);
    if (progress_func != NULL)
        progress_func(0, 2, user_data);
    if (!sync_root(boot_root, error))
        return FALSE;
    if (progress_func != NULL)
        progress_func(1, 2, user_data);
    if (!sync_root(data_root, error))
        return FALSE;
    if (progress_func != NULL)
        progress_func(2, 2, user_data);
    if (phase_func != NULL)
        phase_func(LUC_LINUX_MEDIA_PHASE_VERIFYING, user_data);
    context.completed = 0;
    context.last_reported = 0;
    for (guint i = 0; boot_trees[i] != NULL; i++) {
        g_autofree gchar *source =
            g_build_filename(source_root, boot_trees[i], NULL);
        g_autofree gchar *destination =
            g_build_filename(boot_root, boot_trees[i], NULL);
        if (!verify_tree(&context, source, destination, TRUE,
                         boot_trees[i], error))
            return FALSE;
    }
    {
        g_autofree gchar *source = g_build_filename(source_root, "LiveOS", NULL);
        g_autofree gchar *destination = g_build_filename(data_root, "LiveOS", NULL);
        if (!verify_tree(&context, source, destination, FALSE, "LiveOS", error))
            return FALSE;
    }
    return TRUE;
}
