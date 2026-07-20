/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"

#include <adwaita.h>
#include <stdio.h>

#include "app/window.h"
#include "linux/device-monitor.h"

#define APPLICATION_ID "io.github.zsk1dr0w.LinuxUsbCreator"

static LucDeviceMonitor *monitor;

static gchar *
json_escape(const gchar *value)
{
    GString *escaped = g_string_new(NULL);
    const guchar *cursor = (const guchar *)(value != NULL ? value : "");

    for (; *cursor != '\0'; cursor++) {
        switch (*cursor) {
        case '\\': g_string_append(escaped, "\\\\"); break;
        case '"': g_string_append(escaped, "\\\""); break;
        case '\b': g_string_append(escaped, "\\b"); break;
        case '\f': g_string_append(escaped, "\\f"); break;
        case '\n': g_string_append(escaped, "\\n"); break;
        case '\r': g_string_append(escaped, "\\r"); break;
        case '\t': g_string_append(escaped, "\\t"); break;
        default:
            if (*cursor < 0x20)
                g_string_append_printf(escaped, "\\u%04x", *cursor);
            else
                g_string_append_c(escaped, (gchar)*cursor);
        }
    }
    return g_string_free(escaped, FALSE);
}

static int
run_diagnostics(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LucDeviceMonitor) local_monitor = luc_device_monitor_new(&error);
    g_autoptr(GPtrArray) devices = NULL;

    if (local_monitor == NULL) {
        g_printerr("Unable to connect to UDisks2: %s\n", error->message);
        return 2;
    }

    devices = luc_device_monitor_dup_devices(local_monitor);
    puts("[");
    for (guint i = 0; i < devices->len; i++) {
        LucDevice *device = g_ptr_array_index(devices, i);
        g_autofree gchar *path = json_escape(device->device);
        g_autofree gchar *model = json_escape(device->model);
        g_autofree gchar *serial = json_escape(device->serial);
        g_autofree gchar *bus = json_escape(device->connection_bus);

        printf("  {%s\"device\":\"%s\",\"model\":\"%s\","
               "\"serial\":\"%s\",\"bus\":\"%s\",\"size\":%" G_GUINT64_FORMAT ","
               "\"removable\":%s,\"eligibility\":\"%s\"}",
               i == 0 ? "" : "",
               path,
               model,
               serial,
               bus,
               device->size,
               device->removable || device->media_removable ? "true" : "false",
               luc_device_eligibility_to_string(luc_device_get_eligibility(device)));
        puts(i + 1 == devices->len ? "" : ",");
    }
    puts("]");
    return 0;
}

static void
on_activate(GtkApplication *application)
{
    GtkWindow *window = gtk_application_get_active_window(application);
    if (window == NULL)
        window = GTK_WINDOW(luc_window_new(ADW_APPLICATION(application), monitor));
    gtk_window_present(window);
}

int
main(int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(AdwApplication) application = NULL;
    int status;

    if (argc == 2 && g_str_equal(argv[1], "--diagnose"))
        return run_diagnostics();
    if (argc == 2 && (g_str_equal(argv[1], "--version") || g_str_equal(argv[1], "-V"))) {
        g_print("linuxusbcreator %s\n", PROJECT_VERSION);
        return 0;
    }

    monitor = luc_device_monitor_new(&error);
    if (monitor == NULL) {
        g_printerr("Unable to connect to UDisks2: %s\n", error->message);
        return 2;
    }

    application = adw_application_new(APPLICATION_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), NULL);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_clear_object(&monitor);
    return status;
}

