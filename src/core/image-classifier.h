/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
    LUC_IMAGE_KIND_RAW_OR_UNKNOWN,
    LUC_IMAGE_KIND_LINUX_ISO,
    LUC_IMAGE_KIND_WINDOWS_ISO,
} LucImageKind;

typedef enum {
    LUC_LINUX_PROFILE_NONE,
    LUC_LINUX_PROFILE_FEDORA_LIVE_UEFI,
} LucLinuxProfile;

typedef struct {
    LucImageKind kind;
    gchar *distribution;
    gchar *architecture;
    LucLinuxProfile linux_profile;
} LucImageClassification;

void luc_image_classification_free(LucImageClassification *classification);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(LucImageClassification,
                              luc_image_classification_free)

LucImageClassification *luc_image_classify_listing(const gchar *listing);
LucImageClassification *luc_image_classify(const gchar *path,
                                            GCancellable *cancellable,
                                            GError **error);
const gchar *luc_image_kind_to_string(LucImageKind kind);
const gchar *luc_linux_profile_to_string(LucLinuxProfile profile);

G_END_DECLS
