/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <adwaita.h>
#include "linux/device-monitor.h"

G_BEGIN_DECLS

#define LUC_TYPE_WINDOW (luc_window_get_type())
G_DECLARE_FINAL_TYPE(LucWindow, luc_window, LUC, WINDOW, AdwApplicationWindow)

GtkWidget *luc_window_new(AdwApplication *application, LucDeviceMonitor *monitor);

G_END_DECLS

