/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "alarm_editor.h"

#include <pbl/services/alarms/alarm.h>

#include <applib/ui/menu_layer.h>

//! Opens the actions of an alarm. On round displays they open over \a menu_layer's window, out of
//! its selected row.
void alarm_detail_window_push(AlarmId alarm_id, AlarmInfo *alarm_info, MenuLayer *menu_layer,
                              AlarmEditorCompleteCallback alarm_editor_callback,
                              void *callback_context);
