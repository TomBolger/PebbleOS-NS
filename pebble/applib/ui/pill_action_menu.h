/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file pill_action_menu.h
//! Round watches: an action menu shown over a window instead of in an ActionMenu window.
//!
//! The actions slide in from the right behind a sweeping edge, as rows highlighted with a pill,
//! while a circle settles hanging off the left edge of the screen, the size of the round
//! notification banner, holding an icon. The circle comes from one of two places:
//! - a menu's selected row: its pill moves leftward and morphs into the circle, every frame a
//!   single box with corners as round as half its shorter side, as the menu slides out to the
//!   left (pill_action_menu_open);
//! - a card's round banner: it swings around the screen centre from the top to the left edge as
//!   the card slides out to the left (pill_action_menu_open_from_banner).
//! Closing plays the opening backwards. The rows are the items of a hosted ActionMenu (see
//! action_menu_open_hosted), so actions run through the usual ActionMenu callbacks; submenus
//! replace the rows in place. Closing the ActionMenu without animation while it closes animated
//! ends that at once.

#include <stdint.h>

#include <pbl/services/timeline/timeline_resources.h>

#include <applib/fonts/fonts.h>
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
//! its normal colours for the rest. The labels are Gothic 24 bold, 18 bold where they don't fit.
//! @return the hosted action menu, or NULL if it could not open
ActionMenu *pill_action_menu_open(const PillActionMenuConfig *config,
                                  ActionMenuConfig *menu_config);

//! A font of the rows, and its size in points
typedef struct PillActionMenuFont {
  GFont font;
  int16_t points;
} PillActionMenuFont;

//! A card's round banner, where it is on screen as the menu opens
typedef struct PillActionMenuBanner {
  //! The card's icon (a PDC image), held by the circle
  AppResourceInfo icon;
  //! Lower edge of the banner as drawn now: above the screen when the card is scrolled down
  int16_t bottom;
  //! Centre of the icon in the banner
  int16_t icon_center_y;
  //! The card, slid out to the left as the actions come in from the right; may be NULL
  Layer *card;
} PillActionMenuBanner;

struct ActionMenuHost;

//! Opens a hosted ActionMenu, as action_menu_open_hosted does
typedef ActionMenu *(*PillActionMenuOpenHosted)(WindowStack *window_stack, ActionMenuConfig *config,
                                                const struct ActionMenuHost *host,
                                                void *host_context);

typedef struct PillActionMenuBannerConfig {
  //! The window the card is on; its clicks go to the action menu while it is open
  Window *window;
  //! Where the actions' result windows go
  WindowStack *window_stack;
  //! Opens the hosted ActionMenu of the rows; action_menu_open_hosted if NULL
  PillActionMenuOpenHosted open_hosted;
  PillActionMenuBanner banner;
  //! The banner's colour, for the circle and the selected row
  GColor color;
  //! The selected row's text
  GColor text_color;
  //! Behind the rows, and their text
  GColor background;
  GColor foreground;
  //! The labels, and those that don't fit at that size; a row is the font's size + 14 tall
  PillActionMenuFont font;
  PillActionMenuFont small_font;
} PillActionMenuBannerConfig;

//! Opens the action menu of \a menu_config over \a config's window, swinging out of its card's
//! round banner. If the banner is scrolled out of sight, the circle grows in from just above the
//! screen.
//! @return the hosted action menu, or NULL if it could not open
ActionMenu *pill_action_menu_open_from_banner(const PillActionMenuBannerConfig *config,
                                              ActionMenuConfig *menu_config);
