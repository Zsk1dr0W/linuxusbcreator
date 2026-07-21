/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "linux/filesystem-mount.h"

#include <glib/gstdio.h>
#include <string.h>

#define UDISKS_NAME "org.freedesktop.UDisks2"
#define UDISKS_PATH "/org/freedesktop/UDisks2"
#define UDISKS_BLOCK "org.freedesktop.UDisks2.Block"
#define UDISKS_FILESYSTEM "org.freedesktop.UDisks2.Filesystem"
#define UDISKS_DISCOVERY_ATTEMPTS 600
#define UDISKS_CALL_TIMEOUT_MS 120000

struct _LucFilesystemMount {
    GDBusObjectManager *manager;
    GDBusProxy *filesystem;
    gchar *mount_path;
    gboolean closed;
};

static GVariant *
proxy_property(GDBusInterface *interface, const gchar *name)
{
    if (!G_IS_DBUS_PROXY(interface))
        return NULL;
    return g_dbus_proxy_get_cached_property(G_DBUS_PROXY(interface), name);
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
    return g_strndup(bytes, bytes[length - 1] == '\0' ? length - 1 : length);
}

static gchar *
first_mount_point(GDBusInterface *filesystem)
{
    g_autoptr(GVariant) value = proxy_property(filesystem, "MountPoints");
    GVariantIter iter;
    GVariant *entry;

    if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE("aay")))
        return NULL;
    g_variant_iter_init(&iter, value);
    while ((entry = g_variant_iter_next_value(&iter)) != NULL) {
        gsize length = 0;
        const gchar *bytes =
            g_variant_get_fixed_array(entry, &length, sizeof(guchar));
        gchar *path = NULL;

        if (bytes != NULL && length > 1 && bytes[length - 1] == '\0')
            path = g_strndup(bytes, length - 1);
        g_variant_unref(entry);
        if (path != NULL)
            return path;
    }
    return NULL;
}

static GDBusProxy *
wait_for_filesystem(GDBusObjectManager *manager,
                    const gchar *device_path,
                    const gchar *filesystem_type,
                    GCancellable *cancellable,
                    GError **error)
{
    for (guint attempt = 0; attempt < UDISKS_DISCOVERY_ATTEMPTS; attempt++) {
        g_autolist(GDBusObject) objects =
            g_dbus_object_manager_get_objects(manager);

        if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                "Filesystem mount was cancelled");
            return NULL;
        }
        for (GList *item = objects; item != NULL; item = item->next) {
            GDBusObject *object = item->data;
            g_autoptr(GDBusInterface) block =
                g_dbus_object_get_interface(object, UDISKS_BLOCK);
            g_autoptr(GDBusInterface) filesystem = NULL;
            g_autoptr(GVariant) id_type = NULL;
            g_autofree gchar *device = NULL;

            if (block == NULL)
                continue;
            device = property_bytestring(block, "Device");
            if (g_strcmp0(device, device_path) != 0)
                continue;
            id_type = proxy_property(block, "IdType");
            if (id_type == NULL ||
                !g_variant_is_of_type(id_type, G_VARIANT_TYPE_STRING) ||
                !g_str_equal(g_variant_get_string(id_type, NULL),
                             filesystem_type))
                continue;
            filesystem = g_dbus_object_get_interface(object, UDISKS_FILESYSTEM);
            if (G_IS_DBUS_PROXY(filesystem))
                return g_object_ref(G_DBUS_PROXY(filesystem));
        }
        g_usleep(100000);
    }
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                "UDisks2 did not expose the %s filesystem: %s",
                filesystem_type, device_path);
    return NULL;
}

LucFilesystemMount *
luc_filesystem_mount_open(const gchar *device_path,
                          GCancellable *cancellable,
                          GError **error)
{
    return luc_filesystem_mount_open_type(device_path, "vfat", cancellable,
                                          error);
}

LucFilesystemMount *
luc_filesystem_mount_open_type(const gchar *device_path,
                               const gchar *filesystem_type,
                               GCancellable *cancellable,
                               GError **error)
{
    g_autoptr(LucFilesystemMount) mount = NULL;
    g_autoptr(GVariant) reply = NULL;
    GVariantBuilder options;
    const gchar *path;

    g_return_val_if_fail(device_path != NULL, NULL);
    g_return_val_if_fail(filesystem_type != NULL, NULL);
    if (!g_str_equal(filesystem_type, "vfat") &&
        !g_str_equal(filesystem_type, "ext4")) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Unsupported target filesystem type");
        return NULL;
    }
    if (!g_str_has_prefix(device_path, "/dev/") ||
        strchr(device_path + 5, '/') != NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Filesystem target must be a direct /dev path");
        return NULL;
    }
    mount = g_new0(LucFilesystemMount, 1);
    mount->manager = g_dbus_object_manager_client_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
        UDISKS_NAME, UDISKS_PATH, NULL, NULL, NULL, cancellable, error);
    if (mount->manager == NULL)
        return NULL;
    mount->filesystem = wait_for_filesystem(mount->manager, device_path,
                                            filesystem_type,
                                            cancellable, error);
    if (mount->filesystem == NULL)
        return NULL;
    mount->mount_path = first_mount_point(G_DBUS_INTERFACE(mount->filesystem));
    if (mount->mount_path == NULL) {
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "auth.no_user_interaction",
                              g_variant_new_boolean(TRUE));
        reply = g_dbus_proxy_call_sync(
            mount->filesystem, "Mount", g_variant_new("(a{sv})", &options),
            G_DBUS_CALL_FLAGS_NONE, UDISKS_CALL_TIMEOUT_MS, cancellable, error);
        if (reply == NULL) {
            if (error != NULL && *error != NULL &&
                (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
                 g_error_matches(*error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT) ||
                 g_error_matches(*error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY))) {
                g_prefix_error(error,
                    "UDisks2 did not mount the freshly formatted Windows target within 120 seconds: ");
            }
            return NULL;
        }
        g_variant_get(reply, "(&s)", &path);
        mount->mount_path = g_strdup(path);
    }
    if (!g_path_is_absolute(mount->mount_path) ||
        !g_file_test(mount->mount_path, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "UDisks2 returned an invalid target mount path");
        return NULL;
    }
    return g_steal_pointer(&mount);
}

const gchar *
luc_filesystem_mount_get_path(const LucFilesystemMount *mount)
{
    g_return_val_if_fail(mount != NULL, NULL);
    return mount->mount_path;
}

gboolean
luc_filesystem_mount_close(LucFilesystemMount *mount,
                           GCancellable *cancellable,
                           GError **error)
{
    g_autoptr(GVariant) reply = NULL;
    GVariantBuilder options;

    g_return_val_if_fail(mount != NULL, FALSE);
    if (mount->closed)
        return TRUE;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "auth.no_user_interaction",
                          g_variant_new_boolean(TRUE));
    reply = g_dbus_proxy_call_sync(
        mount->filesystem, "Unmount", g_variant_new("(a{sv})", &options),
        G_DBUS_CALL_FLAGS_NONE, UDISKS_CALL_TIMEOUT_MS, cancellable, error);
    if (reply == NULL)
        return FALSE;
    mount->closed = TRUE;
    return TRUE;
}

void
luc_filesystem_mount_free(LucFilesystemMount *mount)
{
    if (mount == NULL)
        return;
    if (!mount->closed && mount->filesystem != NULL)
        luc_filesystem_mount_close(mount, NULL, NULL);
    g_clear_object(&mount->filesystem);
    g_clear_object(&mount->manager);
    g_free(mount->mount_path);
    g_free(mount);
}
