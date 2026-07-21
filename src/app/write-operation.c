/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "app/write-operation.h"

#include <signal.h>

#include "core/helper-protocol.h"

struct _LucWriteOperation {
    GObject parent_instance;
    gchar *image_path;
    gchar *device_path;
    gchar *serial;
    guint64 device_size;
    gboolean verify;
    gboolean windows_mode;
    gboolean linux_iso_mode;
    gboolean linux_persistence;
    LucWindowsFirmware windows_firmware;
    gboolean running;
    gboolean cancelled;
    gboolean process_done;
    gboolean stream_done;
    gboolean process_success;
    gboolean finished_emitted;
    GSubprocess *process;
    GDataInputStream *output;
    GString *diagnostics;
};

enum {
    PHASE_CHANGED,
    PROGRESS,
    FINISHED,
    N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE(LucWriteOperation, luc_write_operation, G_TYPE_OBJECT)

static void read_next_line(LucWriteOperation *self);

static void
append_diagnostic(LucWriteOperation *self, const gchar *message)
{
    if (message == NULL || message[0] == '\0')
        return;
    if (self->diagnostics->len > 0)
        g_string_append_c(self->diagnostics, '\n');
    g_string_append(self->diagnostics, message);
}

LucWriteOperation *
luc_write_operation_new_linux_iso(const gchar *image_path,
                                  const gchar *device_path,
                                  const gchar *serial,
                                  guint64 device_size,
                                  gboolean persistence)
{
    LucWriteOperation *self = luc_write_operation_new(
        image_path, device_path, serial, device_size, TRUE);

    if (self != NULL) {
        self->linux_iso_mode = TRUE;
        self->linux_persistence = persistence;
    }
    return self;
}

LucWriteOperation *
luc_write_operation_new_windows(const gchar *image_path,
                                const gchar *device_path,
                                const gchar *serial,
                                guint64 device_size,
                                LucWindowsFirmware firmware)
{
    LucWriteOperation *self = luc_write_operation_new(
        image_path, device_path, serial, device_size, TRUE);

    if (self != NULL)
        self->windows_mode = TRUE;
    if (self != NULL)
        self->windows_firmware = firmware;
    return self;
}

static void
finish_if_ready(LucWriteOperation *self)
{
    const gchar *message;

    if (self->finished_emitted || !self->process_done || !self->stream_done)
        return;
    self->finished_emitted = TRUE;
    self->running = FALSE;
    message = g_strstrip(self->diagnostics->str);
    g_signal_emit(self, signals[FINISHED], 0,
                  self->process_success,
                  self->cancelled,
                  message);
}

static void
on_output_line(GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(LucWriteOperation) self = user_data;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *line = NULL;
    LucHelperEvent event;

    line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source),
                                                result, NULL, &error);
    if (line == NULL) {
        if (error != NULL)
            append_diagnostic(self, error->message);
        self->stream_done = TRUE;
        finish_if_ready(self);
        return;
    }

    if (luc_helper_event_parse(line, &event)) {
        if (event.type == LUC_HELPER_EVENT_PHASE)
            g_signal_emit(self, signals[PHASE_CHANGED], 0, event.phase);
        else if (event.type == LUC_HELPER_EVENT_PROGRESS)
            g_signal_emit(self, signals[PROGRESS], 0,
                          event.completed, event.total);
    } else {
        append_diagnostic(self, line);
    }
    read_next_line(self);
}

static void
read_next_line(LucWriteOperation *self)
{
    g_data_input_stream_read_line_async(self->output,
                                        G_PRIORITY_DEFAULT,
                                        NULL,
                                        on_output_line,
                                        g_object_ref(self));
}

static void
on_process_waited(GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(LucWriteOperation) self = user_data;
    g_autoptr(GError) error = NULL;

    if (!g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error))
        append_diagnostic(self, error->message);
    else
        self->process_success = g_subprocess_get_successful(self->process);
    self->process_done = TRUE;
    finish_if_ready(self);
}

static void
luc_write_operation_dispose(GObject *object)
{
    LucWriteOperation *self = LUC_WRITE_OPERATION(object);

    if (self->running && self->process != NULL)
        g_subprocess_send_signal(self->process, SIGINT);
    g_clear_object(&self->output);
    g_clear_object(&self->process);
    G_OBJECT_CLASS(luc_write_operation_parent_class)->dispose(object);
}

static void
luc_write_operation_finalize(GObject *object)
{
    LucWriteOperation *self = LUC_WRITE_OPERATION(object);

    g_clear_pointer(&self->image_path, g_free);
    g_clear_pointer(&self->device_path, g_free);
    g_clear_pointer(&self->serial, g_free);
    if (self->diagnostics != NULL)
        g_string_free(self->diagnostics, TRUE);
    G_OBJECT_CLASS(luc_write_operation_parent_class)->finalize(object);
}

static void
luc_write_operation_class_init(LucWriteOperationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = luc_write_operation_dispose;
    object_class->finalize = luc_write_operation_finalize;
    signals[PHASE_CHANGED] = g_signal_new(
        "phase-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
    signals[PROGRESS] = g_signal_new(
        "progress", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT64, G_TYPE_UINT64);
    signals[FINISHED] = g_signal_new(
        "finished", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 3,
        G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING);
}

static void
luc_write_operation_init(LucWriteOperation *self)
{
    self->diagnostics = g_string_new(NULL);
}

LucWriteOperation *
luc_write_operation_new(const gchar *image_path,
                        const gchar *device_path,
                        const gchar *serial,
                        guint64 device_size,
                        gboolean verify)
{
    LucWriteOperation *self;

    g_return_val_if_fail(image_path != NULL, NULL);
    g_return_val_if_fail(device_path != NULL, NULL);
    g_return_val_if_fail(serial != NULL && serial[0] != '\0', NULL);
    g_return_val_if_fail(device_size > 0, NULL);
    self = g_object_new(LUC_TYPE_WRITE_OPERATION, NULL);
    self->image_path = g_strdup(image_path);
    self->device_path = g_strdup(device_path);
    self->serial = g_strdup(serial);
    self->device_size = device_size;
    self->verify = verify;
    return self;
}

void
luc_write_operation_start(LucWriteOperation *self)
{
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *size = NULL;
    g_autofree gchar *executable = NULL;
    GInputStream *stdout_pipe;

    g_return_if_fail(LUC_IS_WRITE_OPERATION(self));
    g_return_if_fail(!self->running && self->process == NULL);
    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                         G_SUBPROCESS_FLAGS_STDERR_MERGE);
    size = g_strdup_printf("%" G_GUINT64_FORMAT, self->device_size);
    if (self->windows_mode || self->linux_iso_mode) {
        executable = g_file_read_link("/proc/self/exe", &error);
        if (executable != NULL && self->windows_mode)
            self->process = g_subprocess_launcher_spawn(
                launcher, &error, executable, "--write-windows",
                self->image_path, self->device_path, self->serial, size,
                "--firmware",
                luc_windows_firmware_to_string(self->windows_firmware), NULL);
        else if (executable != NULL)
            self->process = g_subprocess_launcher_spawn(
                launcher, &error, executable, "--write-linux",
                self->image_path, self->device_path, self->serial, size,
                "--persistence", self->linux_persistence ? "yes" : "no", NULL);
    } else {
        self->process = g_subprocess_launcher_spawn(launcher, &error,
                                                    "pkexec", LUC_HELPER_PATH,
                                                    "write", self->image_path,
                                                    self->device_path, self->serial,
                                                    size, "--verify",
                                                    self->verify ? "yes" : "no",
                                                    NULL);
    }
    if (self->process == NULL) {
        append_diagnostic(self, error->message);
        self->process_done = TRUE;
        self->stream_done = TRUE;
        finish_if_ready(self);
        return;
    }

    self->running = TRUE;
    stdout_pipe = g_subprocess_get_stdout_pipe(self->process);
    self->output = g_data_input_stream_new(stdout_pipe);
    read_next_line(self);
    g_subprocess_wait_async(self->process, NULL, on_process_waited,
                            g_object_ref(self));
}

void
luc_write_operation_cancel(LucWriteOperation *self)
{
    g_return_if_fail(LUC_IS_WRITE_OPERATION(self));
    if (!self->running || self->process == NULL || self->cancelled)
        return;
    self->cancelled = TRUE;
    g_subprocess_send_signal(self->process, SIGINT);
}

gboolean
luc_write_operation_is_running(LucWriteOperation *self)
{
    g_return_val_if_fail(LUC_IS_WRITE_OPERATION(self), FALSE);
    return self->running;
}
