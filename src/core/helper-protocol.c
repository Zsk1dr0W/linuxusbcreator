/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/helper-protocol.h"

#include <string.h>

static gboolean
parse_progress(const gchar *line, LucHelperEvent *event)
{
    static const gchar prefix[] = "{\"event\":\"progress\",\"completed\":";
    static const gchar separator[] = ",\"total\":";
    gchar *end = NULL;
    guint64 completed;
    guint64 total;

    if (!g_str_has_prefix(line, prefix))
        return FALSE;
    completed = g_ascii_strtoull(line + strlen(prefix), &end, 10);
    if (end == line + strlen(prefix) || !g_str_has_prefix(end, separator))
        return FALSE;
    total = g_ascii_strtoull(end + strlen(separator), &end, 10);
    if (end == NULL || !g_str_equal(end, "}") || total == 0 || completed > total)
        return FALSE;

    event->type = LUC_HELPER_EVENT_PROGRESS;
    event->completed = completed;
    event->total = total;
    return TRUE;
}

gboolean
luc_helper_event_parse(const gchar *line, LucHelperEvent *event)
{
    g_return_val_if_fail(line != NULL, FALSE);
    g_return_val_if_fail(event != NULL, FALSE);
    *event = (LucHelperEvent){0};

    if (parse_progress(line, event))
        return TRUE;
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"hashing\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_HASHING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"validating\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_VALIDATING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"inspecting\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_INSPECTING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"partitioning\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_PARTITIONING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"formatting\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_FORMATTING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"mounting\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_MOUNTING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"copying\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_COPYING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"splitting\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_SPLITTING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"writing\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_WRITING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"syncing\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_SYNCING;
        return TRUE;
    }
    if (g_str_equal(line, "{\"event\":\"phase\",\"name\":\"verifying\"}")) {
        event->type = LUC_HELPER_EVENT_PHASE;
        event->phase = LUC_HELPER_PHASE_VERIFYING;
        return TRUE;
    }
    if (g_str_has_prefix(line, "{\"event\":\"completed\",")) {
        event->type = LUC_HELPER_EVENT_COMPLETED;
        return TRUE;
    }
    return FALSE;
}

gboolean
luc_helper_parse_prepared(const gchar *line, gchar **partition_path)
{
    static const gchar prefix[] = "{\"event\":\"prepared\",\"partition\":\"";
    static const gchar suffix[] = "\"}";
    g_autofree gchar *value = NULL;
    g_autofree gchar *basename = NULL;
    gsize length;

    g_return_val_if_fail(line != NULL, FALSE);
    g_return_val_if_fail(partition_path != NULL && *partition_path == NULL, FALSE);
    if (!g_str_has_prefix(line, prefix) || !g_str_has_suffix(line, suffix))
        return FALSE;
    length = strlen(line);
    if (length <= strlen(prefix) + strlen(suffix))
        return FALSE;
    value = g_strndup(line + strlen(prefix),
                      length - strlen(prefix) - strlen(suffix));
    if (!g_str_has_prefix(value, "/dev/") || strchr(value + 5, '/') != NULL ||
        strchr(value, '\\') != NULL || strchr(value, '"') != NULL)
        return FALSE;
    basename = g_path_get_basename(value);
    if (basename[0] == '\0' || g_str_equal(basename, ".") ||
        g_str_equal(basename, ".."))
        return FALSE;
    *partition_path = g_steal_pointer(&value);
    return TRUE;
}
