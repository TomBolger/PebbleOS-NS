/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "notification_round_actions.h"

#if PBL_ROUND

#include <pbl/i18n/i18n.h>
#include <pbl/services/notifications/notification_style.h>
#include <pbl/services/timeline/timeline_actions.h>

#include <applib/ui/action_menu_hierarchy.h>

static ActionMenu *prv_open_hosted(WindowStack *window_stack, ActionMenuConfig *config,
                                   const struct ActionMenuHost *host, void *host_context) {
  return timeline_actions_open_hosted_action_menu(config, window_stack, host, host_context);
}

ActionMenu *notification_round_actions_open_timeline_menu(Window *window, ActionMenuConfig *config,
                                                          WindowStack *window_stack,
                                                          const PillActionMenuBanner *banner) {
  // Rows follow the notification text size, 38 tall at the default size
  const PillActionMenuBannerConfig pill_config = {
    .window = window,
    .window_stack = window_stack,
    .open_hosted = prv_open_hosted,
    .banner = *banner,
    .color = config->colors.background,
    .text_color = GColorWhite,
    .background = notification_style_bg(),
    .foreground = notification_style_fg(),
    .font = {notification_style_scaled_font(24, true), notification_style_scaled_points(24)},
    .small_font = {notification_style_scaled_font(18, true), notification_style_scaled_points(18)},
  };
  ActionMenu *action_menu = pill_action_menu_open_from_banner(&pill_config, config);
  if (!action_menu) {
    // Nothing took the hierarchy
    action_menu_hierarchy_destroy(config->root_level, nullptr, nullptr);
    pbl_i18n_free_all(config->root_level);
  }
  return action_menu;
}

#endif // PBL_ROUND
