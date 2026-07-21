/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "core/image-classifier.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean
contains_any(const gchar *text, const gchar *const *needles)
{
    for (guint i = 0; needles[i] != NULL; i++) {
        if (strstr(text, needles[i]) != NULL)
            return TRUE;
    }
    return FALSE;
}

void
luc_image_classification_free(LucImageClassification *classification)
{
    if (classification == NULL)
        return;
    g_free(classification->distribution);
    g_free(classification->architecture);
    g_free(classification);
}

const gchar *
luc_image_kind_to_string(LucImageKind kind)
{
    switch (kind) {
    case LUC_IMAGE_KIND_LINUX_ISO:
        return "linux";
    case LUC_IMAGE_KIND_WINDOWS_ISO:
        return "windows";
    case LUC_IMAGE_KIND_RAW_OR_UNKNOWN:
    default:
        return "raw-or-unknown";
    }
}

const gchar *
luc_linux_profile_to_string(LucLinuxProfile profile)
{
    switch (profile) {
    case LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI:
        return "fedora-live-uefi";
    case LUC_LINUX_PROFILE_NONE:
    default:
        return "none";
    }
}

LucImageClassification *
luc_image_classify_listing(const gchar *listing)
{
    static const gchar *const windows_paths[] = {
        "path = sources/boot.wim", "path = sources/install.wim",
        "path = sources/install.esd", NULL,
    };
    static const gchar *const linux_paths[] = {
        "path = .disk/info", "path = isolinux/isolinux.bin",
        "path = live/filesystem.squashfs", "path = liveos/squashfs.img",
        "path = casper/filesystem.squashfs", "path = arch/boot/",
        "path = images/pxeboot/vmlinuz", NULL,
    };
    static const gchar *const fedora_live_uefi_paths[] = {
        "path = efi/boot/bootx64.efi",
        "path = efi/boot/grub.cfg",
        "path = boot/grub2/grub.cfg",
        "path = liveos/squashfs.img",
        NULL,
    };
    g_autofree gchar *lower = NULL;
    LucImageClassification *result;

    g_return_val_if_fail(listing != NULL, NULL);
    lower = g_ascii_strdown(listing, -1);
    result = g_new0(LucImageClassification, 1);
    if (contains_any(lower, windows_paths)) {
        result->kind = LUC_IMAGE_KIND_WINDOWS_ISO;
        result->distribution = g_strdup("Windows");
    } else if (contains_any(lower, linux_paths)) {
        result->kind = LUC_IMAGE_KIND_LINUX_ISO;
        if (strstr(lower, "fedora") != NULL ||
            strstr(lower, "liveos/squashfs.img") != NULL)
            result->distribution = g_strdup("Fedora Linux");
        else if (strstr(lower, "ubuntu") != NULL ||
                 strstr(lower, "casper/filesystem.squashfs") != NULL)
            result->distribution = g_strdup("Ubuntu Linux");
        else if (strstr(lower, "debian") != NULL ||
                 strstr(lower, "path = install.amd/") != NULL)
            result->distribution = g_strdup("Debian GNU/Linux");
        else if (strstr(lower, "arch/boot/") != NULL)
            result->distribution = g_strdup("Arch Linux");
        else
            result->distribution = g_strdup("Linux");
        if (strstr(lower, "fedora") != NULL &&
            strstr(lower, "path = liveos/squashfs.img") != NULL) {
            gboolean complete = TRUE;
            for (guint i = 0; fedora_live_uefi_paths[i] != NULL; i++) {
                if (strstr(lower, fedora_live_uefi_paths[i]) == NULL) {
                    complete = FALSE;
                    break;
                }
            }
            if (complete)
                result->linux_profile = LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI;
        }
    } else {
        result->kind = LUC_IMAGE_KIND_RAW_OR_UNKNOWN;
        result->distribution = g_strdup("Raw/unknown");
    }
    if (strstr(lower, "bootaa64.efi") != NULL ||
        strstr(lower, "aarch64") != NULL || strstr(lower, "arm64") != NULL)
        result->architecture = g_strdup("ARM64");
    else if (strstr(lower, "bootx64.efi") != NULL ||
             strstr(lower, "x86_64") != NULL || strstr(lower, "amd64") != NULL)
        result->architecture = g_strdup("x64");
    else
        result->architecture = g_strdup("unknown");
    return result;
}

LucImageClassification *
luc_image_classify(const gchar *path,
                   GCancellable *cancellable,
                   GError **error)
{
    g_autofree gchar *tool = NULL;
    g_autoptr(GSubprocess) process = NULL;
    g_autofree gchar *stdout_text = NULL;
    g_autofree gchar *stderr_text = NULL;
    struct stat metadata;
    int fd;

    g_return_val_if_fail(path != NULL, NULL);
    fd = g_open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (fd < 0 || fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        if (fd >= 0)
            close(fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Image must be a regular local file");
        return NULL;
    }
    close(fd);
    tool = g_find_program_in_path("7z");
    if (tool == NULL)
        tool = g_find_program_in_path("7zz");
    if (tool == NULL) {
        LucImageClassification *fallback =
            g_new0(LucImageClassification, 1);
        fallback->kind = LUC_IMAGE_KIND_RAW_OR_UNKNOWN;
        fallback->distribution = g_strdup("Raw/unknown");
        fallback->architecture = g_strdup("unknown");
        return fallback;
    }
    process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                               G_SUBPROCESS_FLAGS_STDERR_PIPE,
                               error, tool, "l", "-slt", "--", path, NULL);
    if (process == NULL ||
        !g_subprocess_communicate_utf8(process, NULL, cancellable,
                                       &stdout_text, &stderr_text, error))
        return NULL;
    if (!g_subprocess_get_successful(process)) {
        LucImageClassification *fallback =
            g_new0(LucImageClassification, 1);
        fallback->kind = LUC_IMAGE_KIND_RAW_OR_UNKNOWN;
        fallback->distribution = g_strdup("Raw/unknown");
        fallback->architecture = g_strdup("unknown");
        return fallback;
    }
    return luc_image_classify_listing(stdout_text);
}
