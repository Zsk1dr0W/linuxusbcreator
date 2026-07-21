/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/window.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

#include "app/write-operation.h"
#include "core/helper-protocol.h"
#include "core/image-classifier.h"
#include "core/linux-target-plan.h"
#include "core/windows-image.h"
#include "core/windows-target-plan.h"

#define WINDOWS_FILESYSTEM_OVERHEAD (256U * 1024U * 1024U)

struct _LucWindow {
    AdwApplicationWindow parent_instance;
    LucDeviceMonitor *monitor;
    GtkListBox *list;
    GtkLabel *device_status;
    AdwActionRow *image_row;
    GtkButton *image_button;
    GtkSwitch *verify_switch;
    GtkWidget *verify_row;
    AdwComboRow *firmware_row;
    AdwComboRow *linux_mode_row;
    GtkWidget *persistence_row;
    GtkSwitch *persistence_switch;
    GtkButton *write_button;
    GtkButton *cancel_button;
    GtkProgressBar *progress;
    GtkLabel *operation_status;
    gchar *image_path;
    guint64 image_size;
    LucWindowsImageInfo *windows_info;
    LucImageClassification *classification;
    LucDevice *selected_device;
    LucWriteOperation *operation;
    gboolean operation_active;
    gboolean operation_verify;
    gboolean operation_windows;
    gboolean operation_linux_iso;
    gboolean operation_persistence;
    gboolean refreshing;
};

G_DEFINE_TYPE(LucWindow, luc_window, ADW_TYPE_APPLICATION_WINDOW)

static const gchar *
confirmation_text(void)
{
    return _("ERASE");
}

static LucWindowsFirmware
selected_windows_firmware(LucWindow *self)
{
    return adw_combo_row_get_selected(self->firmware_row) == 1
               ? LUC_WINDOWS_FIRMWARE_BIOS
               : LUC_WINDOWS_FIRMWARE_UEFI;
}

static gboolean
selected_linux_iso_mode(LucWindow *self)
{
    return self->classification != NULL &&
           self->classification->linux_profile ==
               LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI &&
           adw_combo_row_get_selected(self->linux_mode_row) == 1;
}

static gchar *
format_size(guint64 size)
{
    return g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
}

static const gchar *
eligibility_description(const LucDevice *device)
{
    if (luc_device_is_write_candidate(device)) {
        if (device->mounted)
            return _("Available; its partitions will be unmounted before writing");
        return _("Available for writing");
    }
    if (device->serial == NULL || device->serial[0] == '\0')
        return _("Blocked: the device does not report a stable serial number");
    switch (luc_device_get_eligibility(device)) {
    case LUC_DEVICE_REJECT_NO_MEDIA:
        return _("Blocked: no media is available");
    case LUC_DEVICE_REJECT_NO_BLOCK_DEVICE:
        return _("Blocked: no block device is available");
    case LUC_DEVICE_REJECT_NOT_USB:
        return _("Blocked: this is not a USB device");
    case LUC_DEVICE_REJECT_NOT_REMOVABLE:
        return _("Blocked: the device is not removable");
    case LUC_DEVICE_REJECT_SYSTEM:
        return _("Blocked: contains the current operating system");
    case LUC_DEVICE_REJECT_ACTIVE_SWAP:
        return _("Blocked: contains active swap");
    case LUC_DEVICE_REJECT_READ_ONLY:
        return _("Blocked: the device is read-only");
    case LUC_DEVICE_REJECT_UNSUPPORTED:
        return _("Blocked: unsupported device type");
    case LUC_DEVICE_REJECT_MOUNTED:
    case LUC_DEVICE_ELIGIBLE:
    default:
        return _("Blocked by the safety policy");
    }
}

static GtkWidget *
create_device_row(const LucDevice *device)
{
    g_autofree gchar *name = luc_device_get_display_name(device);
    g_autofree gchar *size = format_size(device->size);
    g_autofree gchar *subtitle = NULL;
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    GtkWidget *icon;
    gboolean candidate = luc_device_is_write_candidate(device);

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
    subtitle = g_strdup_printf("%s · %s\n%s",
                               device->device != NULL ? device->device : _("no block node"),
                               size,
                               eligibility_description(device));
    adw_action_row_set_subtitle(row, subtitle);
    adw_action_row_set_subtitle_lines(row, 2);
    icon = gtk_image_new_from_icon_name(
        candidate ? "drive-removable-media-usb-symbolic"
                  : "action-unavailable-symbolic");
    adw_action_row_add_prefix(row, icon);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), candidate);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), candidate);
    gtk_widget_set_sensitive(GTK_WIDGET(row), candidate);
    g_object_set_data_full(G_OBJECT(row), "luc-device", luc_device_copy(device),
                           (GDestroyNotify)luc_device_free);
    return GTK_WIDGET(row);
}

static void
set_operation_status(LucWindow *self, const gchar *message, const gchar *style)
{
    gtk_label_set_text(self->operation_status, message);
    gtk_widget_remove_css_class(GTK_WIDGET(self->operation_status), "error");
    gtk_widget_remove_css_class(GTK_WIDGET(self->operation_status), "success");
    gtk_widget_remove_css_class(GTK_WIDGET(self->operation_status), "warning");
    if (style != NULL)
        gtk_widget_add_css_class(GTK_WIDGET(self->operation_status), style);
}

static void
update_actions(LucWindow *self)
{
    guint64 required_size = self->image_size;

    if (self->windows_info != NULL &&
        self->windows_info->content_size <=
            G_MAXUINT64 - WINDOWS_FILESYSTEM_OVERHEAD)
        required_size = self->windows_info->content_size +
                        WINDOWS_FILESYSTEM_OVERHEAD;
    else if (selected_linux_iso_mode(self) &&
             self->image_size <= G_MAXUINT64 - LUC_LINUX_BOOT_SIZE_BYTES -
                                     LUC_LINUX_FILESYSTEM_OVERHEAD)
        required_size = self->image_size + LUC_LINUX_BOOT_SIZE_BYTES +
                        LUC_LINUX_FILESYSTEM_OVERHEAD;
    gboolean ready = !self->operation_active &&
                     self->image_path != NULL &&
                     self->selected_device != NULL &&
                     self->image_size > 0 &&
                     required_size <= self->selected_device->size;

    gtk_widget_set_sensitive(GTK_WIDGET(self->write_button), ready);
    gtk_widget_set_sensitive(GTK_WIDGET(self->image_button), !self->operation_active);
    gtk_widget_set_sensitive(GTK_WIDGET(self->verify_switch), !self->operation_active);
    gtk_widget_set_sensitive(GTK_WIDGET(self->firmware_row), !self->operation_active);
    gtk_widget_set_sensitive(GTK_WIDGET(self->linux_mode_row), !self->operation_active);
    gtk_widget_set_sensitive(self->persistence_row, !self->operation_active);
    gtk_widget_set_sensitive(GTK_WIDGET(self->list), !self->operation_active);
    if (!self->operation_active && self->image_path != NULL &&
        self->selected_device != NULL &&
        required_size > self->selected_device->size) {
        set_operation_status(self,
                             _("The image is larger than the selected device."),
                             "error");
    }
}

static void
on_linux_mode_changed(AdwComboRow *row, GParamSpec *spec, LucWindow *self)
{
    gboolean iso_mode = selected_linux_iso_mode(self);

    (void)row;
    (void)spec;
    gtk_widget_set_visible(self->persistence_row, iso_mode);
    gtk_widget_set_visible(self->verify_row, !iso_mode);
    if (iso_mode)
        set_operation_status(
            self,
            _("Fedora ISO mode uses UEFI/GPT, bundled signed GRUB and mandatory file verification."),
            NULL);
    else if (self->classification != NULL &&
             self->classification->kind == LUC_IMAGE_KIND_LINUX_ISO)
        set_operation_status(
            self,
            _("Hybrid/raw mode preserves the image layout and supports BIOS and UEFI when provided by the ISO."),
            NULL);
    update_actions(self);
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
    g_autoptr(GPtrArray) devices = NULL;
    g_autofree gchar *selected_path = NULL;
    GtkListBoxRow *row_to_select = NULL;
    guint candidates = 0;

    if (self->operation_active)
        return;
    if (self->selected_device != NULL)
        selected_path = g_strdup(self->selected_device->device);
    devices = luc_device_monitor_dup_devices(self->monitor);
    self->refreshing = TRUE;
    clear_list(self->list);
    g_clear_pointer(&self->selected_device, luc_device_free);
    for (guint i = 0; i < devices->len; i++) {
        LucDevice *device = g_ptr_array_index(devices, i);
        GtkWidget *row = create_device_row(device);

        if (luc_device_is_write_candidate(device))
            candidates++;
        gtk_list_box_append(self->list, row);
        if (selected_path != NULL && g_strcmp0(device->device, selected_path) == 0 &&
            luc_device_is_write_candidate(device))
            row_to_select = GTK_LIST_BOX_ROW(row);
    }
    if (row_to_select != NULL) {
        LucDevice *device = g_object_get_data(G_OBJECT(row_to_select), "luc-device");
        self->selected_device = luc_device_copy(device);
        gtk_list_box_select_row(self->list, row_to_select);
    }
    self->refreshing = FALSE;

    if (devices->len == 0) {
        gtk_label_set_text(self->device_status,
                           _("UDisks2 did not report any storage devices."));
    } else {
        g_autofree gchar *message = g_strdup_printf(
            ngettext("%u device detected; %u USB available for writing.",
                     "%u devices detected; %u USB devices available for writing.",
                     devices->len),
            devices->len, candidates);
        gtk_label_set_text(self->device_status, message);
    }
    update_actions(self);
}

static void
on_devices_changed(LucDeviceMonitor *monitor, LucWindow *self)
{
    (void)monitor;
    refresh_devices(self);
}

static void
on_device_selected(GtkListBox *list, GtkListBoxRow *row, LucWindow *self)
{
    LucDevice *device;

    (void)list;
    if (self->refreshing || self->operation_active)
        return;
    g_clear_pointer(&self->selected_device, luc_device_free);
    if (row != NULL) {
        device = g_object_get_data(G_OBJECT(row), "luc-device");
        if (luc_device_is_write_candidate(device))
            self->selected_device = luc_device_copy(device);
    }
    update_actions(self);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void
on_file_selected(GtkNativeDialog *dialog, gint response, LucWindow *self)
{
    g_autoptr(GFile) file = NULL;
    g_autoptr(GFileInfo) info = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *size = NULL;
    g_autofree gchar *subtitle = NULL;
    g_autoptr(LucWindowsImageInfo) windows_info = NULL;
    g_autoptr(LucImageClassification) classification = NULL;

    if (response != GTK_RESPONSE_ACCEPT)
        goto done;
    file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
    path = g_file_get_path(file);
    info = g_file_query_info(file,
                             G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                             G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                             G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                             G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (path == NULL || info == NULL ||
        g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR) {
        set_operation_status(self,
                             error != NULL ? error->message
                                           : _("Select a regular local image file."),
                             "error");
        goto done;
    }

    classification = luc_image_classify(path, NULL, NULL);
    if (classification != NULL &&
        classification->kind == LUC_IMAGE_KIND_WINDOWS_ISO)
        windows_info = luc_windows_image_inspect(path, NULL, NULL);
    if (classification != NULL &&
        classification->kind == LUC_IMAGE_KIND_WINDOWS_ISO &&
        windows_info == NULL) {
        set_operation_status(self,
                             _("The Windows image could not be inspected safely."),
                             "error");
        goto done;
    }
    if (windows_info != NULL &&
        (windows_info->has_boot_wim ||
         windows_info->install_payload != LUC_WINDOWS_INSTALL_PAYLOAD_NONE) &&
        (!windows_info->is_windows_installer ||
         !windows_info->fat32_compatible ||
         (!windows_info->supports_uefi_x64 &&
          !windows_info->supports_uefi_arm64))) {
        set_operation_status(
            self,
            _("This Windows image is not compatible with the supported FAT32 profiles."),
            "error");
        goto done;
    }

    g_free(self->image_path);
    self->image_path = g_steal_pointer(&path);
    self->image_size = g_file_info_get_size(info);
    g_clear_pointer(&self->windows_info, luc_windows_image_info_free);
    g_clear_pointer(&self->classification, luc_image_classification_free);
    self->classification = g_steal_pointer(&classification);
    if (windows_info != NULL && windows_info->is_windows_installer)
        self->windows_info = g_steal_pointer(&windows_info);
    size = format_size(self->image_size);
    if (self->windows_info != NULL) {
        const gchar *architecture = self->windows_info->supports_uefi_arm64
                                        ? "ARM64" : "x64";
        subtitle = g_strdup_printf(
            self->windows_info->requires_wim_split
                ? _("%s · %s · Windows %s · install.wim will be split")
                : _("%s · %s · Windows %s"),
            g_file_info_get_display_name(info), size, architecture);
        gtk_widget_set_visible(self->verify_row, FALSE);
        gtk_widget_set_visible(GTK_WIDGET(self->linux_mode_row), FALSE);
        gtk_widget_set_visible(self->persistence_row, FALSE);
        {
            const gchar *profiles_both[] = {_("UEFI · GPT"), _("BIOS · MBR"), NULL};
            const gchar *profiles_uefi[] = {_("UEFI · GPT"), NULL};
            GtkStringList *profiles = gtk_string_list_new(
                self->windows_info->supports_bios ? profiles_both : profiles_uefi);

            adw_combo_row_set_model(self->firmware_row, G_LIST_MODEL(profiles));
            adw_combo_row_set_selected(self->firmware_row, 0);
            g_object_unref(profiles);
            gtk_widget_set_visible(GTK_WIDGET(self->firmware_row), TRUE);
        }
        set_operation_status(
            self,
            _("Windows installer detected. Choose UEFI/GPT or BIOS/MBR; files will be verified."),
            NULL);
    } else if (self->classification != NULL &&
               self->classification->kind == LUC_IMAGE_KIND_LINUX_ISO) {
        subtitle = g_strdup_printf(_("%s · %s · %s · %s"),
                                   g_file_info_get_display_name(info), size,
                                   self->classification->distribution,
                                   self->classification->architecture);
        gtk_widget_set_visible(self->verify_row, TRUE);
        gtk_widget_set_visible(GTK_WIDGET(self->firmware_row), FALSE);
        if (self->classification->linux_profile ==
            LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI) {
            const gchar *modes[] = {
                _("Hybrid/raw · BIOS and UEFI"),
                _("ISO · Fedora Live UEFI"),
                NULL,
            };
            GtkStringList *model = gtk_string_list_new(modes);
            adw_combo_row_set_model(self->linux_mode_row, G_LIST_MODEL(model));
            adw_combo_row_set_selected(self->linux_mode_row, 0);
            g_object_unref(model);
            gtk_widget_set_visible(GTK_WIDGET(self->linux_mode_row), TRUE);
        } else {
            gtk_widget_set_visible(GTK_WIDGET(self->linux_mode_row), FALSE);
        }
        gtk_widget_set_visible(self->persistence_row, FALSE);
        set_operation_status(
            self,
            _("Linux image detected. Hybrid/raw writing with full verification will be used."),
            NULL);
    } else {
        subtitle = g_strdup_printf(_("%s · %s · Raw or unrecognized image"),
                                   g_file_info_get_display_name(info), size);
        gtk_widget_set_visible(self->verify_row, TRUE);
        gtk_widget_set_visible(GTK_WIDGET(self->firmware_row), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(self->linux_mode_row), FALSE);
        gtk_widget_set_visible(self->persistence_row, FALSE);
        set_operation_status(self,
                             _("Select a USB device and review verification before continuing."),
                             NULL);
    }
    adw_action_row_set_subtitle(self->image_row, subtitle);
    update_actions(self);

done:
    gtk_native_dialog_destroy(dialog);
    g_object_unref(dialog);
}

static void
choose_image(LucWindow *self)
{
    GtkFileChooserNative *chooser;
    GtkFileFilter *images;
    GtkFileFilter *all_files;

    chooser = gtk_file_chooser_native_new(_("Select disk image"),
                                          GTK_WINDOW(self),
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          _("Select"), _("Cancel"));
    images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, _("Disk images (*.iso, *.img, *.raw)"));
    gtk_file_filter_add_pattern(images, "*.iso");
    gtk_file_filter_add_pattern(images, "*.ISO");
    gtk_file_filter_add_pattern(images, "*.img");
    gtk_file_filter_add_pattern(images, "*.IMG");
    gtk_file_filter_add_pattern(images, "*.raw");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), images);
    all_files = gtk_file_filter_new();
    gtk_file_filter_set_name(all_files, _("All files"));
    gtk_file_filter_add_pattern(all_files, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all_files);
    g_signal_connect(chooser, "response", G_CALLBACK(on_file_selected), self);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

G_GNUC_END_IGNORE_DEPRECATIONS

static void
on_choose_image(GtkButton *button, LucWindow *self)
{
    (void)button;
    choose_image(self);
}

static void
stop_pulsing(LucWindow *self)
{
    (void)self;
}

static void
on_operation_phase(LucWriteOperation *operation, gint phase, LucWindow *self)
{
    (void)operation;
    switch ((LucHelperPhase)phase) {
    case LUC_HELPER_PHASE_HASHING:
        set_operation_status(self, _("Computing the image SHA-256…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        stop_pulsing(self);
        break;
    case LUC_HELPER_PHASE_VALIDATING:
        set_operation_status(self, _("Validating the Windows installer…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        stop_pulsing(self);
        break;
    case LUC_HELPER_PHASE_INSPECTING:
        set_operation_status(self, _("Inspecting the selected image…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        stop_pulsing(self);
        break;
    case LUC_HELPER_PHASE_PARTITIONING:
        set_operation_status(self,
            selected_windows_firmware(self) == LUC_WINDOWS_FIRMWARE_BIOS
                ? _("Creating the MBR partition table…")
                : _("Creating the GPT partition table…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        break;
    case LUC_HELPER_PHASE_FORMATTING:
        set_operation_status(self, _("Formatting the Windows partition as FAT32…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        break;
    case LUC_HELPER_PHASE_MOUNTING:
        set_operation_status(self, _("Mounting the new Windows target…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        break;
    case LUC_HELPER_PHASE_COPYING:
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        set_operation_status(
            self,
            self->operation_linux_iso
                ? _("Copying Fedora boot and LiveOS files…")
                : _("Copying Windows installation files…"), NULL);
        stop_pulsing(self);
        break;
    case LUC_HELPER_PHASE_CONFIGURING:
        set_operation_status(
            self,
            self->operation_persistence
                ? _("Configuring GRUB and persistent OverlayFS…")
                : _("Configuring GRUB for the extracted Fedora media…"),
            NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        break;
    case LUC_HELPER_PHASE_SPLITTING:
        set_operation_status(self, _("Splitting install.wim for FAT32…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        break;
    case LUC_HELPER_PHASE_WRITING:
        stop_pulsing(self);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        set_operation_status(self, _("Writing the image to the USB device…"), NULL);
        break;
    case LUC_HELPER_PHASE_SYNCING:
        set_operation_status(
            self,
            self->operation_linux_iso
                ? _("Synchronizing Fedora media with the device…")
                : _("Synchronizing data with the device…"), NULL);
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        stop_pulsing(self);
        break;
    case LUC_HELPER_PHASE_VERIFYING:
        gtk_progress_bar_set_fraction(self->progress, 0.0);
        set_operation_status(
            self,
            self->operation_linux_iso
                ? _("Verifying every extracted Fedora file…")
                : _("Verifying the image by reading it back…"), NULL);
        break;
    case LUC_HELPER_PHASE_NONE:
    default:
        break;
    }
}

static void
on_operation_progress(LucWriteOperation *operation,
                      guint64 completed,
                      guint64 total,
                      LucWindow *self)
{
    g_autofree gchar *done_size = NULL;
    g_autofree gchar *total_size = NULL;
    g_autofree gchar *text = NULL;
    gdouble fraction;

    (void)operation;
    if (total == 0)
        return;
    stop_pulsing(self);
    fraction = (gdouble)completed / (gdouble)total;
    gtk_progress_bar_set_fraction(self->progress, fraction);
    if (total == 1) {
        text = g_strdup_printf("%.0f%%", fraction * 100.0);
    } else {
        done_size = format_size(completed);
        total_size = format_size(total);
        text = g_strdup_printf("%s / %s · %.0f%%", done_size, total_size,
                               fraction * 100.0);
    }
    gtk_progress_bar_set_text(self->progress, text);
}

static void
on_operation_finished(LucWriteOperation *operation,
                      gboolean success,
                      gboolean cancelled,
                      const gchar *diagnostics,
                      LucWindow *self)
{
    (void)operation;
    stop_pulsing(self);
    self->operation_active = FALSE;
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->write_button), TRUE);
    if (success) {
        gtk_progress_bar_set_fraction(self->progress, 1.0);
        if (self->operation_windows) {
            gtk_progress_bar_set_text(
                self->progress,
                _("Windows media completed, verified and safely unmounted"));
            set_operation_status(
                self,
                selected_windows_firmware(self) == LUC_WINDOWS_FIRMWARE_BIOS
                    ? _("The BIOS/MBR Windows installation media was created successfully. The USB is safely unmounted and can be disconnected.")
                    : _("The UEFI/GPT Windows installation media was created successfully. The USB is safely unmounted and can be disconnected. Reconnect it to browse its files."),
                "success");
        } else if (self->operation_linux_iso) {
            gtk_progress_bar_set_text(
                self->progress,
                self->operation_persistence
                    ? _("Fedora media completed with persistence")
                    : _("Fedora media completed and verified"));
            set_operation_status(
                self,
                self->operation_persistence
                    ? _("Fedora Live UEFI media with persistence was created, verified and safely unmounted.")
                    : _("Fedora Live UEFI media was created, verified and safely unmounted."),
                "success");
        } else if (self->operation_verify) {
            gtk_progress_bar_set_text(self->progress, _("Completed and verified"));
            set_operation_status(self,
                                 _("The image was written and verified successfully."),
                                 "success");
        } else {
            gtk_progress_bar_set_text(self->progress, _("Completed"));
            set_operation_status(self,
                                 _("The image was written without read-back verification."),
                                 "warning");
        }
    } else if (cancelled) {
        gtk_progress_bar_set_text(self->progress, _("Cancelled"));
        set_operation_status(self, _("The operation was cancelled."), "warning");
    } else {
        gtk_progress_bar_set_text(self->progress, _("Failed"));
        set_operation_status(self,
                             diagnostics != NULL && diagnostics[0] != '\0'
                                 ? diagnostics
                                 : _("Writing failed without additional information."),
                             "error");
    }
    g_clear_object(&self->operation);
    refresh_devices(self);
    update_actions(self);
}

static void
start_write(LucWindow *self)
{
    gboolean verify = gtk_switch_get_active(self->verify_switch);

    self->operation_verify = verify;
    self->operation_windows = self->windows_info != NULL;
    self->operation_linux_iso = selected_linux_iso_mode(self);
    self->operation_persistence = self->operation_linux_iso &&
                                  gtk_switch_get_active(self->persistence_switch);
    if (self->operation_windows)
        self->operation = luc_write_operation_new_windows(
            self->image_path, self->selected_device->device,
            self->selected_device->serial, self->selected_device->size,
            selected_windows_firmware(self));
    else if (self->operation_linux_iso)
        self->operation = luc_write_operation_new_linux_iso(
            self->image_path, self->selected_device->device,
            self->selected_device->serial, self->selected_device->size,
            self->operation_persistence);
    else
        self->operation = luc_write_operation_new(
            self->image_path, self->selected_device->device,
            self->selected_device->serial, self->selected_device->size, verify);
    g_signal_connect(self->operation, "phase-changed",
                     G_CALLBACK(on_operation_phase), self);
    g_signal_connect(self->operation, "progress",
                     G_CALLBACK(on_operation_progress), self);
    g_signal_connect(self->operation, "finished",
                     G_CALLBACK(on_operation_finished), self);
    self->operation_active = TRUE;
    gtk_progress_bar_set_fraction(self->progress, 0.0);
    gtk_progress_bar_set_text(self->progress, _("Waiting for authorization…"));
    set_operation_status(self,
                         _("Authorize the limited Linux USB Creator helper."),
                         NULL);
    gtk_widget_set_visible(GTK_WIDGET(self->write_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), TRUE);
    update_actions(self);
    luc_write_operation_start(self->operation);
}

static void
on_confirmation_changed(GtkEditable *editable, GtkWidget *confirm_button)
{
    gtk_widget_set_sensitive(confirm_button,
                             g_str_equal(gtk_editable_get_text(editable),
                                         confirmation_text()));
}

static void
on_confirmation_cancel(GtkButton *button, GtkWindow *dialog)
{
    (void)button;
    gtk_window_destroy(dialog);
}

static void
on_confirmation_accept(GtkButton *button, LucWindow *self)
{
    GtkWindow *dialog = g_object_get_data(G_OBJECT(button), "confirmation-window");

    gtk_window_destroy(dialog);
    start_write(self);
}

static void
show_confirmation(LucWindow *self)
{
    GtkWindow *dialog = GTK_WINDOW(gtk_window_new());
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *title = gtk_label_new(_("This operation will destroy all data on the USB device"));
    GtkWidget *details;
    GtkWidget *instruction;
    GtkWidget *entry = gtk_entry_new();
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *cancel;
    GtkWidget *confirm;
    g_autofree gchar *device_name = luc_device_get_display_name(self->selected_device);
    g_autofree gchar *device_size = format_size(self->selected_device->size);
    g_autofree gchar *image_name = g_path_get_basename(self->image_path);
    g_autofree gchar *detail_text = NULL;

    gtk_window_set_title(dialog, _("Confirm destructive write"));
    gtk_window_set_transient_for(dialog, GTK_WINDOW(self));
    gtk_window_set_modal(dialog, TRUE);
    gtk_window_set_default_size(dialog, 520, -1);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 18);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_box_set_spacing(GTK_BOX(content), 12);
    gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_wrap(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(content), title);
    if (self->windows_info != NULL)
        detail_text = g_strdup_printf(
            selected_windows_firmware(self) == LUC_WINDOWS_FIRMWARE_BIOS
                ? _("Target: %s (%s, %s)\nImage: %s\nMode: Windows BIOS/MBR · FAT32")
                : _("Target: %s (%s, %s)\nImage: %s\nMode: Windows UEFI/GPT · FAT32"),
            device_name, self->selected_device->device, device_size, image_name);
    else if (selected_linux_iso_mode(self))
        detail_text = g_strdup_printf(
            gtk_switch_get_active(self->persistence_switch)
                ? _("Target: %s (%s, %s)\nImage: %s\nMode: Fedora Live UEFI/GPT · persistent")
                : _("Target: %s (%s, %s)\nImage: %s\nMode: Fedora Live UEFI/GPT"),
            device_name, self->selected_device->device, device_size, image_name);
    else
        detail_text = g_strdup_printf(
            _("Target: %s (%s, %s)\nImage: %s"), device_name,
            self->selected_device->device, device_size, image_name);
    details = gtk_label_new(detail_text);
    gtk_label_set_wrap(GTK_LABEL(details), TRUE);
    gtk_label_set_xalign(GTK_LABEL(details), 0.0f);
    gtk_box_append(GTK_BOX(content), details);
    instruction = gtk_label_new(_("Type ERASE to confirm:"));
    gtk_label_set_xalign(GTK_LABEL(instruction), 0.0f);
    gtk_box_append(GTK_BOX(content), instruction);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), confirmation_text());
    gtk_box_append(GTK_BOX(content), entry);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    cancel = gtk_button_new_with_label(_("Cancel"));
    confirm = gtk_button_new_with_label(_("Erase and write"));
    gtk_widget_add_css_class(confirm, "destructive-action");
    gtk_widget_set_sensitive(confirm, FALSE);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), confirm);
    gtk_box_append(GTK_BOX(content), buttons);
    gtk_window_set_child(dialog, content);
    g_object_set_data(G_OBJECT(confirm), "confirmation-window", dialog);
    g_signal_connect(entry, "changed", G_CALLBACK(on_confirmation_changed), confirm);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_confirmation_cancel), dialog);
    g_signal_connect(confirm, "clicked", G_CALLBACK(on_confirmation_accept), self);
    gtk_window_present(dialog);
}

static void
on_write_clicked(GtkButton *button, LucWindow *self)
{
    GStatBuf metadata;
    guint64 required_size;

    (void)button;
    if (self->image_path == NULL || self->selected_device == NULL ||
        !luc_device_is_write_candidate(self->selected_device))
        return;
    if (g_stat(self->image_path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_size <= 0) {
        set_operation_status(self,
                             _("The image no longer exists or is no longer a regular file."),
                             "error");
        return;
    }
    required_size = self->windows_info != NULL
                        ? self->windows_info->content_size +
                              WINDOWS_FILESYSTEM_OVERHEAD
                        : (guint64)metadata.st_size;
    if (selected_linux_iso_mode(self) &&
        required_size <= G_MAXUINT64 - LUC_LINUX_BOOT_SIZE_BYTES -
                             LUC_LINUX_FILESYSTEM_OVERHEAD)
        required_size += LUC_LINUX_BOOT_SIZE_BYTES +
                         LUC_LINUX_FILESYSTEM_OVERHEAD;
    if (required_size > self->selected_device->size) {
        set_operation_status(self,
                             _("The image is larger than the selected device."),
                             "error");
        return;
    }
    self->image_size = (guint64)metadata.st_size;
    show_confirmation(self);
}

static void
on_cancel_clicked(GtkButton *button, LucWindow *self)
{
    (void)button;
    if (self->operation != NULL) {
        luc_write_operation_cancel(self->operation);
        gtk_widget_set_sensitive(GTK_WIDGET(self->cancel_button), FALSE);
        set_operation_status(self, _("Cancelling safely…"), "warning");
    }
}

static gboolean
on_close_request(GtkWindow *window, LucWindow *self)
{
    (void)window;
    if (!self->operation_active)
        return FALSE;
    on_cancel_clicked(NULL, self);
    return TRUE;
}

static void
luc_window_dispose(GObject *object)
{
    LucWindow *self = LUC_WINDOW(object);

    stop_pulsing(self);
    if (self->monitor != NULL)
        g_signal_handlers_disconnect_by_data(self->monitor, self);
    if (self->operation != NULL) {
        g_signal_handlers_disconnect_by_data(self->operation, self);
        luc_write_operation_cancel(self->operation);
    }
    g_clear_object(&self->operation);
    g_clear_object(&self->monitor);
    G_OBJECT_CLASS(luc_window_parent_class)->dispose(object);
}

static void
luc_window_finalize(GObject *object)
{
    LucWindow *self = LUC_WINDOW(object);

    g_clear_pointer(&self->image_path, g_free);
    g_clear_pointer(&self->windows_info, luc_windows_image_info_free);
    g_clear_pointer(&self->classification, luc_image_classification_free);
    g_clear_pointer(&self->selected_device, luc_device_free);
    G_OBJECT_CLASS(luc_window_parent_class)->finalize(object);
}

static void
luc_window_class_init(LucWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = luc_window_dispose;
    object_class->finalize = luc_window_finalize;
}

static GtkWidget *
section_title(const gchar *text)
{
    GtkWidget *label = gtk_label_new(text);

    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "title-3");
    return label;
}

static void
luc_window_init(LucWindow *self)
{
    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *image_list = gtk_list_box_new();
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *safety;

    gtk_window_set_title(GTK_WINDOW(self), _("Linux USB Creator"));
    gtk_window_set_default_size(GTK_WINDOW(self), 760, 720);
    g_signal_connect(self, "close-request", G_CALLBACK(on_close_request), self);
    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_box_append(GTK_BOX(content), section_title(_("1. Select an image")));

    gtk_list_box_set_selection_mode(GTK_LIST_BOX(image_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(image_list, "boxed-list");
    self->image_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->image_row),
                                  _("ISO/raw image"));
    adw_action_row_set_subtitle(self->image_row, _("No image selected"));
    self->image_button = GTK_BUTTON(gtk_button_new_with_label(_("Choose…")));
    gtk_widget_set_valign(GTK_WIDGET(self->image_button), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(self->image_row, GTK_WIDGET(self->image_button));
    gtk_list_box_append(GTK_LIST_BOX(image_list), GTK_WIDGET(self->image_row));
    self->verify_row = GTK_WIDGET(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->verify_row),
                                  _("Full verification"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->verify_row),
                                _("Read the written image back and compare SHA-256"));
    self->verify_switch = GTK_SWITCH(gtk_switch_new());
    gtk_switch_set_active(self->verify_switch, TRUE);
    gtk_widget_set_valign(GTK_WIDGET(self->verify_switch), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(self->verify_row),
                              GTK_WIDGET(self->verify_switch));
    gtk_list_box_append(GTK_LIST_BOX(image_list), self->verify_row);
    self->firmware_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->firmware_row),
                                  _("Windows partition scheme"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->firmware_row),
                                _("Choose the firmware used by the target computer"));
    gtk_widget_set_visible(GTK_WIDGET(self->firmware_row), FALSE);
    gtk_list_box_append(GTK_LIST_BOX(image_list), GTK_WIDGET(self->firmware_row));
    self->linux_mode_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->linux_mode_row),
                                  _("Linux writing mode"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->linux_mode_row),
                                _("Choose raw compatibility or extracted Fedora UEFI media"));
    gtk_widget_set_visible(GTK_WIDGET(self->linux_mode_row), FALSE);
    gtk_list_box_append(GTK_LIST_BOX(image_list), GTK_WIDGET(self->linux_mode_row));
    g_signal_connect(self->linux_mode_row, "notify::selected",
                     G_CALLBACK(on_linux_mode_changed), self);
    self->persistence_row = GTK_WIDGET(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->persistence_row),
                                  _("Persistent Fedora session"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->persistence_row),
                                _("Keep Live session changes in the ext4 data partition"));
    self->persistence_switch = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_valign(GTK_WIDGET(self->persistence_switch), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(self->persistence_row),
                              GTK_WIDGET(self->persistence_switch));
    gtk_widget_set_visible(self->persistence_row, FALSE);
    gtk_list_box_append(GTK_LIST_BOX(image_list), self->persistence_row);
    gtk_box_append(GTK_BOX(content), image_list);
    g_signal_connect(self->image_button, "clicked", G_CALLBACK(on_choose_image), self);

    gtk_box_append(GTK_BOX(content), section_title(_("2. Select the target USB device")));
    self->device_status = GTK_LABEL(gtk_label_new(_("Scanning UDisks2…")));
    gtk_label_set_xalign(self->device_status, 0.0f);
    gtk_label_set_wrap(self->device_status, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(self->device_status), "dim-label");
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->device_status));
    self->list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(self->list, GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(GTK_WIDGET(self->list), "boxed-list");
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->list));
    g_signal_connect(self->list, "row-selected", G_CALLBACK(on_device_selected), self);

    gtk_box_append(GTK_BOX(content), section_title(_("3. Confirm and write")));
    safety = gtk_label_new(_("The system SSD and ineligible devices remain blocked. Writing requires explicit confirmation and Polkit authorization."));
    gtk_label_set_xalign(GTK_LABEL(safety), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(safety), TRUE);
    gtk_widget_add_css_class(safety, "dim-label");
    gtk_box_append(GTK_BOX(content), safety);
    self->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(self->progress, TRUE);
    gtk_progress_bar_set_text(self->progress, _("Ready"));
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->progress));
    self->operation_status = GTK_LABEL(gtk_label_new(
        _("Choose an image and an available USB device.")));
    gtk_label_set_xalign(self->operation_status, 0.0f);
    gtk_label_set_wrap(self->operation_status, TRUE);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->operation_status));
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    self->cancel_button = GTK_BUTTON(gtk_button_new_with_label(_("Cancel operation")));
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), FALSE);
    self->write_button = GTK_BUTTON(gtk_button_new_with_label(_("Write image…")));
    gtk_widget_add_css_class(GTK_WIDGET(self->write_button), "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(self->write_button), FALSE);
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(self->cancel_button));
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(self->write_button));
    gtk_box_append(GTK_BOX(content), actions);
    g_signal_connect(self->write_button, "clicked", G_CALLBACK(on_write_clicked), self);
    g_signal_connect(self->cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), self);

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
