/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "linux/iso-mount.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UDISKS_NAME "org.freedesktop.UDisks2"
#define UDISKS_MANAGER_PATH "/org/freedesktop/UDisks2/Manager"
#define UDISKS_MANAGER "org.freedesktop.UDisks2.Manager"
#define UDISKS_FILESYSTEM "org.freedesktop.UDisks2.Filesystem"
#define UDISKS_LOOP "org.freedesktop.UDisks2.Loop"
#define DBUS_PROPERTIES "org.freedesktop.DBus.Properties"

struct _LucIsoMount {
    GDBusConnection *connection;
    gchar *object_path;
    gchar *mount_path;
    gboolean closed;
};

static GVariant *
empty_options(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    return g_variant_builder_end(&builder);
}

static gboolean
remote_error_has_suffix(const GError *error, const gchar *suffix)
{
    g_autofree gchar *remote = NULL;

    if (error == NULL || !g_dbus_error_is_remote_error(error))
        return FALSE;
    remote = g_dbus_error_get_remote_error(error);
    return remote != NULL && g_str_has_suffix(remote, suffix);
}

static gboolean
filesystem_interface_is_pending(const GError *error)
{
    return remote_error_has_suffix(error, "UnknownInterface") ||
           remote_error_has_suffix(error, "UnknownObject") ||
           remote_error_has_suffix(error, "UnknownMethod") ||
           (remote_error_has_suffix(error, "InvalidArgs") &&
            error->message != NULL &&
            strstr(error->message, "No such interface") != NULL);
}

static gboolean
loop_is_already_detached(const GError *error)
{
    return remote_error_has_suffix(error, "UnknownObject") ||
           remote_error_has_suffix(error, "NotFound") ||
           (error != NULL && error->message != NULL &&
            (strstr(error->message, "No such device or address") != NULL ||
             strstr(error->message, "No such object") != NULL));
}

static gboolean
query_mount_path(LucIsoMount *mount,
                 gchar **mount_path,
                 GCancellable *cancellable,
                 GError **error)
{
    g_autoptr(GVariant) reply = NULL;
    g_autoptr(GVariant) value = NULL;
    GVariantIter iter;
    GVariant *entry;

    reply = g_dbus_connection_call_sync(
        mount->connection, UDISKS_NAME, mount->object_path,
        DBUS_PROPERTIES, "Get",
        g_variant_new("(ss)", UDISKS_FILESYSTEM, "MountPoints"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1,
        cancellable, error);
    if (reply == NULL)
        return FALSE;
    g_variant_get(reply, "(v)", &value);
    if (!g_variant_is_of_type(value, G_VARIANT_TYPE("aay"))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "UDisks2 returned invalid mount-point data");
        return FALSE;
    }
    g_variant_iter_init(&iter, value);
    while ((entry = g_variant_iter_next_value(&iter)) != NULL) {
        gsize length = 0;
        const gchar *bytes = g_variant_get_fixed_array(entry, &length, sizeof(guchar));

        if (bytes != NULL && length > 1 && bytes[length - 1] == '\0') {
            *mount_path = g_strndup(bytes, length - 1);
            g_variant_unref(entry);
            return TRUE;
        }
        g_variant_unref(entry);
    }
    return TRUE;
}

static gboolean
wait_for_filesystem(LucIsoMount *mount,
                    GCancellable *cancellable,
                    GError **error)
{
    for (guint attempt = 0; attempt < 300; attempt++) {
        g_autoptr(GError) local_error = NULL;
        g_autofree gchar *path = NULL;

        if (query_mount_path(mount, &path, cancellable, &local_error)) {
            mount->mount_path = g_steal_pointer(&path);
            return TRUE;
        }
        if (g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_propagate_error(error, g_steal_pointer(&local_error));
            return FALSE;
        }
        if (!filesystem_interface_is_pending(local_error)) {
            g_propagate_error(error, g_steal_pointer(&local_error));
            return FALSE;
        }
        g_usleep(100000);
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                        "UDisks2 did not expose the ISO filesystem in time");
    return FALSE;
}

static gboolean
mount_filesystem(LucIsoMount *mount,
                 GCancellable *cancellable,
                 GError **error)
{
    g_autoptr(GVariant) reply = NULL;
    g_autoptr(GError) local_error = NULL;
    const gchar *path;

    if (mount->mount_path != NULL)
        return TRUE;
    reply = g_dbus_connection_call_sync(
        mount->connection, UDISKS_NAME, mount->object_path,
        UDISKS_FILESYSTEM, "Mount",
        g_variant_new("(@a{sv})", empty_options()),
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, -1,
        cancellable, &local_error);
    if (reply == NULL) {
        if (remote_error_has_suffix(local_error, "AlreadyMounted") &&
            query_mount_path(mount, &mount->mount_path, cancellable, error))
            return mount->mount_path != NULL;
        g_propagate_error(error, g_steal_pointer(&local_error));
        return FALSE;
    }
    g_variant_get(reply, "(&s)", &path);
    mount->mount_path = g_strdup(path);
    return TRUE;
}

static void
delete_loop_best_effort(LucIsoMount *mount, GCancellable *cancellable)
{
    g_autoptr(GError) ignored = NULL;
    g_autoptr(GVariant) reply = NULL;

    if (mount->connection == NULL || mount->object_path == NULL)
        return;
    reply = g_dbus_connection_call_sync(
        mount->connection, UDISKS_NAME, mount->object_path,
        UDISKS_LOOP, "Delete", g_variant_new("(@a{sv})", empty_options()),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancellable, &ignored);
}

LucIsoMount *
luc_iso_mount_open(const gchar *image_path,
                   GCancellable *cancellable,
                   GError **error)
{
    g_autoptr(LucIsoMount) mount = NULL;
    g_autoptr(GUnixFDList) fd_list = NULL;
    g_autoptr(GVariant) reply = NULL;
    GVariantBuilder options;
    struct stat metadata;
    const gchar *object_path;
    int image_fd = -1;
    int fd_index;

    g_return_val_if_fail(image_path != NULL, NULL);
    image_fd = g_open(image_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (image_fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Unable to open ISO: %s", g_strerror(errno));
        return NULL;
    }
    if (fstat(image_fd, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        close(image_fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "ISO must be a regular file, not a link");
        return NULL;
    }
    mount = g_new0(LucIsoMount, 1);
    mount->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, cancellable, error);
    if (mount->connection == NULL) {
        close(image_fd);
        return NULL;
    }
    fd_list = g_unix_fd_list_new();
    fd_index = g_unix_fd_list_append(fd_list, image_fd, error);
    close(image_fd);
    if (fd_index < 0)
        return NULL;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "read-only", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&options, "{sv}", "no-part-scan", g_variant_new_boolean(TRUE));
    reply = g_dbus_connection_call_with_unix_fd_list_sync(
        mount->connection, UDISKS_NAME, UDISKS_MANAGER_PATH,
        UDISKS_MANAGER, "LoopSetup",
        g_variant_new("(h@a{sv})", fd_index, g_variant_builder_end(&options)),
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1,
        fd_list, NULL, cancellable, error);
    if (reply == NULL)
        return NULL;
    g_variant_get(reply, "(&o)", &object_path);
    if (!g_str_has_prefix(object_path, "/org/freedesktop/UDisks2/block_devices/")) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "UDisks2 returned an invalid loop object path");
        return NULL;
    }
    mount->object_path = g_strdup(object_path);
    if (!wait_for_filesystem(mount, cancellable, error) ||
        !mount_filesystem(mount, cancellable, error)) {
        delete_loop_best_effort(mount, cancellable);
        return NULL;
    }
    if (!g_path_is_absolute(mount->mount_path) ||
        !g_file_test(mount->mount_path, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "UDisks2 returned an invalid ISO mount path");
        luc_iso_mount_close(mount, cancellable, NULL);
        return NULL;
    }
    g_clear_pointer(&reply, g_variant_unref);
    reply = g_dbus_connection_call_sync(
        mount->connection, UDISKS_NAME, mount->object_path,
        UDISKS_LOOP, "SetAutoclear",
        g_variant_new("(b@a{sv})", TRUE, empty_options()),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancellable, error);
    if (reply == NULL) {
        luc_iso_mount_close(mount, cancellable, NULL);
        return NULL;
    }
    return g_steal_pointer(&mount);
}

const gchar *
luc_iso_mount_get_path(const LucIsoMount *mount)
{
    g_return_val_if_fail(mount != NULL, NULL);
    return mount->mount_path;
}

gboolean
luc_iso_mount_close(LucIsoMount *mount,
                    GCancellable *cancellable,
                    GError **error)
{
    g_autoptr(GError) local_error = NULL;
    g_autoptr(GVariant) reply = NULL;
    gboolean detached = FALSE;

    g_return_val_if_fail(mount != NULL, FALSE);
    if (mount->closed)
        return TRUE;
    if (mount->mount_path != NULL) {
        reply = g_dbus_connection_call_sync(
            mount->connection, UDISKS_NAME, mount->object_path,
            UDISKS_FILESYSTEM, "Unmount",
            g_variant_new("(@a{sv})", empty_options()),
            NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancellable, &local_error);
    }
    g_clear_pointer(&reply, g_variant_unref);
    if (mount->object_path != NULL) {
        g_autoptr(GError) delete_error = NULL;

        reply = g_dbus_connection_call_sync(
            mount->connection, UDISKS_NAME, mount->object_path,
            UDISKS_LOOP, "Delete", g_variant_new("(@a{sv})", empty_options()),
            NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancellable, &delete_error);
        if (reply != NULL || loop_is_already_detached(delete_error)) {
            detached = TRUE;
        } else {
            if (local_error == NULL)
                local_error = g_steal_pointer(&delete_error);
        }
    }
    mount->closed = detached;
    if (!detached && error != NULL)
        g_propagate_error(error, g_steal_pointer(&local_error));
    return detached;
}

void
luc_iso_mount_free(LucIsoMount *mount)
{
    if (mount == NULL)
        return;
    if (!mount->closed)
        luc_iso_mount_close(mount, NULL, NULL);
    g_clear_object(&mount->connection);
    g_free(mount->object_path);
    g_free(mount->mount_path);
    g_free(mount);
}
