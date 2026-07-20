/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "core/operation-log.h"

static void
test_append_jsonl(void)
{
    g_autofree gchar *path = g_build_filename(g_get_tmp_dir(),
                                              "linuxusbcreator-operation-test.jsonl", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *contents = NULL;
    gsize length = 0;

    g_remove(path);
    g_assert_true(luc_operation_log_append(path, "copy", "completed",
                                           "/tmp/a.img", "/tmp/b.img", 42,
                                           "abc123", &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, >, 0);
    g_assert_nonnull(strstr(contents, "\"source\":\"/tmp/a.img\""));
    g_assert_nonnull(strstr(contents, "\"status\":\"completed\""));
    g_assert_nonnull(strstr(contents, "\"bytes\":42"));
    g_assert_true(g_str_has_suffix(contents, "\n"));
    g_assert_cmpint(g_remove(path), ==, 0);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/operation-log/jsonl", test_append_jsonl);
    return g_test_run();
}
