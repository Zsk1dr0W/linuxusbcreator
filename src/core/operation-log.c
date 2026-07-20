/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "core/operation-log.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gchar *
json_string(const gchar *value)
{
    GString *escaped = g_string_new("\"");
    const guchar *cursor = (const guchar *)(value != NULL ? value : "");

    for (; *cursor != '\0'; cursor++) {
        switch (*cursor) {
        case '\\': g_string_append(escaped, "\\\\"); break;
        case '"': g_string_append(escaped, "\\\""); break;
        case '\n': g_string_append(escaped, "\\n"); break;
        case '\r': g_string_append(escaped, "\\r"); break;
        case '\t': g_string_append(escaped, "\\t"); break;
        default:
            if (*cursor < 0x20)
                g_string_append_printf(escaped, "\\u%04x", *cursor);
            else
                g_string_append_c(escaped, (gchar)*cursor);
        }
    }
    g_string_append_c(escaped, '"');
    return g_string_free(escaped, FALSE);
}

gboolean
luc_operation_log_append(const gchar *path,
                         const gchar *operation,
                         const gchar *status,
                         const gchar *source,
                         const gchar *destination,
                         guint64 bytes,
                         const gchar *sha256,
                         GError **error)
{
    g_autofree gchar *operation_json = json_string(operation);
    g_autofree gchar *status_json = json_string(status);
    g_autofree gchar *source_json = json_string(source);
    g_autofree gchar *destination_json = json_string(destination);
    g_autofree gchar *sha256_json = json_string(sha256);
    g_autofree gchar *line = NULL;
    gint64 timestamp = g_get_real_time();
    int fd;
    ssize_t written;
    gsize length;

    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(operation != NULL, FALSE);
    g_return_val_if_fail(status != NULL, FALSE);
    line = g_strdup_printf(
        "{\"timestamp_us\":%" G_GINT64_FORMAT ",\"operation\":%s,"
        "\"status\":%s,\"source\":%s,\"destination\":%s,"
        "\"bytes\":%" G_GUINT64_FORMAT ",\"sha256\":%s}\n",
        timestamp, operation_json, status_json, source_json, destination_json, bytes,
        sha256_json);
    fd = g_open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open operation log '%s': %s", path, g_strerror(errno));
        return FALSE;
    }
    length = strlen(line);
    written = write(fd, line, length);
    if (written != (ssize_t)length) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to append operation log '%s': %s", path, g_strerror(errno));
        close(fd);
        return FALSE;
    }
    if (fsync(fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to flush operation log '%s': %s", path, g_strerror(errno));
        close(fd);
        return FALSE;
    }
    close(fd);
    return TRUE;
}
