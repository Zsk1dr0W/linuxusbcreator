/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>

#include "core/image-writer.h"

static gchar *
make_temp_path(const gchar *suffix)
{
    return g_build_filename(g_get_tmp_dir(),
                            g_strdup_printf("linuxusbcreator-test-%u-%s",
                                            g_random_int(), suffix),
                            NULL);
}

static void
write_fixture(const gchar *path)
{
    const gchar content[] = "Linux USB Creator test image\n";
    g_assert_true(g_file_set_contents(path, content, sizeof(content) - 1, NULL));
}

static void
test_sha256(void)
{
    g_autofree gchar *source = make_temp_path("source.img");
    g_autofree gchar *digest = NULL;
    g_autoptr(GError) error = NULL;

    write_fixture(source);
    digest = luc_image_sha256(source, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(digest, ==,
                    "acf997217617ac8ab9c261a3fa5ef99e880f00b3d9c4efc3d0e4d92d8b5a4ca7");
    g_assert_cmpint(g_remove(source), ==, 0);
}

static void
test_copy_and_verify(void)
{
    g_autofree gchar *source = make_temp_path("source.img");
    g_autofree gchar *destination = make_temp_path("destination.img");
    g_autoptr(GError) error = NULL;

    write_fixture(source);
    g_assert_true(luc_image_copy_regular_file(source, destination, TRUE, NULL, NULL, NULL,
                                              &error));
    g_assert_no_error(error);
    g_assert_true(g_file_test(destination, G_FILE_TEST_IS_REGULAR));
    g_assert_cmpint(g_remove(source), ==, 0);
    g_assert_cmpint(g_remove(destination), ==, 0);
}

static void
test_cancelled_copy(void)
{
    g_autofree gchar *source = make_temp_path("source.img");
    g_autofree gchar *destination = make_temp_path("destination.img");
    g_autoptr(GCancellable) cancellable = g_cancellable_new();
    g_autoptr(GError) error = NULL;

    write_fixture(source);
    g_cancellable_cancel(cancellable);
    g_assert_false(luc_image_copy_regular_file(source, destination, FALSE, cancellable,
                                               NULL, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_remove(source);
    g_remove(destination);
}

static void
test_destination_must_be_regular_file(void)
{
    g_autofree gchar *source = make_temp_path("source.img");
    g_autoptr(GError) error = NULL;

    write_fixture(source);
    g_assert_false(luc_image_copy_regular_file(source, "/dev/null", FALSE, NULL, NULL, NULL,
                                               &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_remove(source);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image/sha256", test_sha256);
    g_test_add_func("/image/copy-and-verify", test_copy_and_verify);
    g_test_add_func("/image/cancelled", test_cancelled_copy);
    g_test_add_func("/image/regular-destination", test_destination_must_be_regular_file);
    return g_test_run();
}
