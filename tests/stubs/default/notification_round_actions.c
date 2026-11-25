/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/compiler.h>

#include <popups/notifications/notification_round_actions.h>

#if PBL_ROUND
ActionMenu *PBL_WEAK notification_round_actions_open_timeline_menu(
    Window *window, ActionMenuConfig *config, WindowStack *window_stack,
    const NotificationRoundActionsBanner *banner) {
  return nullptr;
}

void PBL_WEAK notification_round_actions_close(bool animated) {
}
#endif
