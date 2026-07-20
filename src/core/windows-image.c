/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "core/windows-image.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    gchar *path;
    guint64 size;
    gboolean directory;
    gboolean unsafe_type;
} ListingEntry;

static gboolean
parse_size(const gchar *value, guint64 *size)
{
    gchar *end = NULL;

    if (value == NULL || !g_ascii_isdigit(value[0]))
        return FALSE;
    errno = 0;
    *size = g_ascii_strtoull(value, &end, 10);
    return errno == 0 && end != value && *end == '\0';
}

static gboolean
path_is_safe(const gchar *path)
{
    g_auto(GStrv) parts = NULL;

    if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '\\' ||
        (g_ascii_isalpha(path[0]) && path[1] == ':'))
        return FALSE;
    parts = g_strsplit(path, "/", -1);
    for (guint i = 0; parts[i] != NULL; i++) {
        if (g_str_equal(parts[i], ".."))
            return FALSE;
    }
    return TRUE;
}

static gchar *
normalized_path(const gchar *path)
{
    gchar *normalized = g_strdup(path);

    for (gchar *cursor = normalized; *cursor != '\0'; cursor++) {
        if (*cursor == '\\')
            *cursor = '/';
    }
    while (g_str_has_prefix(normalized, "./"))
        memmove(normalized, normalized + 2, strlen(normalized + 2) + 1);
    return normalized;
}

static void
consume_entry(ListingEntry *entry,
              LucWindowsImageInfo *info,
              GHashTable *paths)
{
    g_autofree gchar *normalized = NULL;
    g_autofree gchar *folded = NULL;

    if (entry->path == NULL)
        return;
    normalized = normalized_path(entry->path);
    if (!path_is_safe(normalized))
        info->has_unsafe_paths = TRUE;
    if (entry->unsafe_type)
        info->has_unsafe_paths = TRUE;
    folded = g_ascii_strdown(normalized, -1);
    if (g_hash_table_contains(paths, folded))
        info->has_case_collisions = TRUE;
    else
        g_hash_table_add(paths, g_strdup(folded));

    if (entry->directory)
        return;
    info->file_count++;
    if (G_MAXUINT64 - info->content_size < entry->size) {
        info->content_size = G_MAXUINT64;
        info->has_unsafe_paths = TRUE;
    } else {
        info->content_size += entry->size;
    }
    info->largest_file_size = MAX(info->largest_file_size, entry->size);
    if (entry->size > LUC_FAT32_MAX_FILE_SIZE)
        info->oversized_file_count++;
    if (g_str_equal(folded, "sources/boot.wim")) {
        info->has_boot_wim = TRUE;
        g_free(info->boot_wim_path);
        info->boot_wim_path = g_strdup(normalized);
    } else if (g_str_equal(folded, "sources/install.wim")) {
        info->has_install_wim = TRUE;
        info->install_payload = LUC_WINDOWS_INSTALL_PAYLOAD_WIM;
        info->install_size = entry->size;
        g_free(info->install_path);
        info->install_path = g_strdup(normalized);
    } else if (g_str_equal(folded, "sources/install.esd")) {
        info->has_install_esd = TRUE;
        info->install_payload = LUC_WINDOWS_INSTALL_PAYLOAD_ESD;
        info->install_size = entry->size;
        g_free(info->install_path);
        info->install_path = g_strdup(normalized);
    } else if (g_str_equal(folded, "efi/boot/bootx64.efi")) {
        info->supports_uefi_x64 = TRUE;
    } else if (g_str_equal(folded, "efi/boot/bootaa64.efi")) {
        info->supports_uefi_arm64 = TRUE;
    } else if (g_str_equal(folded, "bootmgr")) {
        info->has_bootmgr = TRUE;
    } else if (g_str_equal(folded, "boot/bcd")) {
        info->has_boot_bcd = TRUE;
    } else if (g_str_equal(folded, "boot/etfsboot.com")) {
        info->has_bios_boot_image = TRUE;
    }
}

void
luc_windows_image_info_free(LucWindowsImageInfo *info)
{
    if (info == NULL)
        return;
    g_free(info->boot_wim_path);
    g_free(info->install_path);
    g_free(info);
}

const gchar *
luc_windows_image_format_to_string(LucWindowsImageFormat format)
{
    switch (format) {
    case LUC_WINDOWS_IMAGE_FORMAT_ISO9660:
        return "iso9660";
    case LUC_WINDOWS_IMAGE_FORMAT_UDF:
        return "udf";
    case LUC_WINDOWS_IMAGE_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

const gchar *
luc_windows_install_payload_to_string(LucWindowsInstallPayload payload)
{
    switch (payload) {
    case LUC_WINDOWS_INSTALL_PAYLOAD_WIM:
        return "wim";
    case LUC_WINDOWS_INSTALL_PAYLOAD_ESD:
        return "esd";
    case LUC_WINDOWS_INSTALL_PAYLOAD_NONE:
    default:
        return "none";
    }
}

gboolean
luc_windows_image_parse_7z_listing(const gchar *listing,
                                   LucWindowsImageInfo **info,
                                   GError **error)
{
    g_autoptr(LucWindowsImageInfo) parsed = NULL;
    g_autoptr(GHashTable) paths = NULL;
    g_auto(GStrv) lines = NULL;
    ListingEntry entry = {0};
    gboolean entries_started = FALSE;

    g_return_val_if_fail(listing != NULL, FALSE);
    g_return_val_if_fail(info != NULL && *info == NULL, FALSE);
    parsed = g_new0(LucWindowsImageInfo, 1);
    paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    lines = g_strsplit(listing, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        const gchar *value;

        if (g_str_has_prefix(line, "Type = ") && !entries_started) {
            value = line + strlen("Type = ");
            if (g_ascii_strcasecmp(value, "Udf") == 0)
                parsed->format = LUC_WINDOWS_IMAGE_FORMAT_UDF;
            else if (g_ascii_strcasecmp(value, "Iso") == 0)
                parsed->format = LUC_WINDOWS_IMAGE_FORMAT_ISO9660;
        } else if (g_str_equal(line, "----------")) {
            entries_started = TRUE;
        } else if (entries_started && g_str_has_prefix(line, "Path = ")) {
            consume_entry(&entry, parsed, paths);
            g_clear_pointer(&entry.path, g_free);
            entry.size = 0;
            entry.directory = FALSE;
            entry.unsafe_type = FALSE;
            entry.path = g_strdup(line + strlen("Path = "));
        } else if (entries_started && g_str_has_prefix(line, "Size = ")) {
            if (!parse_size(line + strlen("Size = "), &entry.size)) {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                    "Invalid file size in 7-Zip listing");
                g_free(entry.path);
                return FALSE;
            }
        } else if (entries_started && g_str_has_prefix(line, "Attributes = ")) {
            value = line + strlen("Attributes = ");
            entry.directory = strchr(value, 'D') != NULL;
        } else if (entries_started && g_str_has_prefix(line, "Folder = ")) {
            entry.directory = g_str_equal(line + strlen("Folder = "), "+");
        } else if (entries_started &&
                   (g_str_has_prefix(line, "Symbolic Link =") ||
                    g_str_has_prefix(line, "Hard Link ="))) {
            value = strchr(line, '=');
            if (value != NULL && g_strstrip((gchar *)value + 1)[0] != '\0')
                entry.unsafe_type = TRUE;
        }
    }
    consume_entry(&entry, parsed, paths);
    g_free(entry.path);

    if (parsed->format == LUC_WINDOWS_IMAGE_FORMAT_UNKNOWN) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "The image is not a recognized ISO9660 or UDF filesystem");
        return FALSE;
    }
    if (parsed->has_install_wim && parsed->has_install_esd) {
        parsed->install_payload = LUC_WINDOWS_INSTALL_PAYLOAD_NONE;
        parsed->install_size = 0;
        g_clear_pointer(&parsed->install_path, g_free);
    }
    parsed->supports_bios = parsed->has_bootmgr && parsed->has_boot_bcd &&
                            parsed->has_bios_boot_image;
    parsed->requires_wim_split =
        parsed->install_payload == LUC_WINDOWS_INSTALL_PAYLOAD_WIM &&
        parsed->install_size > LUC_FAT32_MAX_FILE_SIZE;
    parsed->fat32_compatible = parsed->oversized_file_count == 0 ||
                               (parsed->requires_wim_split &&
                                parsed->oversized_file_count == 1);
    parsed->is_windows_installer =
        parsed->has_boot_wim &&
        parsed->install_payload != LUC_WINDOWS_INSTALL_PAYLOAD_NONE &&
        (parsed->supports_bios || parsed->supports_uefi_x64 ||
         parsed->supports_uefi_arm64) &&
        !parsed->has_unsafe_paths && !parsed->has_case_collisions;
    *info = g_steal_pointer(&parsed);
    return TRUE;
}

LucWindowsImageInfo *
luc_windows_image_inspect(const gchar *path,
                          GCancellable *cancellable,
                          GError **error)
{
    g_autofree gchar *tool = NULL;
    g_autoptr(GSubprocess) process = NULL;
    g_autofree gchar *stdout_text = NULL;
    g_autofree gchar *stderr_text = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;
    GStatBuf metadata;
    int image_fd;

    g_return_val_if_fail(path != NULL, NULL);
    image_fd = g_open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (image_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to inspect image: %s", g_strerror(errno));
        return NULL;
    }
    if (fstat(image_fd, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        close(image_fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Windows image must be a regular file, not a link");
        return NULL;
    }
    close(image_fd);
    tool = g_find_program_in_path("7z");
    if (tool == NULL)
        tool = g_find_program_in_path("7zz");
    if (tool == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "7-Zip is required to inspect ISO9660/UDF images");
        return NULL;
    }
    process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                               G_SUBPROCESS_FLAGS_STDERR_PIPE,
                               error, tool, "l", "-slt", "--", path, NULL);
    if (process == NULL ||
        !g_subprocess_communicate_utf8(process, NULL, cancellable,
                                       &stdout_text, &stderr_text, error))
        return NULL;
    if (!g_subprocess_get_successful(process)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "7-Zip could not inspect the image: %s",
                    stderr_text != NULL ? g_strstrip(stderr_text) : "unknown error");
        return NULL;
    }
    if (!luc_windows_image_parse_7z_listing(stdout_text, &info, error))
        return NULL;
    info->image_size = (guint64)metadata.st_size;
    return g_steal_pointer(&info);
}

static gboolean
parse_latest_percentage(const gchar *text, guint *percentage)
{
    gboolean found = FALSE;

    for (const gchar *cursor = text; (cursor = strchr(cursor, '%')) != NULL;
         cursor++) {
        const gchar *start = cursor;
        guint64 value;
        gchar *end = NULL;

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
verify_wim_file(const gchar *path,
                guint64 base,
                guint64 total,
                GCancellable *cancellable,
                LucWindowsImageProgressFunc progress_func,
                gpointer user_data,
                GError **error)
{
    g_autofree gchar *tool = NULL;
    g_autoptr(GSubprocess) process = NULL;
    g_autoptr(GString) diagnostic_text = g_string_new(NULL);
    g_autoptr(GString) parse_window = g_string_new(NULL);
    GInputStream *output_stream;
    struct stat metadata;
    const gchar *diagnostic;
    gsize length;
    int fd;

    fd = g_open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (fd < 0 || fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        if (fd >= 0)
            close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "WIM payload is not a regular file: %s", path);
        return FALSE;
    }
    close(fd);
    tool = g_find_program_in_path("wimlib-imagex");
    if (tool == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "wimlib-imagex is required to validate Windows payloads");
        return FALSE;
    }
    process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                               G_SUBPROCESS_FLAGS_STDERR_MERGE,
                               error, tool, "verify", path, NULL);
    if (process == NULL)
        return FALSE;
    if (progress_func != NULL)
        progress_func(base, total, user_data);
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
        if (progress_func != NULL) {
            guint percentage;
            if (parse_latest_percentage(parse_window->str, &percentage))
                progress_func(base +
                                  ((guint64)metadata.st_size * percentage) / 100,
                              total, user_data);
        }
    }
    if (!g_subprocess_wait(process, cancellable, error))
        return FALSE;
    if (g_subprocess_get_successful(process))
    {
        if (progress_func != NULL)
            progress_func(base + (guint64)metadata.st_size, total, user_data);
        return TRUE;
    }
    diagnostic = diagnostic_text->len > 0
                     ? g_strstrip(diagnostic_text->str) : "unknown error";
    length = strlen(diagnostic);
    if (length > 4096)
        diagnostic += length - 4096;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "wimlib could not verify '%s': %s", path, diagnostic);
    return FALSE;
}

gboolean
luc_windows_image_verify_payloads(const gchar *mount_path,
                                  const LucWindowsImageInfo *info,
                                  GCancellable *cancellable,
                                  GError **error)
{
    return luc_windows_image_verify_payloads_with_progress(
        mount_path, info, cancellable, NULL, NULL, error);
}

gboolean
luc_windows_image_verify_payloads_with_progress(
    const gchar *mount_path,
    const LucWindowsImageInfo *info,
    GCancellable *cancellable,
    LucWindowsImageProgressFunc progress_func,
    gpointer user_data,
    GError **error)
{
    g_autofree gchar *boot_path = NULL;
    g_autofree gchar *install_path = NULL;
    struct stat boot_metadata;
    struct stat install_metadata;
    guint64 total;

    g_return_val_if_fail(mount_path != NULL, FALSE);
    g_return_val_if_fail(info != NULL, FALSE);
    if (!info->is_windows_installer || info->boot_wim_path == NULL ||
        info->install_path == NULL ||
        !path_is_safe(info->boot_wim_path) || !path_is_safe(info->install_path)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Windows image metadata is incomplete or unsafe");
        return FALSE;
    }
    boot_path = g_build_filename(mount_path, info->boot_wim_path, NULL);
    install_path = g_build_filename(mount_path, info->install_path, NULL);
    if (g_stat(boot_path, &boot_metadata) != 0 ||
        g_stat(install_path, &install_metadata) != 0 ||
        boot_metadata.st_size < 0 || install_metadata.st_size < 0 ||
        G_MAXUINT64 - (guint64)boot_metadata.st_size <
            (guint64)install_metadata.st_size) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Unable to determine Windows payload sizes");
        return FALSE;
    }
    total = (guint64)boot_metadata.st_size + (guint64)install_metadata.st_size;
    return verify_wim_file(boot_path, 0, total, cancellable,
                           progress_func, user_data, error) &&
           verify_wim_file(install_path, (guint64)boot_metadata.st_size, total,
                           cancellable, progress_func, user_data, error);
}
