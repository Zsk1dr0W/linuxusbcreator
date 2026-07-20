/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/device.h"
#include "core/image-writer.h"
#include "core/operation-log.h"
#include "linux/device-monitor.h"

#define OPERATION_LOG "/var/log/linuxusbcreator.jsonl"

static GCancellable *operation_cancellable;

static void
on_signal(int signal_number)
{
    (void)signal_number;
    if (operation_cancellable != NULL)
        g_cancellable_cancel(operation_cancellable);
}

static void
print_progress(goffset completed, goffset total, gpointer user_data)
{
    (void)user_data;
    g_print("{\"event\":\"progress\",\"completed\":%" G_GOFFSET_FORMAT
            ",\"total\":%" G_GOFFSET_FORMAT "}\n",
            completed, total);
    fflush(stdout);
}

static void
print_phase(LucImagePhase phase, gpointer user_data)
{
    const gchar *name;

    (void)user_data;
    switch (phase) {
    case LUC_IMAGE_PHASE_WRITING:
        name = "writing";
        break;
    case LUC_IMAGE_PHASE_SYNCING:
        name = "syncing";
        break;
    case LUC_IMAGE_PHASE_VERIFYING:
        name = "verifying";
        break;
    default:
        return;
    }
    g_print("{\"event\":\"phase\",\"name\":\"%s\"}\n", name);
    fflush(stdout);
}

static gboolean
is_whole_block_device(const gchar *device_path, GError **error)
{
    g_autofree gchar *basename = g_path_get_basename(device_path);
    g_autofree gchar *partition_path = NULL;

    if (!g_str_has_prefix(device_path, "/dev/") || strchr(basename, '/') != NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Target must be a direct /dev block path");
        return FALSE;
    }
    partition_path = g_build_filename("/sys/class/block", basename, "partition", NULL);
    if (g_file_test(partition_path, G_FILE_TEST_EXISTS)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Target must be a whole disk, not a partition");
        return FALSE;
    }
    return TRUE;
}

static int
run_write(const gchar *image_path,
          const gchar *device_path,
          const gchar *expected_serial,
          guint64 expected_size,
          gboolean verify)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucDeviceMonitor) monitor = NULL;
    g_autoptr(LucDevice) device = NULL;
    g_autofree gchar *digest = NULL;
    GStatBuf image_stat;
    gboolean success;

    if (!is_whole_block_device(device_path, &error))
        goto failed;
    if (g_stat(image_path, &image_stat) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to inspect image: %s", g_strerror(errno));
        goto failed;
    }
    if (!S_ISREG(image_stat.st_mode)) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Image is not a regular file");
        goto failed;
    }
    monitor = luc_device_monitor_new(&error);
    if (monitor == NULL)
        goto failed;
    device = luc_device_monitor_find_device(monitor, device_path);
    if (!luc_device_validate_confirmation(device, expected_serial, expected_size,
                                          FALSE, &error))
        goto failed;
    if (!luc_device_monitor_unmount_drive(monitor, device->drive_path, &error))
        goto failed;

    g_clear_object(&monitor);
    g_clear_pointer(&device, luc_device_free);
    monitor = luc_device_monitor_new(&error);
    if (monitor == NULL)
        goto failed;
    device = luc_device_monitor_find_device(monitor, device_path);
    if (!luc_device_validate_confirmation(device, expected_serial, expected_size,
                                          TRUE, &error))
        goto failed;

    g_print("{\"event\":\"phase\",\"name\":\"hashing\"}\n");
    fflush(stdout);
    digest = luc_image_sha256(image_path, operation_cancellable, &error);
    if (digest == NULL)
        goto failed;
    luc_operation_log_append(OPERATION_LOG, "raw-write", "started", image_path,
                             device_path, image_stat.st_size, digest, NULL);
    success = luc_image_write_block_device(image_path, device_path, verify,
                                           operation_cancellable, print_phase,
                                           print_progress, NULL, &error);
    luc_operation_log_append(OPERATION_LOG, "raw-write",
                             success ? "completed" : "failed",
                             image_path, device_path, image_stat.st_size, digest, NULL);
    if (!success)
        goto failed;
    g_print("{\"event\":\"completed\",\"sha256\":\"%s\"}\n", digest);
    return EXIT_SUCCESS;

failed:
    g_printerr("linuxusbcreator-helper: %s\n",
               error != NULL ? error->message : "unknown error");
    return EXIT_FAILURE;
}

int
main(int argc, char **argv)
{
    gchar *end = NULL;
    guint64 expected_size;
    gboolean verify;

    if (geteuid() != 0) {
        g_printerr("This helper must be started through Polkit/pkexec.\n");
        return EXIT_FAILURE;
    }
    if (argc != 8 || !g_str_equal(argv[1], "write") ||
        !g_str_equal(argv[6], "--verify")) {
        g_printerr("Usage: linuxusbcreator-helper write IMAGE DEVICE SERIAL SIZE --verify yes|no\n");
        return EXIT_FAILURE;
    }
    errno = 0;
    expected_size = g_ascii_strtoull(argv[5], &end, 10);
    if (errno != 0 || end == argv[5] || *end != '\0' || expected_size == 0) {
        g_printerr("Invalid expected device size.\n");
        return EXIT_FAILURE;
    }
    if (!g_str_equal(argv[7], "yes") && !g_str_equal(argv[7], "no")) {
        g_printerr("Verification must be yes or no.\n");
        return EXIT_FAILURE;
    }
    verify = g_str_equal(argv[7], "yes");
    operation_cancellable = g_cancellable_new();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    int result = run_write(argv[2], argv[3], argv[4], expected_size, verify);
    g_clear_object(&operation_cancellable);
    return result;
}
