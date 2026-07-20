/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/window.h"
#include <glib/gi18n.h>

struct _LucWindow {
    AdwApplicationWindow parent_instance;
    LucDeviceMonitor *monitor;
    GtkListBox *list;
    GtkLabel *status;
};

G_DEFINE_TYPE(LucWindow, luc_window, ADW_TYPE_APPLICATION_WINDOW)

static gchar *
format_size(guint64 size)
{
    return g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
}

static GtkWidget *
create_device_row(const LucDevice *device)
{
    g_autofree gchar *name = luc_device_get_display_name(device);
    g_autofree gchar *size = format_size(device->size);
    LucDeviceEligibility eligibility = luc_device_get_eligibility(device);
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    GtkWidget *icon;
    g_autofree gchar *subtitle = NULL;

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
    subtitle = g_strdup_printf("%s · %s · %s",
                               device->device != NULL ? device->device : "no block device",
                               size,
                               luc_device_eligibility_to_string(eligibility));
    adw_action_row_set_subtitle(row, subtitle);
    icon = gtk_image_new_from_icon_name(
        eligibility == LUC_DEVICE_ELIGIBLE ? "drive-removable-media-usb-symbolic"
                                           : "action-unavailable-symbolic");
    adw_action_row_add_prefix(row, icon);
    gtk_widget_set_sensitive(GTK_WIDGET(row), eligibility == LUC_DEVICE_ELIGIBLE);
    return GTK_WIDGET(row);
}

static void
clear_list(GtkListBox *list)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(list))) != NULL)
        gtk_list_box_remove(list, child);
}

static void
refresh_devices(LucWindow *self)
{
    g_autoptr(GPtrArray) devices = luc_device_monitor_dup_devices(self->monitor);
    guint eligible = 0;

    clear_list(self->list);
    for (guint i = 0; i < devices->len; i++) {
        LucDevice *device = g_ptr_array_index(devices, i);
        if (luc_device_get_eligibility(device) == LUC_DEVICE_ELIGIBLE)
            eligible++;
        gtk_list_box_append(self->list, create_device_row(device));
    }

    if (devices->len == 0)
        gtk_label_set_text(self->status, _("No storage devices were reported by UDisks2."));
    else {
        g_autofree gchar *message = g_strdup_printf(
            "%u device%s detected; %u eligible removable USB device%s.",
            devices->len,
            devices->len == 1 ? "" : "s",
            eligible,
            eligible == 1 ? "" : "s");
        gtk_label_set_text(self->status, message);
    }
}

static void
on_devices_changed(LucDeviceMonitor *monitor, LucWindow *self)
{
    (void)monitor;
    refresh_devices(self);
}

static void
luc_window_dispose(GObject *object)
{
    LucWindow *self = LUC_WINDOW(object);
    if (self->monitor != NULL)
        g_signal_handlers_disconnect_by_data(self->monitor, self);
    g_clear_object(&self->monitor);
    G_OBJECT_CLASS(luc_window_parent_class)->dispose(object);
}

static void
luc_window_class_init(LucWindowClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = luc_window_dispose;
}

static void
luc_window_init(LucWindow *self)
{
    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *title = gtk_label_new(_("Detected storage devices"));

    gtk_window_set_title(GTK_WINDOW(self), _("Linux USB Creator"));
    gtk_window_set_default_size(GTK_WINDOW(self), 680, 520);
    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_add_css_class(title, "title-2");
    gtk_box_append(GTK_BOX(content), title);

    self->status = GTK_LABEL(gtk_label_new(_("Scanning UDisks2…")));
    gtk_label_set_xalign(self->status, 0.0f);
    gtk_label_set_wrap(self->status, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(self->status), "dim-label");
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->status));

    self->list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(self->list, GTK_SELECTION_NONE);
    gtk_widget_add_css_class(GTK_WIDGET(self->list), "boxed-list");
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->list));

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
    adw_toolbar_view_set_content(toolbar, scroller);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self), GTK_WIDGET(toolbar));
}

GtkWidget *
luc_window_new(AdwApplication *application, LucDeviceMonitor *monitor)
{
    LucWindow *self;

    g_return_val_if_fail(ADW_IS_APPLICATION(application), NULL);
    g_return_val_if_fail(LUC_IS_DEVICE_MONITOR(monitor), NULL);
    self = g_object_new(LUC_TYPE_WINDOW, "application", application, NULL);
    self->monitor = g_object_ref(monitor);
    g_signal_connect(monitor, "devices-changed", G_CALLBACK(on_devices_changed), self);
    refresh_devices(self);
    return GTK_WIDGET(self);
}
