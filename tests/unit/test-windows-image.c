/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>

#include "core/windows-image.h"

static const gchar *valid_windows_listing =
    "Path = windows.iso\n"
    "Type = Udf\n"
    "Physical Size = 6000000000\n"
    "----------\n"
    "Path = bootmgr\nSize = 400000\nAttributes = A\n\n"
    "Path = boot/bcd\nSize = 20000\nAttributes = A\n\n"
    "Path = boot/etfsboot.com\nSize = 4096\nAttributes = A\n\n"
    "Path = efi/boot/bootx64.efi\nSize = 1000000\nAttributes = A\n\n"
    "Path = sources/boot.wim\nSize = 500000000\nAttributes = A\n\n"
    "Path = sources/install.wim\nSize = 5000000000\nAttributes = A\n";

static void
test_valid_udf_requires_split(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;

    g_assert_true(luc_windows_image_parse_7z_listing(valid_windows_listing,
                                                     &info, &error));
    g_assert_no_error(error);
    g_assert_cmpint(info->format, ==, LUC_WINDOWS_IMAGE_FORMAT_UDF);
    g_assert_true(info->is_windows_installer);
    g_assert_true(info->supports_bios);
    g_assert_true(info->supports_uefi_x64);
    g_assert_false(info->supports_uefi_arm64);
    g_assert_true(info->has_boot_wim);
    g_assert_cmpstr(info->boot_wim_path, ==, "sources/boot.wim");
    g_assert_cmpint(info->install_payload, ==, LUC_WINDOWS_INSTALL_PAYLOAD_WIM);
    g_assert_cmpstr(info->install_path, ==, "sources/install.wim");
    g_assert_cmpuint(info->install_size, ==, G_GUINT64_CONSTANT(5000000000));
    g_assert_true(info->requires_wim_split);
    g_assert_true(info->fat32_compatible);
    g_assert_cmpuint(info->file_count, ==, 6);
}

static void
test_rejects_unknown_filesystem(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;

    g_assert_false(luc_windows_image_parse_7z_listing(
        "Type = Rar\n----------\nPath = sources/boot.wim\nSize = 1\n",
        &info, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

static void
test_rejects_invalid_size(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;

    g_assert_false(luc_windows_image_parse_7z_listing(
        "Type = Udf\n----------\nPath = file\nSize = -1\n",
        &info, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_unsafe_and_case_collision(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;
    const gchar *listing =
        "Type = Iso\n----------\n"
        "Path = sources\nSize = \nFolder = +\n\n"
        "Path = ../escape\nSize = 1\nAttributes = A\n\n"
        "Path = SOURCES/BOOT.WIM\nSize = 2\nAttributes = A\n\n"
        "Path = sources/boot.wim\nSize = 2\nAttributes = A\n\n"
        "Path = sources/install.esd\nSize = 3\nAttributes = A\n\n"
        "Path = efi/boot/bootaa64.efi\nSize = 4\nAttributes = A\n";

    g_assert_true(luc_windows_image_parse_7z_listing(listing, &info, &error));
    g_assert_no_error(error);
    g_assert_true(info->has_unsafe_paths);
    g_assert_true(info->has_case_collisions);
    g_assert_false(info->is_windows_installer);
    g_assert_true(info->supports_uefi_arm64);
    g_assert_cmpuint(info->file_count, ==, 5);
}

static void
test_large_esd_is_not_fat32_compatible(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;
    const gchar *listing =
        "Type = Udf\n----------\n"
        "Path = sources/boot.wim\nSize = 2\nAttributes = A\n\n"
        "Path = sources/install.esd\nSize = 5000000000\nAttributes = A\n\n"
        "Path = efi/boot/bootx64.efi\nSize = 4\nAttributes = A\n";

    g_assert_true(luc_windows_image_parse_7z_listing(listing, &info, &error));
    g_assert_no_error(error);
    g_assert_true(info->is_windows_installer);
    g_assert_false(info->requires_wim_split);
    g_assert_false(info->fat32_compatible);
}

static void
test_ambiguous_payload_and_link(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;
    const gchar *listing =
        "Type = Udf\n----------\n"
        "Path = sources/boot.wim\nSize = 2\nFolder = -\n\n"
        "Path = sources/install.wim\nSize = 3\nFolder = -\n\n"
        "Path = sources/install.esd\nSize = 4\nFolder = -\n\n"
        "Path = efi/boot/bootx64.efi\nSize = 4\nFolder = -\n"
        "Symbolic Link = elsewhere\n";

    g_assert_true(luc_windows_image_parse_7z_listing(listing, &info, &error));
    g_assert_no_error(error);
    g_assert_true(info->has_install_wim);
    g_assert_true(info->has_install_esd);
    g_assert_cmpint(info->install_payload, ==, LUC_WINDOWS_INSTALL_PAYLOAD_NONE);
    g_assert_true(info->has_unsafe_paths);
    g_assert_false(info->is_windows_installer);
}

static void
test_directories_do_not_satisfy_required_files(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucWindowsImageInfo) info = NULL;
    const gchar *listing =
        "Type = Udf\n----------\n"
        "Path = sources/boot.wim\nSize = \nFolder = +\n\n"
        "Path = sources/install.wim\nSize = \nFolder = +\n\n"
        "Path = efi/boot/bootx64.efi\nSize = \nFolder = +\n";

    g_assert_true(luc_windows_image_parse_7z_listing(listing, &info, &error));
    g_assert_no_error(error);
    g_assert_false(info->has_boot_wim);
    g_assert_cmpint(info->install_payload, ==, LUC_WINDOWS_INSTALL_PAYLOAD_NONE);
    g_assert_false(info->supports_uefi_x64);
    g_assert_false(info->is_windows_installer);
    g_assert_cmpuint(info->file_count, ==, 0);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/windows-image/valid-udf-split", test_valid_udf_requires_split);
    g_test_add_func("/windows-image/unknown-filesystem", test_rejects_unknown_filesystem);
    g_test_add_func("/windows-image/invalid-size", test_rejects_invalid_size);
    g_test_add_func("/windows-image/unsafe-collision", test_unsafe_and_case_collision);
    g_test_add_func("/windows-image/large-esd", test_large_esd_is_not_fat32_compatible);
    g_test_add_func("/windows-image/ambiguous-link", test_ambiguous_payload_and_link);
    g_test_add_func("/windows-image/required-files-not-directories",
                    test_directories_do_not_satisfy_required_files);
    return g_test_run();
}
