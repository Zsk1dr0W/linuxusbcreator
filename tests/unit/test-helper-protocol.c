/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>

#include "core/helper-protocol.h"

static void
test_progress(void)
{
    LucHelperEvent event;

    g_assert_true(luc_helper_event_parse(
        "{\"event\":\"progress\",\"completed\":1048576,\"total\":2097152}",
        &event));
    g_assert_cmpint(event.type, ==, LUC_HELPER_EVENT_PROGRESS);
    g_assert_cmpuint(event.completed, ==, 1048576);
    g_assert_cmpuint(event.total, ==, 2097152);
}

static void
test_phases(void)
{
    LucHelperEvent event;

    g_assert_true(luc_helper_event_parse(
        "{\"event\":\"phase\",\"name\":\"verifying\"}", &event));
    g_assert_cmpint(event.type, ==, LUC_HELPER_EVENT_PHASE);
    g_assert_cmpint(event.phase, ==, LUC_HELPER_PHASE_VERIFYING);
}

static void
test_rejects_invalid_progress(void)
{
    LucHelperEvent event;

    g_assert_false(luc_helper_event_parse(
        "{\"event\":\"progress\",\"completed\":3,\"total\":2}", &event));
    g_assert_false(luc_helper_event_parse("not-json", &event));
}

static void
test_prepared_partition(void)
{
    g_autofree gchar *partition = NULL;

    g_assert_true(luc_helper_parse_prepared(
        "{\"event\":\"prepared\",\"partition\":\"/dev/sdb1\"}",
        &partition));
    g_assert_cmpstr(partition, ==, "/dev/sdb1");
    g_clear_pointer(&partition, g_free);
    g_assert_false(luc_helper_parse_prepared(
        "{\"event\":\"prepared\",\"partition\":\"/tmp/file\"}",
        &partition));
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/helper-protocol/progress", test_progress);
    g_test_add_func("/helper-protocol/phases", test_phases);
    g_test_add_func("/helper-protocol/reject-invalid", test_rejects_invalid_progress);
    g_test_add_func("/helper-protocol/prepared", test_prepared_partition);
    return g_test_run();
}
