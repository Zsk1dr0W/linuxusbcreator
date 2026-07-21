/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    LUC_HELPER_EVENT_NONE,
    LUC_HELPER_EVENT_PHASE,
    LUC_HELPER_EVENT_PROGRESS,
    LUC_HELPER_EVENT_COMPLETED,
} LucHelperEventType;

typedef enum {
    LUC_HELPER_PHASE_NONE,
    LUC_HELPER_PHASE_HASHING,
    LUC_HELPER_PHASE_INSPECTING,
    LUC_HELPER_PHASE_VALIDATING,
    LUC_HELPER_PHASE_PARTITIONING,
    LUC_HELPER_PHASE_FORMATTING,
    LUC_HELPER_PHASE_MOUNTING,
    LUC_HELPER_PHASE_COPYING,
    LUC_HELPER_PHASE_CONFIGURING,
    LUC_HELPER_PHASE_SPLITTING,
    LUC_HELPER_PHASE_WRITING,
    LUC_HELPER_PHASE_SYNCING,
    LUC_HELPER_PHASE_VERIFYING,
} LucHelperPhase;

typedef struct {
    LucHelperEventType type;
    LucHelperPhase phase;
    guint64 completed;
    guint64 total;
} LucHelperEvent;

gboolean luc_helper_event_parse(const gchar *line, LucHelperEvent *event);
gboolean luc_helper_parse_prepared(const gchar *line, gchar **partition_path);
gboolean luc_helper_parse_linux_prepared(const gchar *line,
                                         gchar **boot_partition_path,
                                         gchar **data_partition_path);

G_END_DECLS
