/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>

#include "app/write-operation.h"

typedef struct {
    gboolean called;
    gboolean success;
    gchar *message;
} FinishedState;

static void
on_finished(LucWriteOperation *operation,
            gboolean success,
            gboolean cancelled,
            const gchar *message,
            FinishedState *state)
{
    (void)operation;
    (void)cancelled;
    state->called = TRUE;
    state->success = success;
    state->message = g_strdup(message);
}

static void
test_spawn_failure_is_reported(void)
{
    g_autoptr(LucWriteOperation) operation = NULL;
    FinishedState state = {0};

    g_setenv("PATH", "/path/that/does/not/exist", TRUE);
    operation = luc_write_operation_new("/tmp/image.iso", "/dev/sdz",
                                        "SERIAL", 1024, TRUE);
    g_signal_connect(operation, "finished", G_CALLBACK(on_finished), &state);
    luc_write_operation_start(operation);
    g_assert_true(state.called);
    g_assert_false(state.success);
    g_assert_nonnull(state.message);
    g_assert_false(luc_write_operation_is_running(operation));
    g_free(state.message);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/write-operation/spawn-failure", test_spawn_failure_is_reported);
    return g_test_run();
}
