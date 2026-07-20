/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define LUC_FAT32_MAX_FILE_SIZE G_GUINT64_CONSTANT(4294967295)

typedef enum {
    LUC_WINDOWS_IMAGE_FORMAT_UNKNOWN,
    LUC_WINDOWS_IMAGE_FORMAT_ISO9660,
    LUC_WINDOWS_IMAGE_FORMAT_UDF,
} LucWindowsImageFormat;

typedef enum {
    LUC_WINDOWS_INSTALL_PAYLOAD_NONE,
    LUC_WINDOWS_INSTALL_PAYLOAD_WIM,
    LUC_WINDOWS_INSTALL_PAYLOAD_ESD,
} LucWindowsInstallPayload;

typedef struct {
    LucWindowsImageFormat format;
    LucWindowsInstallPayload install_payload;
    gchar *install_path;
    guint64 image_size;
    guint64 content_size;
    guint64 largest_file_size;
    guint64 install_size;
    guint file_count;
    guint oversized_file_count;
    gboolean has_boot_wim;
    gboolean has_install_wim;
    gboolean has_install_esd;
    gboolean has_bootmgr;
    gboolean has_boot_bcd;
    gboolean supports_bios;
    gboolean supports_uefi_x64;
    gboolean supports_uefi_arm64;
    gboolean has_unsafe_paths;
    gboolean has_case_collisions;
    gboolean requires_wim_split;
    gboolean fat32_compatible;
    gboolean is_windows_installer;
} LucWindowsImageInfo;

void luc_windows_image_info_free(LucWindowsImageInfo *info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(LucWindowsImageInfo, luc_windows_image_info_free)

const gchar *luc_windows_image_format_to_string(LucWindowsImageFormat format);
const gchar *luc_windows_install_payload_to_string(LucWindowsInstallPayload payload);

gboolean luc_windows_image_parse_7z_listing(const gchar *listing,
                                            LucWindowsImageInfo **info,
                                            GError **error);

LucWindowsImageInfo *luc_windows_image_inspect(const gchar *path,
                                               GCancellable *cancellable,
                                               GError **error);

G_END_DECLS
