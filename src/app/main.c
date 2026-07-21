/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"

#include <adwaita.h>
#include <errno.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>

#include "app/window.h"
#include "core/helper-protocol.h"
#include "core/image-classifier.h"
#include "core/image-writer.h"
#include "core/linux-media.h"
#include "core/linux-target-plan.h"
#include "core/windows-image.h"
#include "core/windows-media.h"
#include "core/windows-target-plan.h"
#include "linux/device-monitor.h"
#include "linux/filesystem-mount.h"
#include "linux/iso-mount.h"

#define APPLICATION_ID "io.github.zsk1dr0w.LinuxUsbCreator"

static LucDeviceMonitor *monitor;
static GCancellable *cli_cancellable;

static void print_windows_progress(guint64 completed,
                                   guint64 total,
                                   gpointer user_data);

static void
on_cli_signal(int signal_number)
{
    (void)signal_number;
    if (cli_cancellable != NULL)
        g_cancellable_cancel(cli_cancellable);
}

static void
print_help(const gchar *program)
{
    g_print("%s\n", _("Usage:"));
    g_print("  %s [OPTION…]\n", program);
    g_print("  %s --diagnose\n", program);
    g_print("  %s --sha256 IMAGE\n", program);
    g_print("  %s --inspect-image IMAGE\n", program);
    g_print("  %s --inspect-windows IMAGE\n", program);
    g_print("  %s --validate-windows IMAGE\n", program);
    g_print("  %s --validate-linux IMAGE\n", program);
    g_print("  %s --write-windows IMAGE DEVICE SERIAL SIZE --firmware uefi|bios\n", program);
    g_print("  %s --write-linux IMAGE DEVICE SERIAL SIZE --persistence yes|no\n", program);
    g_print("  %s --write-image IMAGE DEVICE SERIAL SIZE [--no-verify]\n\n", program);
    g_print("%s\n", _("Commands:"));
    g_print("  %-45s %s\n", "--diagnose", _("Print detected devices and eligibility as JSON"));
    g_print("  %-45s %s\n", "--sha256 IMAGE", _("Compute SHA-256 for a regular image file"));
    g_print("  %-45s %s\n", "--inspect-image IMAGE", _("Identify a Linux, Windows, or raw/unknown image as JSON"));
    g_print("  %-45s %s\n", "--inspect-windows IMAGE", _("Inspect a Windows ISO9660/UDF installer as JSON"));
    g_print("  %-45s %s\n", "--validate-windows IMAGE", _("Mount read-only and validate Windows WIM/ESD payloads"));
    g_print("  %-45s %s\n", "--validate-linux IMAGE", _("Mount read-only and validate the Fedora Live ISO profile"));
    g_print("  %-45s %s\n", "--write-windows … --firmware uefi|bios", _("Create verified Windows installation media for UEFI/GPT or BIOS/MBR"));
    g_print("  %-45s %s\n", "--write-linux … --persistence yes|no", _("Create verified Fedora Live UEFI media with optional persistence"));
    g_print("  %-45s %s\n", "--write-image IMAGE DEVICE SERIAL SIZE", _("Write and verify an image on a confirmed USB device"));
    g_print("  %-45s %s\n\n", "--no-verify", _("Skip read-back verification (not recommended)"));
    g_print("%s\n", _("Options:"));
    g_print("  %-45s %s\n", "-h, --help", _("Show this help"));
    g_print("  %-45s %s\n", "-V, --version", _("Show program version"));
}

static int
run_linux_validation(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucImageClassification) classification =
        luc_image_classify(path, NULL, &error);
    g_autoptr(LucIsoMount) mount = NULL;
    guint64 content_size = 0;
    gboolean valid = FALSE;

    if (classification == NULL ||
        classification->linux_profile != LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI) {
        g_printerr("Unable to validate Linux image: %s\n",
                   error != NULL ? error->message
                                 : "unsupported Linux ISO profile");
        return 2;
    }
    mount = luc_iso_mount_open(path, NULL, &error);
    if (mount != NULL)
        valid = luc_linux_media_validate_fedora(
            luc_iso_mount_get_path(mount), &content_size, &error);
    if (mount != NULL &&
        !luc_iso_mount_close(mount, NULL, valid ? &error : NULL))
        valid = FALSE;
    if (!valid) {
        g_printerr("Unable to validate Linux image: %s\n",
                   error != NULL ? error->message : "unknown error");
        return 2;
    }
    g_print("{\"validated\":true,\"linux_profile\":\"fedora-live-uefi\","
            "\"content_size\":%" G_GUINT64_FORMAT "}\n", content_size);
    return 0;
}

static gboolean
prepare_linux_target(const gchar *device,
                     const gchar *serial,
                     guint64 size,
                     gchar **boot_partition,
                     gchar **data_partition,
                     GError **error)
{
    g_autoptr(GSubprocess) process = NULL;
    g_autofree gchar *size_text = g_strdup_printf("%" G_GUINT64_FORMAT, size);
    g_autoptr(GDataInputStream) stream = NULL;
    g_autoptr(GString) diagnostics = g_string_new(NULL);
    GInputStream *pipe;

    process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                               G_SUBPROCESS_FLAGS_STDERR_MERGE,
                               error, "pkexec", LUC_HELPER_PATH,
                               "prepare-linux", device, serial, size_text, NULL);
    if (process == NULL)
        return FALSE;
    pipe = g_subprocess_get_stdout_pipe(process);
    stream = g_data_input_stream_new(pipe);
    for (;;) {
        g_autofree gchar *line =
            g_data_input_stream_read_line(stream, NULL, cli_cancellable, error);
        LucHelperEvent event;

        if (line == NULL)
            break;
        if (luc_helper_event_parse(line, &event)) {
            g_print("%s\n", line);
            fflush(stdout);
        } else if (*boot_partition == NULL && *data_partition == NULL &&
                   luc_helper_parse_linux_prepared(line, boot_partition,
                                                   data_partition)) {
            continue;
        } else {
            if (diagnostics->len > 0)
                g_string_append_c(diagnostics, '\n');
            g_string_append(diagnostics, line);
        }
    }
    if (error != NULL && *error != NULL) {
        g_subprocess_send_signal(process, SIGINT);
        g_subprocess_wait(process, NULL, NULL);
        return FALSE;
    }
    if (!g_subprocess_wait(process, cli_cancellable, error))
        return FALSE;
    if (!g_subprocess_get_successful(process) || *boot_partition == NULL ||
        *data_partition == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Privileged Linux target preparation failed: %s",
                    diagnostics->len > 0
                        ? g_strstrip(diagnostics->str) : "unknown error");
        return FALSE;
    }
    return TRUE;
}

static void
print_linux_phase(LucLinuxMediaPhase phase, gpointer user_data)
{
    const gchar *name;

    (void)user_data;
    switch (phase) {
    case LUC_LINUX_MEDIA_PHASE_COPYING:
        name = "copying";
        break;
    case LUC_LINUX_MEDIA_PHASE_CONFIGURING:
        name = "configuring";
        break;
    case LUC_LINUX_MEDIA_PHASE_SYNCING:
        name = "syncing";
        break;
    case LUC_LINUX_MEDIA_PHASE_VERIFYING:
        name = "verifying";
        break;
    default:
        return;
    }
    g_print("{\"event\":\"phase\",\"name\":\"%s\"}\n", name);
    fflush(stdout);
}

static int
run_linux_write(const gchar *image,
                const gchar *device,
                const gchar *serial,
                const gchar *size_text,
                const gchar *persistence_text)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucImageClassification) classification = NULL;
    g_autoptr(LucIsoMount) source = NULL;
    g_autoptr(LucFilesystemMount) boot = NULL;
    g_autoptr(LucFilesystemMount) data = NULL;
    g_autofree gchar *boot_partition = NULL;
    g_autofree gchar *data_partition = NULL;
    gchar *end = NULL;
    guint64 size;
    guint64 content_size = 0;
    gboolean persistence;
    gboolean success = FALSE;

    if (!g_str_equal(persistence_text, "yes") &&
        !g_str_equal(persistence_text, "no")) {
        g_printerr("Linux persistence must be yes or no.\n");
        return 2;
    }
    persistence = g_str_equal(persistence_text, "yes");
    errno = 0;
    size = g_ascii_strtoull(size_text, &end, 10);
    if (errno != 0 || end == size_text || *end != '\0' || size == 0) {
        g_printerr("Invalid expected device size.\n");
        return 2;
    }
    cli_cancellable = g_cancellable_new();
    signal(SIGINT, on_cli_signal);
    signal(SIGTERM, on_cli_signal);
    g_print("{\"event\":\"phase\",\"name\":\"inspecting\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":1}\n");
    fflush(stdout);
    classification = luc_image_classify(image, cli_cancellable, &error);
    if (classification == NULL ||
        classification->linux_profile != LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI) {
        if (error == NULL)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                "Image is not a supported Fedora Live x64 ISO");
        goto out;
    }
    source = luc_iso_mount_open(image, cli_cancellable, &error);
    if (source == NULL ||
        !luc_linux_media_validate_fedora(luc_iso_mount_get_path(source),
                                         &content_size, &error))
        goto out;
    if (content_size > G_MAXUINT64 - LUC_LINUX_BOOT_SIZE_BYTES -
                           LUC_LINUX_FILESYSTEM_OVERHEAD ||
        content_size + LUC_LINUX_BOOT_SIZE_BYTES +
                LUC_LINUX_FILESYSTEM_OVERHEAD > size) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "USB device is too small for extracted Fedora media");
        goto out;
    }
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":1}\n");
    fflush(stdout);
    if (!prepare_linux_target(device, serial, size, &boot_partition,
                              &data_partition, &error))
        goto out;
    g_print("{\"event\":\"phase\",\"name\":\"mounting\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":2}\n");
    fflush(stdout);
    boot = luc_filesystem_mount_open_type(boot_partition, "vfat",
                                          cli_cancellable, &error);
    if (boot == NULL)
        goto out;
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":2}\n");
    fflush(stdout);
    data = luc_filesystem_mount_open_type(data_partition, "ext4",
                                          cli_cancellable, &error);
    if (data == NULL)
        goto out;
    g_print("{\"event\":\"progress\",\"completed\":2,\"total\":2}\n");
    fflush(stdout);
    if (!luc_linux_media_copy_fedora(
            luc_iso_mount_get_path(source),
            luc_filesystem_mount_get_path(boot),
            luc_filesystem_mount_get_path(data), persistence,
            cli_cancellable, print_linux_phase, print_windows_progress,
            NULL, &error))
        goto out;
    if (!luc_filesystem_mount_close(data, cli_cancellable, &error))
        goto out;
    if (!luc_filesystem_mount_close(boot, cli_cancellable, &error))
        goto out;
    if (!luc_iso_mount_close(source, cli_cancellable, &error))
        goto out;
    success = TRUE;
    g_print("{\"event\":\"completed\",\"mode\":\"fedora-live-uefi\","
            "\"persistence\":%s}\n", persistence ? "true" : "false");

out:
    if (!success)
        g_printerr("Unable to create Fedora Live media: %s\n",
                   error != NULL ? error->message : "unknown error");
    g_clear_object(&cli_cancellable);
    return success ? 0 : 2;
}

static gboolean
prepare_windows_target(const gchar *device,
                       const gchar *serial,
                       guint64 size,
                       LucWindowsFirmware firmware,
                       gchar **partition,
                       GError **error)
{
    g_autoptr(GSubprocess) process = NULL;
    g_autofree gchar *size_text = g_strdup_printf("%" G_GUINT64_FORMAT, size);
    g_autoptr(GDataInputStream) stream = NULL;
    g_autoptr(GString) diagnostics = g_string_new(NULL);
    GInputStream *pipe;

    process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                               G_SUBPROCESS_FLAGS_STDERR_MERGE,
                               error, "pkexec", LUC_HELPER_PATH,
                               "prepare-windows", device, serial, size_text,
                               luc_windows_firmware_to_string(firmware), NULL);
    if (process == NULL)
        return FALSE;
    pipe = g_subprocess_get_stdout_pipe(process);
    stream = g_data_input_stream_new(pipe);
    for (;;) {
        g_autofree gchar *line =
            g_data_input_stream_read_line(stream, NULL, cli_cancellable, error);
        LucHelperEvent event;

        if (line == NULL)
            break;
        if (luc_helper_event_parse(line, &event)) {
            g_print("%s\n", line);
            fflush(stdout);
        } else if (*partition == NULL) {
            if (!luc_helper_parse_prepared(line, partition)) {
                if (diagnostics->len > 0)
                    g_string_append_c(diagnostics, '\n');
                g_string_append(diagnostics, line);
            }
        } else {
            if (diagnostics->len > 0)
                g_string_append_c(diagnostics, '\n');
            g_string_append(diagnostics, line);
        }
    }
    if (error != NULL && *error != NULL) {
        g_subprocess_send_signal(process, SIGINT);
        g_subprocess_wait(process, NULL, NULL);
        return FALSE;
    }
    if (!g_subprocess_wait(process, cli_cancellable, error))
        return FALSE;
    if (!g_subprocess_get_successful(process) || *partition == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Privileged Windows target preparation failed: %s",
                    diagnostics->len > 0
                        ? g_strstrip(diagnostics->str) : "unknown error");
        return FALSE;
    }
    return TRUE;
}

static void
print_windows_phase(LucWindowsMediaPhase phase, gpointer user_data)
{
    const gchar *name;

    (void)user_data;
    switch (phase) {
    case LUC_WINDOWS_MEDIA_PHASE_COPYING:
        name = "copying";
        break;
    case LUC_WINDOWS_MEDIA_PHASE_SPLITTING:
        name = "splitting";
        break;
    case LUC_WINDOWS_MEDIA_PHASE_SYNCING:
        name = "syncing";
        break;
    case LUC_WINDOWS_MEDIA_PHASE_VERIFYING:
        name = "verifying";
        break;
    default:
        return;
    }
    g_print("{\"event\":\"phase\",\"name\":\"%s\"}\n", name);
    fflush(stdout);
}

static void
print_windows_progress(guint64 completed, guint64 total, gpointer user_data)
{
    (void)user_data;
    g_print("{\"event\":\"progress\",\"completed\":%" G_GUINT64_FORMAT
            ",\"total\":%" G_GUINT64_FORMAT "}\n",
            completed, total);
    fflush(stdout);
}

static int
run_windows_write(const gchar *image,
                  const gchar *device,
                  const gchar *serial,
                  const gchar *size_text,
                  const gchar *firmware_text)
{
    const guint64 filesystem_overhead = 256U * 1024U * 1024U;
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;
    g_autoptr(LucIsoMount) source = NULL;
    g_autoptr(LucFilesystemMount) destination = NULL;
    g_autofree gchar *partition = NULL;
    gchar *end = NULL;
    guint64 size;
    LucWindowsFirmware firmware;
    gboolean success = FALSE;

    if (!luc_windows_firmware_from_string(firmware_text, &firmware)) {
        g_printerr("Windows firmware profile must be uefi or bios.\n");
        return 2;
    }
    errno = 0;
    size = g_ascii_strtoull(size_text, &end, 10);
    if (errno != 0 || end == size_text || *end != '\0' || size == 0) {
        g_printerr("Invalid expected device size.\n");
        return 2;
    }
    cli_cancellable = g_cancellable_new();
    signal(SIGINT, on_cli_signal);
    signal(SIGTERM, on_cli_signal);
    g_print("{\"event\":\"phase\",\"name\":\"inspecting\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":1}\n");
    fflush(stdout);
    info = luc_windows_image_inspect(image, cli_cancellable, &error);
    if (info == NULL || !info->is_windows_installer ||
        !info->fat32_compatible ||
        (firmware == LUC_WINDOWS_FIRMWARE_UEFI &&
         !info->supports_uefi_x64 && !info->supports_uefi_arm64) ||
        (firmware == LUC_WINDOWS_FIRMWARE_BIOS && !info->supports_bios)) {
        if (error == NULL)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                "Image does not support the selected Windows firmware profile");
        goto out;
    }
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":1}\n");
    if (info->content_size > G_MAXUINT64 - filesystem_overhead ||
        size < info->content_size + filesystem_overhead) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "USB device is too small for the extracted Windows installer");
        goto out;
    }
    source = luc_iso_mount_open(image, cli_cancellable, &error);
    if (source == NULL)
        goto out;
    g_print("{\"event\":\"phase\",\"name\":\"validating\"}\n");
    fflush(stdout);
    if (!luc_windows_image_verify_payloads_with_progress(
            luc_iso_mount_get_path(source), info, cli_cancellable,
            print_windows_progress, NULL, &error))
        goto out;
    if (!prepare_windows_target(device, serial, size, firmware,
                                &partition, &error))
        goto out;
    g_print("{\"event\":\"phase\",\"name\":\"mounting\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":1}\n");
    fflush(stdout);
    destination = luc_filesystem_mount_open(partition, cli_cancellable, &error);
    if (destination == NULL)
        goto out;
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":1}\n");
    if (!luc_windows_media_copy(luc_iso_mount_get_path(source),
                                luc_filesystem_mount_get_path(destination),
                                info, cli_cancellable, print_windows_phase,
                                print_windows_progress, NULL, &error))
        goto out;
    if (!luc_filesystem_mount_close(destination, cli_cancellable, &error))
        goto out;
    if (!luc_iso_mount_close(source, cli_cancellable, &error))
        goto out;
    success = TRUE;
    g_print("{\"event\":\"completed\",\"mode\":\"windows-%s-fat32\"}\n",
            luc_windows_firmware_to_string(firmware));

out:
    if (!success)
        g_printerr("Unable to create Windows installation media: %s\n",
                   error != NULL ? error->message : "unknown error");
    g_clear_object(&cli_cancellable);
    return success ? 0 : 2;
}

static int
run_windows_inspection(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info =
        luc_windows_image_inspect(path, NULL, &error);

    if (info == NULL) {
        g_printerr("Unable to inspect Windows image: %s\n", error->message);
        return 2;
    }
    g_print("{\"format\":\"%s\",\"windows_installer\":%s,"
            "\"uefi_x64\":%s,\"uefi_arm64\":%s,\"bios\":%s,"
            "\"install_payload\":\"%s\",\"install_size\":%" G_GUINT64_FORMAT ","
            "\"content_size\":%" G_GUINT64_FORMAT ",\"file_count\":%u,"
            "\"requires_wim_split\":%s,\"fat32_compatible\":%s,"
            "\"unsafe_paths\":%s,\"case_collisions\":%s}\n",
            luc_windows_image_format_to_string(info->format),
            info->is_windows_installer ? "true" : "false",
            info->supports_uefi_x64 ? "true" : "false",
            info->supports_uefi_arm64 ? "true" : "false",
            info->supports_bios ? "true" : "false",
            luc_windows_install_payload_to_string(info->install_payload),
            info->install_size, info->content_size, info->file_count,
            info->requires_wim_split ? "true" : "false",
            info->fat32_compatible ? "true" : "false",
            info->has_unsafe_paths ? "true" : "false",
            info->has_case_collisions ? "true" : "false");
    return info->is_windows_installer ? 0 : 3;
}

static int
run_windows_validation(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info =
        luc_windows_image_inspect(path, NULL, &error);
    g_autoptr(LucIsoMount) mount = NULL;
    gboolean verified;

    if (info == NULL || !info->is_windows_installer) {
        g_printerr("Unable to validate Windows image: %s\n",
                   error != NULL ? error->message : "installer structure is incomplete");
        return 2;
    }
    mount = luc_iso_mount_open(path, NULL, &error);
    if (mount == NULL) {
        g_printerr("Unable to mount Windows image read-only: %s\n", error->message);
        return 2;
    }
    verified = luc_windows_image_verify_payloads(
        luc_iso_mount_get_path(mount), info, NULL, &error);
    if (!luc_iso_mount_close(mount, NULL, verified ? &error : NULL))
        verified = FALSE;
    if (!verified) {
        g_printerr("Unable to validate Windows payloads: %s\n",
                   error != NULL ? error->message : "unknown error");
        return 2;
    }
    g_print("{\"validated\":true,\"format\":\"%s\","
            "\"install_payload\":\"%s\",\"boot_wim\":true}\n",
            luc_windows_image_format_to_string(info->format),
            luc_windows_install_payload_to_string(info->install_payload));
    return 0;
}

static gchar *
json_escape(const gchar *value)
{
    GString *escaped = g_string_new(NULL);
    const guchar *cursor = (const guchar *)(value != NULL ? value : "");

    for (; *cursor != '\0'; cursor++) {
        switch (*cursor) {
        case '\\': g_string_append(escaped, "\\\\"); break;
        case '"': g_string_append(escaped, "\\\""); break;
        case '\b': g_string_append(escaped, "\\b"); break;
        case '\f': g_string_append(escaped, "\\f"); break;
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
    return g_string_free(escaped, FALSE);
}

static int
run_sha256(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *digest = luc_image_sha256(path, NULL, &error);

    if (digest == NULL) {
        g_printerr("Unable to compute SHA-256: %s\n", error->message);
        return 2;
    }
    g_print("%s  %s\n", digest, path);
    return 0;
}

static int
run_image_classification(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucImageClassification) classification =
        luc_image_classify(path, NULL, &error);
    g_autofree gchar *distribution = NULL;
    g_autofree gchar *architecture = NULL;

    if (classification == NULL) {
        g_printerr("Unable to identify image: %s\n", error->message);
        return 2;
    }
    distribution = json_escape(classification->distribution);
    architecture = json_escape(classification->architecture);
    g_print("{\"kind\":\"%s\",\"distribution\":\"%s\","
            "\"architecture\":\"%s\",\"linux_profile\":\"%s\"}\n",
            luc_image_kind_to_string(classification->kind), distribution,
            architecture,
            luc_linux_profile_to_string(classification->linux_profile));
    return 0;
}

static int
run_privileged_write(const gchar *image,
                     const gchar *device,
                     const gchar *serial,
                     const gchar *size,
                     gboolean verify)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) subprocess = NULL;

    subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error,
                                  "pkexec", LUC_HELPER_PATH, "write", image,
                                  device, serial, size, "--verify",
                                  verify ? "yes" : "no", NULL);
    if (subprocess == NULL) {
        g_printerr("Unable to start privileged helper: %s\n", error->message);
        return 2;
    }
    if (!g_subprocess_wait_check(subprocess, NULL, &error)) {
        g_printerr("Privileged write failed: %s\n", error->message);
        return 2;
    }
    return 0;
}

static int
run_diagnostics(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucDeviceMonitor) local_monitor = luc_device_monitor_new(&error);
    g_autoptr(GPtrArray) devices = NULL;

    if (local_monitor == NULL) {
        g_printerr("Unable to connect to UDisks2: %s\n", error->message);
        return 2;
    }

    devices = luc_device_monitor_dup_devices(local_monitor);
    puts("[");
    for (guint i = 0; i < devices->len; i++) {
        LucDevice *device = g_ptr_array_index(devices, i);
        g_autofree gchar *path = json_escape(device->device);
        g_autofree gchar *model = json_escape(device->model);
        g_autofree gchar *serial = json_escape(device->serial);
        g_autofree gchar *bus = json_escape(device->connection_bus);

        printf("  {%s\"device\":\"%s\",\"model\":\"%s\","
               "\"serial\":\"%s\",\"bus\":\"%s\",\"size\":%" G_GUINT64_FORMAT ","
               "\"removable\":%s,\"eligibility\":\"%s\"}",
               i == 0 ? "" : "",
               path,
               model,
               serial,
               bus,
               device->size,
               device->removable || device->media_removable ? "true" : "false",
               luc_device_eligibility_to_string(luc_device_get_eligibility(device)));
        puts(i + 1 == devices->len ? "" : ",");
    }
    puts("]");
    return 0;
}

static void
on_activate(GtkApplication *application)
{
    GtkWindow *window = gtk_application_get_active_window(application);
    if (window == NULL)
        window = GTK_WINDOW(luc_window_new(ADW_APPLICATION(application), monitor));
    gtk_window_present(window);
}

int
main(int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(AdwApplication) application = NULL;
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    if (argc == 2 && (g_str_equal(argv[1], "--help") ||
                      g_str_equal(argv[1], "-h") ||
                      g_str_equal(argv[1], "--help-all"))) {
        print_help(argv[0]);
        return 0;
    }
    if (argc == 2 && g_str_equal(argv[1], "--diagnose"))
        return run_diagnostics();
    if (argc == 3 && g_str_equal(argv[1], "--sha256"))
        return run_sha256(argv[2]);
    if (argc == 3 && g_str_equal(argv[1], "--inspect-image"))
        return run_image_classification(argv[2]);
    if (argc == 3 && g_str_equal(argv[1], "--inspect-windows"))
        return run_windows_inspection(argv[2]);
    if (argc == 3 && g_str_equal(argv[1], "--validate-windows"))
        return run_windows_validation(argv[2]);
    if (argc == 3 && g_str_equal(argv[1], "--validate-linux"))
        return run_linux_validation(argv[2]);
    if (argc == 8 && g_str_equal(argv[1], "--write-windows") &&
        g_str_equal(argv[6], "--firmware"))
        return run_windows_write(argv[2], argv[3], argv[4], argv[5], argv[7]);
    if (argc == 8 && g_str_equal(argv[1], "--write-linux") &&
        g_str_equal(argv[6], "--persistence"))
        return run_linux_write(argv[2], argv[3], argv[4], argv[5], argv[7]);
    if ((argc == 6 || argc == 7) && g_str_equal(argv[1], "--write-image")) {
        gboolean verify = argc == 6 || !g_str_equal(argv[6], "--no-verify");
        if (argc == 7 && verify) {
            g_printerr("Unknown write option: %s\n", argv[6]);
            return 2;
        }
        return run_privileged_write(argv[2], argv[3], argv[4], argv[5], verify);
    }
    if (argc == 2 && (g_str_equal(argv[1], "--version") || g_str_equal(argv[1], "-V"))) {
        g_print("linuxusbcreator %s\n", PROJECT_VERSION);
        return 0;
    }

    monitor = luc_device_monitor_new(&error);
    if (monitor == NULL) {
        g_printerr("Unable to connect to UDisks2: %s\n", error->message);
        return 2;
    }

    application = adw_application_new(APPLICATION_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), NULL);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_clear_object(&monitor);
    return status;
}
