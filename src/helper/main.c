/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "config.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <pwd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/device.h"
#include "core/image-writer.h"
#include "core/linux-target-plan.h"
#include "core/operation-log.h"
#include "core/windows-bios-boot.h"
#include "core/windows-target-plan.h"
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
    if (!S_ISREG(image_stat.st_mode) || image_stat.st_size <= 0) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Image must be a non-empty regular file");
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
    digest = luc_image_sha256_with_progress(image_path, operation_cancellable,
                                            print_progress, NULL, &error);
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

static gchar *
find_trusted_tool(const gchar *const *candidates, GError **error)
{
    for (guint i = 0; candidates[i] != NULL; i++) {
        struct stat metadata;

        if (g_stat(candidates[i], &metadata) == 0 &&
            S_ISREG(metadata.st_mode) && metadata.st_uid == 0 &&
            (metadata.st_mode & (S_IWGRP | S_IWOTH)) == 0)
            return g_strdup(candidates[i]);
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "A required trusted system utility is not installed");
    return NULL;
}

static gboolean
run_fixed_tool(const gchar *const *argv,
               const gchar *stdin_text,
               GError **error)
{
    g_autoptr(GSubprocess) process = NULL;
    g_autofree gchar *stderr_text = NULL;
    const gchar *diagnostic;
    gsize length;

    process = g_subprocess_newv(
        argv,
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
        G_SUBPROCESS_FLAGS_STDERR_PIPE,
        error);
    if (process == NULL)
        return FALSE;
    if (!g_subprocess_communicate_utf8(process, stdin_text,
                                       operation_cancellable,
                                       NULL, &stderr_text, error)) {
        g_subprocess_force_exit(process);
        g_subprocess_wait(process, NULL, NULL);
        return FALSE;
    }
    if (g_subprocess_get_successful(process))
        return TRUE;
    diagnostic = stderr_text != NULL ? g_strstrip(stderr_text) : "unknown error";
    length = strlen(diagnostic);
    if (length > 4096)
        diagnostic += length - 4096;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "System utility failed: %s", diagnostic);
    return FALSE;
}

static gboolean
wait_for_partition(const gchar *partition_path, GError **error)
{
    for (guint attempt = 0; attempt < 150; attempt++) {
        struct stat metadata;

        if (operation_cancellable != NULL &&
            g_cancellable_is_cancelled(operation_cancellable)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                "Media preparation was cancelled");
            return FALSE;
        }
        if (g_stat(partition_path, &metadata) == 0 && S_ISBLK(metadata.st_mode))
            return TRUE;
        g_usleep(100000);
    }
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                "Partition did not appear after creating the partition table: %s",
                partition_path);
    return FALSE;
}

static gboolean
get_calling_user(gchar **root_owner, GError **error)
{
    const gchar *text = g_getenv("PKEXEC_UID");
    gchar *end = NULL;
    guint64 value;
    struct passwd *account;

    if (text == NULL || text[0] == '\0') {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Polkit did not report the calling user");
        return FALSE;
    }
    errno = 0;
    value = g_ascii_strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > G_MAXUINT32) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Polkit reported an invalid calling user");
        return FALSE;
    }
    account = getpwuid((uid_t)value);
    if (account == NULL || account->pw_uid != (uid_t)value) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Unable to resolve the calling user");
        return FALSE;
    }
    *root_owner = g_strdup_printf("root_owner=%u:%u",
                                  (guint)account->pw_uid,
                                  (guint)account->pw_gid);
    return TRUE;
}

static int
run_prepare_linux(const gchar *device_path,
                  const gchar *expected_serial,
                  guint64 expected_size)
{
    static const gchar *const sfdisk_candidates[] = {
        "/usr/sbin/sfdisk", "/usr/bin/sfdisk", "/sbin/sfdisk", NULL,
    };
    static const gchar *const mkfs_fat_candidates[] = {
        "/usr/sbin/mkfs.fat", "/usr/bin/mkfs.fat", "/sbin/mkfs.fat", NULL,
    };
    static const gchar *const mkfs_ext4_candidates[] = {
        "/usr/sbin/mkfs.ext4", "/usr/bin/mkfs.ext4", "/sbin/mkfs.ext4", NULL,
    };
    g_autoptr(GError) error = NULL;
    g_autoptr(LucDeviceMonitor) monitor = NULL;
    g_autoptr(LucDevice) device = NULL;
    g_autofree gchar *sfdisk = NULL;
    g_autofree gchar *mkfs_fat = NULL;
    g_autofree gchar *mkfs_ext4 = NULL;
    g_autofree gchar *boot_partition = NULL;
    g_autofree gchar *data_partition = NULL;
    g_autofree gchar *root_owner = NULL;
    const gchar *sfdisk_argv[7];
    const gchar *fat_argv[7];
    const gchar *ext4_argv[8];

    if (expected_size <= LUC_LINUX_BOOT_SIZE_BYTES +
                             LUC_LINUX_FILESYSTEM_OVERHEAD) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "USB device is too small for Fedora ISO mode");
        goto failed;
    }
    if (!is_whole_block_device(device_path, &error) ||
        !get_calling_user(&root_owner, &error))
        goto failed;
    sfdisk = find_trusted_tool(sfdisk_candidates, &error);
    mkfs_fat = find_trusted_tool(mkfs_fat_candidates, &error);
    mkfs_ext4 = find_trusted_tool(mkfs_ext4_candidates, &error);
    if (sfdisk == NULL || mkfs_fat == NULL || mkfs_ext4 == NULL)
        goto failed;
    monitor = luc_device_monitor_new(&error);
    if (monitor == NULL)
        goto failed;
    device = luc_device_monitor_find_device(monitor, device_path);
    if (!luc_device_validate_confirmation(device, expected_serial, expected_size,
                                          FALSE, &error) ||
        !luc_device_monitor_unmount_drive(monitor, device->drive_path, &error))
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

    luc_operation_log_append(OPERATION_LOG, "linux-iso-prepare", "started", "",
                             device_path, expected_size, "", NULL);
    g_print("{\"event\":\"phase\",\"name\":\"partitioning\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":1}\n");
    fflush(stdout);
    sfdisk_argv[0] = sfdisk;
    sfdisk_argv[1] = "--wipe";
    sfdisk_argv[2] = "always";
    sfdisk_argv[3] = "--wipe-partitions";
    sfdisk_argv[4] = "always";
    sfdisk_argv[5] = device_path;
    sfdisk_argv[6] = NULL;
    if (!run_fixed_tool(sfdisk_argv, luc_linux_target_sfdisk_plan(), &error))
        goto failed_logged;
    boot_partition = luc_linux_target_partition_path(device_path, 1, &error);
    data_partition = luc_linux_target_partition_path(device_path, 2, &error);
    if (boot_partition == NULL || data_partition == NULL ||
        !wait_for_partition(boot_partition, &error) ||
        !wait_for_partition(data_partition, &error))
        goto failed_logged;
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":1}\n");
    g_print("{\"event\":\"phase\",\"name\":\"formatting\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":2}\n");
    fflush(stdout);
    fat_argv[0] = mkfs_fat;
    fat_argv[1] = "-F";
    fat_argv[2] = "32";
    fat_argv[3] = "-n";
    fat_argv[4] = LUC_LINUX_BOOT_LABEL;
    fat_argv[5] = boot_partition;
    fat_argv[6] = NULL;
    if (!run_fixed_tool(fat_argv, NULL, &error))
        goto failed_logged;
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":2}\n");
    fflush(stdout);
    ext4_argv[0] = mkfs_ext4;
    ext4_argv[1] = "-F";
    ext4_argv[2] = "-L";
    ext4_argv[3] = LUC_LINUX_DATA_LABEL;
    ext4_argv[4] = "-E";
    ext4_argv[5] = root_owner;
    ext4_argv[6] = data_partition;
    ext4_argv[7] = NULL;
    if (!run_fixed_tool(ext4_argv, NULL, &error))
        goto failed_logged;
    g_print("{\"event\":\"progress\",\"completed\":2,\"total\":2}\n");
    fflush(stdout);
    sync();
    luc_operation_log_append(OPERATION_LOG, "linux-iso-prepare", "completed", "",
                             device_path, expected_size, "", NULL);
    g_print("{\"event\":\"prepared-linux\",\"boot_partition\":\"%s\","
            "\"data_partition\":\"%s\"}\n",
            boot_partition, data_partition);
    return EXIT_SUCCESS;

failed_logged:
    luc_operation_log_append(OPERATION_LOG, "linux-iso-prepare", "failed", "",
                             device_path, expected_size, "", NULL);
failed:
    g_printerr("linuxusbcreator-helper: %s\n",
               error != NULL ? error->message : "unknown error");
    return EXIT_FAILURE;
}

static int
run_prepare_windows(const gchar *device_path,
                    const gchar *expected_serial,
                    guint64 expected_size,
                    LucWindowsFirmware firmware)
{
    static const gchar *const sfdisk_candidates[] = {
        "/usr/sbin/sfdisk", "/usr/bin/sfdisk", "/sbin/sfdisk", NULL,
    };
    static const gchar *const mkfs_candidates[] = {
        "/usr/sbin/mkfs.fat", "/usr/bin/mkfs.fat", "/sbin/mkfs.fat", NULL,
    };
    g_autoptr(GError) error = NULL;
    g_autoptr(LucDeviceMonitor) monitor = NULL;
    g_autoptr(LucDevice) device = NULL;
    g_autofree gchar *sfdisk = NULL;
    g_autofree gchar *mkfs = NULL;
    g_autofree gchar *partition_path = NULL;
    const gchar *sfdisk_argv[7];
    const gchar *mkfs_argv[7];

    if (!is_whole_block_device(device_path, &error))
        goto failed;
    sfdisk = find_trusted_tool(sfdisk_candidates, &error);
    if (sfdisk == NULL)
        goto failed;
    mkfs = find_trusted_tool(mkfs_candidates, &error);
    if (mkfs == NULL)
        goto failed;
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

    luc_operation_log_append(OPERATION_LOG, "windows-prepare", "started", "",
                             device_path, expected_size, "", NULL);
    g_print("{\"event\":\"phase\",\"name\":\"partitioning\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":1}\n");
    fflush(stdout);
    sfdisk_argv[0] = sfdisk;
    sfdisk_argv[1] = "--wipe";
    sfdisk_argv[2] = "always";
    sfdisk_argv[3] = "--wipe-partitions";
    sfdisk_argv[4] = "always";
    sfdisk_argv[5] = device_path;
    sfdisk_argv[6] = NULL;
    if (!run_fixed_tool(sfdisk_argv, luc_windows_target_sfdisk_plan(firmware), &error))
        goto failed_logged;
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":1}\n");
    partition_path = luc_windows_target_partition_path(device_path, &error);
    if (partition_path == NULL)
        goto failed_logged;
    if (!wait_for_partition(partition_path, &error))
        goto failed_logged;

    g_print("{\"event\":\"phase\",\"name\":\"formatting\"}\n");
    g_print("{\"event\":\"progress\",\"completed\":0,\"total\":1}\n");
    fflush(stdout);
    mkfs_argv[0] = mkfs;
    mkfs_argv[1] = "-F";
    mkfs_argv[2] = "32";
    mkfs_argv[3] = "-n";
    mkfs_argv[4] = "LUC-WINDOWS";
    mkfs_argv[5] = partition_path;
    mkfs_argv[6] = NULL;
    if (!run_fixed_tool(mkfs_argv, NULL, &error))
        goto failed_logged;
    if (firmware == LUC_WINDOWS_FIRMWARE_BIOS &&
        !luc_windows_bios_install_boot_records(device_path, partition_path,
                                               &error))
        goto failed_logged;
    g_print("{\"event\":\"progress\",\"completed\":1,\"total\":1}\n");
    fflush(stdout);
    sync();
    luc_operation_log_append(OPERATION_LOG, "windows-prepare", "completed", "",
                             device_path, expected_size, "", NULL);
    g_print("{\"event\":\"prepared\",\"partition\":\"%s\"}\n",
            partition_path);
    return EXIT_SUCCESS;

failed_logged:
    luc_operation_log_append(OPERATION_LOG, "windows-prepare", "failed", "",
                             device_path, expected_size, "", NULL);
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
    if (argc == 6 && g_str_equal(argv[1], "prepare-windows")) {
        LucWindowsFirmware firmware;

        errno = 0;
        expected_size = g_ascii_strtoull(argv[4], &end, 10);
        if (errno != 0 || end == argv[4] || *end != '\0' || expected_size == 0) {
            g_printerr("Invalid expected device size.\n");
            return EXIT_FAILURE;
        }
        if (!luc_windows_firmware_from_string(argv[5], &firmware)) {
            g_printerr("Windows firmware profile must be uefi or bios.\n");
            return EXIT_FAILURE;
        }
        operation_cancellable = g_cancellable_new();
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        int result = run_prepare_windows(argv[2], argv[3], expected_size,
                                         firmware);
        g_clear_object(&operation_cancellable);
        return result;
    }
    if (argc == 5 && g_str_equal(argv[1], "prepare-linux")) {
        errno = 0;
        expected_size = g_ascii_strtoull(argv[4], &end, 10);
        if (errno != 0 || end == argv[4] || *end != '\0' || expected_size == 0) {
            g_printerr("Invalid expected device size.\n");
            return EXIT_FAILURE;
        }
        operation_cancellable = g_cancellable_new();
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        int result = run_prepare_linux(argv[2], argv[3], expected_size);
        g_clear_object(&operation_cancellable);
        return result;
    }
    if (argc != 8 || !g_str_equal(argv[1], "write") ||
        !g_str_equal(argv[6], "--verify")) {
        g_printerr("Usage:\n"
                   "  linuxusbcreator-helper write IMAGE DEVICE SERIAL SIZE --verify yes|no\n"
                   "  linuxusbcreator-helper prepare-windows DEVICE SERIAL SIZE uefi|bios\n"
                   "  linuxusbcreator-helper prepare-linux DEVICE SERIAL SIZE\n");
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
