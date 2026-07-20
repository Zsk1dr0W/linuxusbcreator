/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "core/windows-media.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_BUFFER_SIZE (4U * 1024U * 1024U)
#define WIM_SPLIT_SIZE_MB "3800"

typedef struct {
    const gchar *source_root;
    const gchar *destination_root;
    const LucWindowsImageInfo *info;
    GCancellable *cancellable;
    LucWindowsMediaProgressFunc progress_func;
    gpointer user_data;
    guint64 completed;
    guint64 progress_total;
    guint64 last_reported;
} CopyContext;

static gboolean
check_cancelled(GCancellable *cancellable, GError **error)
{
    if (cancellable == NULL || !g_cancellable_is_cancelled(cancellable))
        return TRUE;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "Windows media creation was cancelled");
    return FALSE;
}

static gchar *
normalize_relative(const gchar *relative)
{
    gchar *value = g_strdup(relative != NULL ? relative : "");

    for (gchar *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '\\')
            *cursor = '/';
    }
    return value;
}

static gboolean
is_install_wim(const CopyContext *context, const gchar *relative)
{
    g_autofree gchar *entry = normalize_relative(relative);
    g_autofree gchar *install = normalize_relative(context->info->install_path);

    return context->info->requires_wim_split &&
           g_ascii_strcasecmp(entry, install) == 0;
}

static void
report_absolute(CopyContext *context, guint64 completed)
{
    guint64 threshold = MAX(context->progress_total / 100, 1);

    context->completed = MIN(completed, context->progress_total);
    if (context->progress_func != NULL &&
        (context->completed == 0 ||
         context->completed == context->progress_total ||
         context->completed - context->last_reported >= threshold)) {
        context->progress_func(context->completed, context->progress_total,
                               context->user_data);
        context->last_reported = context->completed;
    }
}

static void
report_progress(CopyContext *context, guint64 amount)
{
    report_absolute(context,
                    G_MAXUINT64 - context->completed < amount
                        ? context->progress_total
                        : context->completed + amount);
}

static gboolean
copy_regular_file(CopyContext *context,
                  const gchar *source,
                  const gchar *destination,
                  guint64 expected_size,
                  GError **error)
{
    g_autofree guint8 *buffer = NULL;
    guint64 written = 0;
    int source_fd = -1;
    int destination_fd = -1;
    gboolean success = FALSE;

    source_fd = g_open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (source_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open source file '%s': %s",
                    source, g_strerror(errno));
        goto out;
    }
    destination_fd = g_open(destination,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                            0644);
    if (destination_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to create destination file '%s': %s",
                    destination, g_strerror(errno));
        goto out;
    }
    buffer = g_malloc(COPY_BUFFER_SIZE);
    while (written < expected_size) {
        gsize request = (gsize)MIN((guint64)COPY_BUFFER_SIZE,
                                   expected_size - written);
        ssize_t count;

        if (!check_cancelled(context->cancellable, error))
            goto out;
        do {
            count = read(source_fd, buffer, request);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) {
            g_set_error(error, G_IO_ERROR,
                        count == 0 ? G_IO_ERROR_PARTIAL_INPUT
                                   : g_io_error_from_errno(errno),
                        "Short read from '%s' after %" G_GUINT64_FORMAT " bytes",
                        source, written);
            goto out;
        }
        for (ssize_t offset = 0; offset < count;) {
            ssize_t output;

            do {
                output = write(destination_fd, buffer + offset,
                               (gsize)(count - offset));
            } while (output < 0 && errno == EINTR);
            if (output <= 0) {
                g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                            "Short write to '%s' after %" G_GUINT64_FORMAT " bytes",
                            destination, written + (guint64)offset);
                goto out;
            }
            offset += output;
        }
        written += (guint64)count;
        report_progress(context, (guint64)count);
    }
    {
        guint8 extra;
        ssize_t count;

        do {
            count = read(source_fd, &extra, 1);
        } while (count < 0 && errno == EINTR);
        if (count != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Source file changed while copying: %s", source);
            goto out;
        }
    }
    if (fsync(destination_fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to synchronize '%s': %s",
                    destination, g_strerror(errno));
        goto out;
    }
    success = TRUE;

out:
    if (destination_fd >= 0)
        close(destination_fd);
    if (source_fd >= 0)
        close(source_fd);
    if (!success)
        g_unlink(destination);
    return success;
}

static gboolean
copy_directory(CopyContext *context, const gchar *relative, GError **error)
{
    g_autofree gchar *source_dir = NULL;
    g_autofree gchar *destination_dir = NULL;
    g_autoptr(GDir) directory = NULL;
    const gchar *name;

    source_dir = relative[0] == '\0'
                     ? g_strdup(context->source_root)
                     : g_build_filename(context->source_root, relative, NULL);
    destination_dir = relative[0] == '\0'
                          ? g_strdup(context->destination_root)
                          : g_build_filename(context->destination_root, relative, NULL);
    if (relative[0] != '\0' && g_mkdir(destination_dir, 0755) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to create destination directory '%s': %s",
                    destination_dir, g_strerror(errno));
        return FALSE;
    }
    directory = g_dir_open(source_dir, 0, error);
    if (directory == NULL)
        return FALSE;
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *child_relative = NULL;
        g_autofree gchar *source = NULL;
        g_autofree gchar *destination = NULL;
        struct stat metadata;

        if (!check_cancelled(context->cancellable, error))
            return FALSE;
        if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL ||
            g_str_equal(name, ".") || g_str_equal(name, "..")) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unsafe source entry name: %s", name);
            return FALSE;
        }
        child_relative = relative[0] == '\0'
                             ? g_strdup(name)
                             : g_build_filename(relative, name, NULL);
        source = g_build_filename(context->source_root, child_relative, NULL);
        destination = g_build_filename(context->destination_root,
                                       child_relative, NULL);
        if (g_lstat(source, &metadata) != 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Unable to inspect source entry '%s': %s",
                        source, g_strerror(errno));
            return FALSE;
        }
        if (S_ISLNK(metadata.st_mode) ||
            (!S_ISDIR(metadata.st_mode) && !S_ISREG(metadata.st_mode))) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unsupported source entry type: %s", source);
            return FALSE;
        }
        if (S_ISDIR(metadata.st_mode)) {
            if (!copy_directory(context, child_relative, error))
                return FALSE;
        } else if (!is_install_wim(context, child_relative)) {
            if ((guint64)metadata.st_size > LUC_FAT32_MAX_FILE_SIZE) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "File exceeds the FAT32 limit: %s", source);
                return FALSE;
            }
            if (!copy_regular_file(context, source, destination,
                                   (guint64)metadata.st_size, error))
                return FALSE;
        }
    }
    return TRUE;
}

static gboolean
parse_latest_percentage(const gchar *text, guint *percentage)
{
    gboolean found = FALSE;

    for (const gchar *cursor = text; (cursor = strchr(cursor, '%')) != NULL;
         cursor++) {
        const gchar *start = cursor;
        gchar *end = NULL;
        guint64 value;

        while (start > text && g_ascii_isdigit(start[-1]))
            start--;
        if (start == cursor)
            continue;
        value = g_ascii_strtoull(start, &end, 10);
        if (end == cursor && value <= 100) {
            *percentage = (guint)value;
            found = TRUE;
        }
    }
    return found;
}

static gboolean
run_wimlib(const gchar *const *argv,
           GCancellable *cancellable,
           CopyContext *context,
           guint range_start,
           guint range_end,
           GError **error)
{
    g_autoptr(GSubprocess) process = NULL;
    g_autoptr(GString) diagnostic_text = g_string_new(NULL);
    g_autoptr(GString) parse_window = g_string_new(NULL);
    GInputStream *output_stream;
    const gchar *diagnostic;
    gsize length;

    process = g_subprocess_newv(argv,
                                G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                G_SUBPROCESS_FLAGS_STDERR_MERGE,
                                error);
    if (process == NULL)
        return FALSE;
    report_absolute(context, range_start);
    output_stream = g_subprocess_get_stdout_pipe(process);
    for (;;) {
        gchar buffer[4096];
        gssize count = g_input_stream_read(output_stream, buffer,
                                           sizeof(buffer), cancellable, error);

        if (count < 0) {
            g_subprocess_force_exit(process);
            g_subprocess_wait(process, NULL, NULL);
            return FALSE;
        }
        if (count == 0)
            break;
        g_string_append_len(diagnostic_text, buffer, count);
        if (diagnostic_text->len > 16384)
            g_string_erase(diagnostic_text, 0, diagnostic_text->len - 16384);
        g_string_append_len(parse_window, buffer, count);
        if (parse_window->len > 8192)
            g_string_erase(parse_window, 0, parse_window->len - 8192);
        {
            guint percentage;
            if (parse_latest_percentage(parse_window->str, &percentage))
                report_absolute(context,
                                range_start +
                                    ((range_end - range_start) * percentage) / 100);
        }
    }
    if (!g_subprocess_wait(process, cancellable, error))
        return FALSE;
    if (g_subprocess_get_successful(process))
    {
        report_absolute(context, range_end);
        return TRUE;
    }
    diagnostic = diagnostic_text->len > 0
                     ? g_strstrip(diagnostic_text->str) : "unknown error";
    length = strlen(diagnostic);
    if (length > 4096)
        diagnostic += length - 4096;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "wimlib-imagex failed: %s", diagnostic);
    return FALSE;
}

static gboolean
split_install_wim(CopyContext *context, GError **error)
{
    g_autofree gchar *tool = g_find_program_in_path("wimlib-imagex");
    g_autofree gchar *source = NULL;
    g_autofree gchar *sources_dir = NULL;
    g_autofree gchar *destination = NULL;
    g_autofree gchar *reference = NULL;
    g_autoptr(GDir) directory = NULL;
    const gchar *split_argv[6];
    const gchar *verify_argv[5];
    const gchar *name;
    guint fragments = 0;

    if (tool == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "wimlib-imagex is required to split install.wim");
        return FALSE;
    }
    source = g_build_filename(context->source_root, context->info->install_path, NULL);
    {
        g_autofree gchar *relative_sources =
            g_path_get_dirname(context->info->install_path);
        sources_dir = g_build_filename(context->destination_root,
                                       relative_sources, NULL);
    }
    destination = g_build_filename(sources_dir, "install.swm", NULL);
    split_argv[0] = tool;
    split_argv[1] = "split";
    split_argv[2] = source;
    split_argv[3] = destination;
    split_argv[4] = WIM_SPLIT_SIZE_MB;
    split_argv[5] = NULL;
    if (!run_wimlib(split_argv, context->cancellable, context, 0, 50, error))
        return FALSE;
    directory = g_dir_open(sources_dir, 0, error);
    if (directory == NULL)
        return FALSE;
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *path = NULL;
        struct stat metadata;

        if (!(g_ascii_strcasecmp(name, "install.swm") == 0 ||
              (g_ascii_strncasecmp(name, "install", 7) == 0 &&
               g_str_has_suffix(name, ".swm"))))
            continue;
        path = g_build_filename(sources_dir, name, NULL);
        if (g_lstat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
            (guint64)metadata.st_size > LUC_FAT32_MAX_FILE_SIZE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "wimlib produced an invalid FAT32 fragment: %s", path);
            return FALSE;
        }
        fragments++;
    }
    if (fragments < 2) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "wimlib did not produce a split WIM set");
        return FALSE;
    }
    reference = g_strdup_printf("--ref=%s/install*.swm", sources_dir);
    verify_argv[0] = tool;
    verify_argv[1] = "verify";
    verify_argv[2] = destination;
    verify_argv[3] = reference;
    verify_argv[4] = NULL;
    return run_wimlib(verify_argv, context->cancellable, context, 50, 100, error);
}

static gboolean
sync_destination(const gchar *path, GError **error)
{
    int fd = g_open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY, 0);

    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open destination for synchronization: %s",
                    g_strerror(errno));
        return FALSE;
    }
    if (fsync(fd) != 0) {
        int saved_errno = errno;
        close(fd);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "Unable to synchronize destination: %s",
                    g_strerror(saved_errno));
        return FALSE;
    }
    close(fd);
    return TRUE;
}

static gboolean
verify_regular_file(CopyContext *context,
                    const gchar *source,
                    const gchar *destination,
                    guint64 expected_size,
                    GError **error)
{
    g_autofree guint8 *source_buffer = g_malloc(COPY_BUFFER_SIZE);
    g_autofree guint8 *destination_buffer = g_malloc(COPY_BUFFER_SIZE);
    guint64 verified = 0;
    int source_fd = -1;
    int destination_fd = -1;
    gboolean success = FALSE;

    source_fd = g_open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    destination_fd = g_open(destination, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (source_fd < 0 || destination_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open files for verification: %s",
                    g_strerror(errno));
        goto out;
    }
    while (verified < expected_size) {
        gsize request = (gsize)MIN((guint64)COPY_BUFFER_SIZE,
                                   expected_size - verified);
        ssize_t source_count;
        ssize_t destination_count;

        if (!check_cancelled(context->cancellable, error))
            goto out;
        do {
            source_count = read(source_fd, source_buffer, request);
        } while (source_count < 0 && errno == EINTR);
        do {
            destination_count = read(destination_fd, destination_buffer, request);
        } while (destination_count < 0 && errno == EINTR);
        if (source_count <= 0 || destination_count != source_count ||
            memcmp(source_buffer, destination_buffer, (gsize)source_count) != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Verification mismatch for '%s' after %" G_GUINT64_FORMAT
                        " bytes", destination, verified);
            goto out;
        }
        verified += (guint64)source_count;
        report_progress(context, (guint64)source_count);
    }
    {
        guint8 extra;
        if (read(destination_fd, &extra, 1) != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Destination file has an unexpected size: %s",
                        destination);
            goto out;
        }
    }
    success = TRUE;

out:
    if (destination_fd >= 0)
        close(destination_fd);
    if (source_fd >= 0)
        close(source_fd);
    return success;
}

static gboolean
verify_directory(CopyContext *context, const gchar *relative, GError **error)
{
    g_autofree gchar *source_dir = relative[0] == '\0'
                                         ? g_strdup(context->source_root)
                                         : g_build_filename(context->source_root,
                                                            relative, NULL);
    g_autoptr(GDir) directory = g_dir_open(source_dir, 0, error);
    const gchar *name;

    if (directory == NULL)
        return FALSE;
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *child_relative = relative[0] == '\0'
                                               ? g_strdup(name)
                                               : g_build_filename(relative,
                                                                  name, NULL);
        g_autofree gchar *source =
            g_build_filename(context->source_root, child_relative, NULL);
        g_autofree gchar *destination =
            g_build_filename(context->destination_root, child_relative, NULL);
        struct stat source_metadata;
        struct stat destination_metadata;

        if (g_lstat(source, &source_metadata) != 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Unable to inspect source during verification: %s",
                        source);
            return FALSE;
        }
        if (S_ISDIR(source_metadata.st_mode)) {
            if (g_lstat(destination, &destination_metadata) != 0 ||
                !S_ISDIR(destination_metadata.st_mode)) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Destination directory is missing: %s", destination);
                return FALSE;
            }
            if (!verify_directory(context, child_relative, error))
                return FALSE;
        } else if (!is_install_wim(context, child_relative)) {
            if (g_lstat(destination, &destination_metadata) != 0 ||
                !S_ISREG(destination_metadata.st_mode) ||
                (guint64)destination_metadata.st_size !=
                    (guint64)source_metadata.st_size) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Destination entry is missing or invalid: %s",
                            destination);
                return FALSE;
            }
            if (!verify_regular_file(context, source, destination,
                                     (guint64)source_metadata.st_size, error))
                return FALSE;
        }
    }
    return TRUE;
}

gboolean
luc_windows_media_copy(const gchar *source_root,
                       const gchar *destination_root,
                       const LucWindowsImageInfo *info,
                       GCancellable *cancellable,
                       LucWindowsMediaPhaseFunc phase_func,
                       LucWindowsMediaProgressFunc progress_func,
                       gpointer user_data,
                       GError **error)
{
    CopyContext context = {
        .source_root = source_root,
        .destination_root = destination_root,
        .info = info,
        .cancellable = cancellable,
        .progress_func = progress_func,
        .user_data = user_data,
        .progress_total = info->requires_wim_split
                              ? info->content_size - info->install_size
                              : info->content_size,
    };
    struct stat source_metadata;
    struct stat destination_metadata;

    g_return_val_if_fail(source_root != NULL, FALSE);
    g_return_val_if_fail(destination_root != NULL, FALSE);
    g_return_val_if_fail(info != NULL, FALSE);
    if (!info->is_windows_installer || !info->fat32_compatible ||
        (!info->supports_uefi_x64 && !info->supports_uefi_arm64) ||
        (info->requires_wim_split &&
         info->install_payload != LUC_WINDOWS_INSTALL_PAYLOAD_WIM)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Windows image is not compatible with UEFI/FAT32 media");
        return FALSE;
    }
    if (g_lstat(source_root, &source_metadata) != 0 ||
        !S_ISDIR(source_metadata.st_mode) ||
        g_lstat(destination_root, &destination_metadata) != 0 ||
        !S_ISDIR(destination_metadata.st_mode)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Source and destination must be existing directories");
        return FALSE;
    }
    if (phase_func != NULL)
        phase_func(LUC_WINDOWS_MEDIA_PHASE_COPYING, user_data);
    report_absolute(&context, 0);
    if (!copy_directory(&context, "", error))
        return FALSE;
    report_absolute(&context, context.progress_total);
    if (info->requires_wim_split) {
        if (phase_func != NULL)
            phase_func(LUC_WINDOWS_MEDIA_PHASE_SPLITTING, user_data);
        context.completed = 0;
        context.last_reported = 0;
        context.progress_total = 100;
        if (!split_install_wim(&context, error))
            return FALSE;
    }
    if (phase_func != NULL)
        phase_func(LUC_WINDOWS_MEDIA_PHASE_SYNCING, user_data);
    context.completed = 0;
    context.last_reported = 0;
    context.progress_total = 1;
    report_absolute(&context, 0);
    if (!sync_destination(destination_root, error))
        return FALSE;
    report_absolute(&context, 1);
    context.completed = 0;
    context.last_reported = 0;
    context.progress_total = info->requires_wim_split
                                 ? info->content_size - info->install_size
                                 : info->content_size;
    if (phase_func != NULL)
        phase_func(LUC_WINDOWS_MEDIA_PHASE_VERIFYING, user_data);
    report_absolute(&context, 0);
    if (!verify_directory(&context, "", error))
        return FALSE;
    report_absolute(&context, context.progress_total);
    return TRUE;
}
