/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "core/windows-media.h"

static void
remove_tree(const gchar *path)
{
    g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
    const gchar *name;

    if (directory == NULL) {
        g_remove(path);
        return;
    }
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *child = g_build_filename(path, name, NULL);
        if (g_file_test(child, G_FILE_TEST_IS_DIR) &&
            !g_file_test(child, G_FILE_TEST_IS_SYMLINK))
            remove_tree(child);
        else
            g_remove(child);
    }
    g_rmdir(path);
}

static LucWindowsImageInfo
compatible_info(guint64 content_size)
{
    return (LucWindowsImageInfo){
        .install_payload = LUC_WINDOWS_INSTALL_PAYLOAD_WIM,
        .install_path = "sources/install.wim",
        .content_size = content_size,
        .supports_uefi_x64 = TRUE,
        .fat32_compatible = TRUE,
        .is_windows_installer = TRUE,
    };
}

static void
test_copy_regular_tree(void)
{
    g_autofree gchar *root = g_dir_make_tmp("luc-windows-media-XXXXXX", NULL);
    g_autofree gchar *source = g_build_filename(root, "source", NULL);
    g_autofree gchar *destination = g_build_filename(root, "destination", NULL);
    g_autofree gchar *sources = g_build_filename(source, "sources", NULL);
    g_autofree gchar *efi = g_build_filename(source, "efi", "boot", NULL);
    g_autofree gchar *boot = g_build_filename(efi, "bootx64.efi", NULL);
    g_autofree gchar *wim = g_build_filename(sources, "install.wim", NULL);
    g_autofree gchar *copied_boot =
        g_build_filename(destination, "efi", "boot", "bootx64.efi", NULL);
    g_autofree gchar *copied_wim =
        g_build_filename(destination, "sources", "install.wim", NULL);
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    g_autoptr(GError) error = NULL;
    LucWindowsImageInfo info = compatible_info(12);

    g_assert_cmpint(g_mkdir_with_parents(sources, 0755), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(efi, 0755), ==, 0);
    g_assert_cmpint(g_mkdir(destination, 0755), ==, 0);
    g_assert_true(g_file_set_contents(boot, "EFI", 3, NULL));
    g_assert_true(g_file_set_contents(wim, "WIM-DATA!", 9, NULL));
    g_assert_true(luc_windows_media_copy(source, destination, &info, NULL,
                                         NULL, NULL, NULL, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(copied_boot, &contents, &length, NULL));
    g_assert_cmpmem(contents, length, "EFI", 3);
    g_clear_pointer(&contents, g_free);
    g_assert_true(g_file_get_contents(copied_wim, &contents, &length, NULL));
    g_assert_cmpmem(contents, length, "WIM-DATA!", 9);
    remove_tree(root);
}

static void
test_rejects_symlink(void)
{
    g_autofree gchar *root = g_dir_make_tmp("luc-windows-media-XXXXXX", NULL);
    g_autofree gchar *source = g_build_filename(root, "source", NULL);
    g_autofree gchar *destination = g_build_filename(root, "destination", NULL);
    g_autofree gchar *link = g_build_filename(source, "unsafe", NULL);
    g_autoptr(GError) error = NULL;
    LucWindowsImageInfo info = compatible_info(0);

    g_assert_cmpint(g_mkdir(source, 0755), ==, 0);
    g_assert_cmpint(g_mkdir(destination, 0755), ==, 0);
    g_assert_cmpint(symlink("/etc/passwd", link), ==, 0);
    g_assert_false(luc_windows_media_copy(source, destination, &info, NULL,
                                          NULL, NULL, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    remove_tree(root);
}

static void
test_rejects_incompatible_image(void)
{
    g_autoptr(GError) error = NULL;
    LucWindowsImageInfo info = compatible_info(0);

    info.fat32_compatible = FALSE;
    g_assert_false(luc_windows_media_copy("/", "/", &info, NULL,
                                          NULL, NULL, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/windows-media/copy-tree", test_copy_regular_tree);
    g_test_add_func("/windows-media/reject-symlink", test_rejects_symlink);
    g_test_add_func("/windows-media/reject-incompatible", test_rejects_incompatible_image);
    return g_test_run();
}
