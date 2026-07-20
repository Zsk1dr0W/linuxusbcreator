/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "linux/device-monitor.h"

#include <string.h>

#define UDISKS_NAME "org.freedesktop.UDisks2"
#define UDISKS_PATH "/org/freedesktop/UDisks2"
#define UDISKS_DRIVE "org.freedesktop.UDisks2.Drive"
#define UDISKS_BLOCK "org.freedesktop.UDisks2.Block"
#define UDISKS_FILESYSTEM "org.freedesktop.UDisks2.Filesystem"
#define UDISKS_SWAPSPACE "org.freedesktop.UDisks2.Swapspace"

struct _LucDeviceMonitor {
    GObject parent_instance;
    GDBusObjectManager *manager;
};

enum {
    DEVICES_CHANGED,
    N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE(LucDeviceMonitor, luc_device_monitor, G_TYPE_OBJECT)

static GVariant *
proxy_property(GDBusInterface *interface, const gchar *name)
{
    if (!G_IS_DBUS_PROXY(interface))
        return NULL;
    return g_dbus_proxy_get_cached_property(G_DBUS_PROXY(interface), name);
}

static gchar *
property_string(GDBusInterface *interface, const gchar *name)
{
    g_autoptr(GVariant) value = proxy_property(interface, name);
    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
        return NULL;
    return g_variant_dup_string(value, NULL);
}

static gchar *
property_object_path(GDBusInterface *interface, const gchar *name)
{
    g_autoptr(GVariant) value = proxy_property(interface, name);
    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH))
        return NULL;
    return g_variant_dup_string(value, NULL);
}

static gboolean
property_boolean(GDBusInterface *interface, const gchar *name, gboolean fallback)
{
    g_autoptr(GVariant) value = proxy_property(interface, name);
    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
        return fallback;
    return g_variant_get_boolean(value);
}

static guint64
property_uint64(GDBusInterface *interface, const gchar *name)
{
    g_autoptr(GVariant) value = proxy_property(interface, name);
    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64))
        return 0;
    return g_variant_get_uint64(value);
}

static gchar *
property_bytestring(GDBusInterface *interface, const gchar *name)
{
    g_autoptr(GVariant) value = proxy_property(interface, name);
    gsize length = 0;
    const gchar *bytes;

    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING))
        return NULL;
    bytes = g_variant_get_fixed_array(value, &length, sizeof(guchar));
    if (bytes == NULL || length == 0)
        return NULL;
    return g_strndup(bytes, length > 0 && bytes[length - 1] == '\0' ? length - 1 : length);
}

static gboolean
filesystem_is_mounted(GDBusInterface *interface, gboolean *is_root)
{
    g_autoptr(GVariant) value = proxy_property(interface, "MountPoints");
    GVariantIter iter;
    GVariant *entry;

    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE("aay")))
        return FALSE;

    g_variant_iter_init(&iter, value);
    while ((entry = g_variant_iter_next_value(&iter)) != NULL) {
        gsize length = 0;
        const gchar *mount = g_variant_get_fixed_array(entry, &length, sizeof(guchar));
        if (mount != NULL && length >= 2 && mount[0] == '/' && mount[1] == '\0')
            *is_root = TRUE;
        g_variant_unref(entry);
    }
    return g_variant_n_children(value) > 0;
}

static gboolean
is_ignored_device(const gchar *device)
{
    if (device == NULL)
        return TRUE;
    return g_str_has_prefix(device, "/dev/loop") ||
           g_str_has_prefix(device, "/dev/zram") ||
           g_str_has_prefix(device, "/dev/dm-") ||
           g_str_has_prefix(device, "/dev/md");
}

static void
scan_blocks_for_drive(LucDeviceMonitor *self, LucDevice *device)
{
    g_autolist(GDBusObject) objects = g_dbus_object_manager_get_objects(self->manager);
    GList *item;

    for (item = objects; item != NULL; item = item->next) {
        GDBusObject *object = item->data;
        g_autoptr(GDBusInterface) block = g_dbus_object_get_interface(object, UDISKS_BLOCK);
        g_autofree gchar *drive_path = NULL;

        if (block == NULL)
            continue;
        drive_path = property_object_path(block, "Drive");
        if (g_strcmp0(drive_path, device->drive_path) != 0)
            continue;

        if (device->device == NULL) {
            device->object_path = g_strdup(g_dbus_object_get_object_path(object));
            device->device = property_bytestring(block, "Device");
            device->read_only = property_boolean(block, "ReadOnly", FALSE);
            device->ignored_kind = is_ignored_device(device->device);
        }

        g_autoptr(GDBusInterface) filesystem =
            g_dbus_object_get_interface(object, UDISKS_FILESYSTEM);
        if (filesystem != NULL) {
            gboolean is_root = FALSE;
            device->mounted |= filesystem_is_mounted(filesystem, &is_root);
            device->system_device |= is_root;
        }

        g_autoptr(GDBusInterface) swap =
            g_dbus_object_get_interface(object, UDISKS_SWAPSPACE);
        if (swap != NULL)
            device->active_swap |= property_boolean(swap, "Active", FALSE);
    }
}

static gint
compare_devices(gconstpointer left, gconstpointer right)
{
    const LucDevice *a = *(LucDevice *const *)left;
    const LucDevice *b = *(LucDevice *const *)right;
    if (a->size < b->size)
        return -1;
    if (a->size > b->size)
        return 1;
    return g_strcmp0(a->device, b->device);
}

GPtrArray *
luc_device_monitor_dup_devices(LucDeviceMonitor *self)
{
    g_autolist(GDBusObject) objects = NULL;
    GPtrArray *devices;
    GList *item;

    g_return_val_if_fail(LUC_IS_DEVICE_MONITOR(self), NULL);
    devices = g_ptr_array_new_with_free_func((GDestroyNotify)luc_device_free);
    objects = g_dbus_object_manager_get_objects(self->manager);

    for (item = objects; item != NULL; item = item->next) {
        GDBusObject *object = item->data;
        g_autoptr(GDBusInterface) drive = g_dbus_object_get_interface(object, UDISKS_DRIVE);
        g_autoptr(LucDevice) device = NULL;

        if (drive == NULL)
            continue;

        device = luc_device_new();
        device->drive_path = g_strdup(g_dbus_object_get_object_path(object));
        device->vendor = property_string(drive, "Vendor");
        device->model = property_string(drive, "Model");
        device->serial = property_string(drive, "Serial");
        device->connection_bus = property_string(drive, "ConnectionBus");
        device->size = property_uint64(drive, "Size");
        device->removable = property_boolean(drive, "Removable", FALSE);
        device->media_removable = property_boolean(drive, "MediaRemovable", FALSE);
        device->media_available = property_boolean(drive, "MediaAvailable", TRUE);
        device->optical = property_boolean(drive, "Optical", FALSE);
        scan_blocks_for_drive(self, device);
        g_ptr_array_add(devices, g_steal_pointer(&device));
    }

    g_ptr_array_sort(devices, compare_devices);
    return devices;
}

static void
emit_devices_changed(LucDeviceMonitor *self)
{
    g_signal_emit(self, signals[DEVICES_CHANGED], 0);
}

static void
on_object_changed(GDBusObjectManager *manager,
                  GDBusObject *object,
                  LucDeviceMonitor *self)
{
    (void)manager;
    (void)object;
    emit_devices_changed(self);
}

static void
on_properties_changed(GDBusObjectManagerClient *manager,
                      GDBusObjectProxy *object,
                      GDBusProxy *interface,
                      GVariant *changed_properties,
                      const gchar *const *invalidated_properties,
                      LucDeviceMonitor *self)
{
    (void)manager;
    (void)object;
    (void)interface;
    (void)changed_properties;
    (void)invalidated_properties;
    emit_devices_changed(self);
}

static void
luc_device_monitor_dispose(GObject *object)
{
    LucDeviceMonitor *self = LUC_DEVICE_MONITOR(object);
    g_clear_object(&self->manager);
    G_OBJECT_CLASS(luc_device_monitor_parent_class)->dispose(object);
}

static void
luc_device_monitor_class_init(LucDeviceMonitorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = luc_device_monitor_dispose;

    signals[DEVICES_CHANGED] = g_signal_new(
        "devices-changed",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL,
        NULL,
        NULL,
        G_TYPE_NONE,
        0);
}

static void
luc_device_monitor_init(LucDeviceMonitor *self)
{
    (void)self;
}

LucDeviceMonitor *
luc_device_monitor_new(GError **error)
{
    g_autoptr(LucDeviceMonitor) self = g_object_new(LUC_TYPE_DEVICE_MONITOR, NULL);

    self->manager = g_dbus_object_manager_client_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
        UDISKS_NAME,
        UDISKS_PATH,
        NULL,
        NULL,
        NULL,
        NULL,
        error);
    if (self->manager == NULL)
        return NULL;

    g_signal_connect(self->manager, "object-added", G_CALLBACK(on_object_changed), self);
    g_signal_connect(self->manager, "object-removed", G_CALLBACK(on_object_changed), self);
    g_signal_connect(self->manager,
                     "interface-proxy-properties-changed",
                     G_CALLBACK(on_properties_changed),
                     self);
    return g_steal_pointer(&self);
}

