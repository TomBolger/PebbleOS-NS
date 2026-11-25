/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <applib/ui/action_menu_window.h>
#include <applib/ui/pill_action_menu.h>
#include <applib/ui/window.h>

//! @file notification_round_actions.h
//! Round watches: a timeline item's action menu drawn over its card instead of in a window, a
//! pill action menu swinging out of the card's banner (see pill_action_menu_open_from_banner),
//! its rows in the notification text size and colours.

//! Open the timeline action menu of @p config over @p window, as a hosted menu whose result
//! windows go on @p window_stack (see timeline_actions_open_hosted_action_menu). The circle and
//! the selected row are config's background colour. The menu closes as any ActionMenu does, and
//! config's did_close runs once it has.
//! @return the menu, or NULL if it could not open; config's hierarchy is then destroyed.
ActionMenu *notification_round_actions_open_timeline_menu(Window *window, ActionMenuConfig *config,
                                                          WindowStack *window_stack,
                                                          const PillActionMenuBanner *banner);
