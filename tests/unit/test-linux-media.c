/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "core/linux-media.h"

static void
make_file(const gchar *root, const gchar *relative, const gchar *contents)
{
    g_autofree gchar *path = g_build_filename(root, relative, NULL);
    g_autofree gchar *parent = g_path_get_dirname(path);

    g_assert_cmpint(g_mkdir_with_parents(parent, 0755), ==, 0);
    g_assert_true(g_file_set_contents(path, contents, -1, NULL));
}

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
        remove_tree(child);
    }
    g_rmdir(path);
}

static void
test_fedora_persistent_copy(void)
{
    g_autofree gchar *root = g_dir_make_tmp("luc-linux-media-XXXXXX", NULL);
    g_autofree gchar *source = g_build_filename(root, "source", NULL);
    g_autofree gchar *boot = g_build_filename(root, "boot-target", NULL);
    g_autofree gchar *data = g_build_filename(root, "data-target", NULL);
    g_autofree gchar *grub_path = NULL;
    g_autofree gchar *grub = NULL;
    g_autofree gchar *overlay = NULL;
    guint64 content_size = 0;
    g_autoptr(GError) error = NULL;

    g_assert_cmpint(g_mkdir(source, 0755), ==, 0);
    g_assert_cmpint(g_mkdir(boot, 0755), ==, 0);
    g_assert_cmpint(g_mkdir(data, 0755), ==, 0);
    make_file(source, "EFI/BOOT/BOOTX64.EFI", "signed-shim");
    make_file(source, "EFI/BOOT/grub.cfg",
              "search --file --set=root /boot/marker\n");
    make_file(source, "boot/marker", "marker");
    make_file(source, "boot/grub2/grub.cfg",
              "linux /boot/linux root=live:CDLABEL=Fedora-Test rd.live.image\n");
    make_file(source, "LiveOS/squashfs.img", "squashfs-data");

    g_assert_true(luc_linux_media_validate_fedora(source, &content_size,
                                                  &error));
    g_assert_no_error(error);
    g_assert_cmpuint(content_size, >, 0);
    g_assert_true(luc_linux_media_copy_fedora(
        source, boot, data, TRUE, NULL, NULL, NULL, NULL, &error));
    g_assert_no_error(error);
    grub_path = g_build_filename(boot, "boot/grub2/grub.cfg", NULL);
    g_assert_true(g_file_get_contents(grub_path, &grub, NULL, &error));
    g_assert_no_error(error);
    g_assert_nonnull(strstr(grub, "root=live:LABEL=LUC-LIVE"));
    g_assert_nonnull(strstr(grub,
                           "rd.live.overlay=LABEL=LUC-LIVE:/overlayfs"));
    overlay = g_build_filename(data, "overlayfs", NULL);
    g_assert_true(g_file_test(overlay, G_FILE_TEST_IS_DIR));
    remove_tree(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/linux-media/fedora-persistent-copy",
                    test_fedora_persistent_copy);
    return g_test_run();
}
