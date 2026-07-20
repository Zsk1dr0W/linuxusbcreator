/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <stdio.h>

#include "app/window.h"
#include "core/image-writer.h"
#include "linux/device-monitor.h"

#define APPLICATION_ID "io.github.zsk1dr0w.LinuxUsbCreator"

static LucDeviceMonitor *monitor;

static void
print_help(const gchar *program)
{
    g_print("%s\n", _("Usage:"));
    g_print("  %s [OPTION…]\n", program);
    g_print("  %s --diagnose\n", program);
    g_print("  %s --sha256 IMAGE\n", program);
    g_print("  %s --write-image IMAGE DEVICE SERIAL SIZE [--no-verify]\n\n", program);
    g_print("%s\n", _("Commands:"));
    g_print("  %-45s %s\n", "--diagnose", _("Print detected devices and eligibility as JSON"));
    g_print("  %-45s %s\n", "--sha256 IMAGE", _("Compute SHA-256 for a regular image file"));
    g_print("  %-45s %s\n", "--write-image IMAGE DEVICE SERIAL SIZE", _("Write and verify an image on a confirmed USB device"));
    g_print("  %-45s %s\n\n", "--no-verify", _("Skip read-back verification (not recommended)"));
    g_print("%s\n", _("Options:"));
    g_print("  %-45s %s\n", "-h, --help", _("Show this help"));
    g_print("  %-45s %s\n", "-V, --version", _("Show program version"));
}

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
run_sha256(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *digest = luc_image_sha256(path, NULL, &error);

    if (digest == NULL) {
        g_printerr("Unable to compute SHA-256: %s\n", error->message);
        return 2;
    }
    g_print("%s  %s\n", digest, path);
    return 0;
}

static int
run_privileged_write(const gchar *image,
                     const gchar *device,
                     const gchar *serial,
                     const gchar *size,
                     gboolean verify)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) subprocess = NULL;

    subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error,
                                  "pkexec", LUC_HELPER_PATH, "write", image,
                                  device, serial, size, "--verify",
                                  verify ? "yes" : "no", NULL);
    if (subprocess == NULL) {
        g_printerr("Unable to start privileged helper: %s\n", error->message);
        return 2;
    }
    if (!g_subprocess_wait_check(subprocess, NULL, &error)) {
        g_printerr("Privileged write failed: %s\n", error->message);
        return 2;
    }
    return 0;
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

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    if (argc == 2 && (g_str_equal(argv[1], "--help") ||
                      g_str_equal(argv[1], "-h") ||
                      g_str_equal(argv[1], "--help-all"))) {
        print_help(argv[0]);
        return 0;
    }
    if (argc == 2 && g_str_equal(argv[1], "--diagnose"))
        return run_diagnostics();
    if (argc == 3 && g_str_equal(argv[1], "--sha256"))
        return run_sha256(argv[2]);
    if ((argc == 6 || argc == 7) && g_str_equal(argv[1], "--write-image")) {
        gboolean verify = argc == 6 || !g_str_equal(argv[6], "--no-verify");
        if (argc == 7 && verify) {
            g_printerr("Unknown write option: %s\n", argv[6]);
            return 2;
        }
        return run_privileged_write(argv[2], argv[3], argv[4], argv[5], verify);
    }
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
