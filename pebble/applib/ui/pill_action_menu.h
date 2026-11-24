/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file pill_action_menu.h
//! Round watches: an action menu shown over a menu's window instead of in an ActionMenu window.
//!
//! Opening morphs the pill of the menu's selected row, leftward, into a circle hanging off the
//! left edge of the screen, the size of the round notification banner, holding an icon, while
//! the menu slides out to the left and the actions slide in from the right as rows highlighted
//! with a pill. Every frame of the morph is a single box with corners as round as half its
//! shorter side, so the shape goes from pill to circle without ever breaking up. Closing plays
//! the opening backwards, into the pill of the row then selected. The rows are the items of a
//! hosted ActionMenu (see action_menu_open_hosted), so actions run through the usual ActionMenu
//! callbacks; submenus replace the rows in place.

#include <stdint.h>

#include <applib/ui/action_menu_window.h>
#include <applib/ui/menu_layer.h>
#include <applib/ui/window.h>

typedef struct PillActionMenuConfig {
  //! The menu whose selected row opens the action menu, on \ref window
  MenuLayer *menu_layer;
  //! The window the menu is on; its clicks go to the action menu while it is open
  Window *window;
  //! Resource of the PDC icon held by the circle
  uint32_t icon_resource_id;
} PillActionMenuConfig;

//! Opens the action menu of \a menu_config over \a config's window, growing out of its menu's
//! selected row. The colours are the menu's: its highlight for the circle and the selected row,
//! its normal colours for the rest.
//! @return the hosted action menu, or NULL if it could not open
ActionMenu *pill_action_menu_open(const PillActionMenuConfig *config,
                                  ActionMenuConfig *menu_config);
