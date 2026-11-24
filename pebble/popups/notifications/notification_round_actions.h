/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/services/timeline/timeline_resources.h>

#include <applib/graphics/gtypes.h>
#include <applib/ui/action_menu_window.h>
#include <applib/ui/window.h>

//! @file notification_round_actions.h
//! Round watches: a notification's action menu drawn over its card instead of in a window.
//!
//! Opening swings the card's round banner around to the left edge, where it settles as a bubble
//! holding the notification's icon, while the actions slide in as pills to the right of it.
//! Closing plays the same motion backwards. The rows are the items of a hosted ActionMenu (see
//! action_menu_open_hosted), so actions run through the usual ActionMenu callbacks; submenus
//! replace the rows in place.

//! Where the card's banner is when the menu opens, in screen coordinates.
typedef struct NotificationRoundActionsBanner {
  GColor color;
  //! The notification's icon (a PDC image), drawn in the bubble.
  AppResourceInfo icon;
  //! Lower edge of the banner as drawn now: above the screen when the card is scrolled down.
  int16_t bottom;
  //! Centre of the icon in the banner.
  int16_t icon_center_y;
} NotificationRoundActionsBanner;

//! Called once the menu has closed and its layer is gone.
typedef void (*NotificationRoundActionsClosedCb)(ActionMenu *action_menu, void *context);

//! Open the menu of hosted @p action_menu over @p window. Its click handling replaces the
//! window's until it closes, and it takes touch over the whole window.
//! @return false if it could not open.
bool notification_round_actions_open(Window *window, ActionMenu *action_menu,
                                     const NotificationRoundActionsBanner *banner,
                                     NotificationRoundActionsClosedCb closed, void *context);

//! Close the menu, playing the opening backwards if @p animated. The closed callback runs once
//! the layer is gone, which is immediately if not animated.
void notification_round_actions_close(bool animated);

//! The opening progress for time @p t, both in thousandths: an ease out with a small overshoot,
//! played backwards for closing.
int32_t notification_round_actions_ease(int32_t t);

//! Scroll position of the rows for @p selected of @p count rows of @p row_height: the first row
//! starts two rows above the centre, the selection walks down to the centre, the list then
//! scrolls under it and stops with the last row two below the centre.
int32_t notification_round_actions_scroll_for(uint16_t selected, uint16_t count,
                                              int16_t row_height);
