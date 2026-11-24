/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "action_menu_hierarchy.h"
#include "action_menu_window.h"
#include "action_menu_layer.h"

#include <applib/ui/crumbs_layer.h>

struct ActionMenu {
  Window window;
};

typedef struct AnimationContext {
  Window *window;
  const ActionMenuLevel *next_level;
} AnimationContext;

typedef struct {
  const ActionMenuLevel *cur_level;
  int num_dots;
  GEdgeInsets menu_insets;
} ActionMenuViewModel;

//! Draws and drives an action menu in place of its window (see action_menu_open_hosted).
typedef struct ActionMenuHost {
  //! The menu is closing. Remove its view, animated or not, then call
  //! action_menu_hosted_did_close.
  void (*close)(ActionMenu *action_menu, bool animated, void *context);
} ActionMenuHost;

//! State of a hosted menu, kept apart so a menu window stays the same size.
typedef struct ActionMenuHosting {
  const ActionMenuHost *host;
  void *context;
  WindowStack *window_stack;
  bool closing;
  bool performing;
  bool closed_while_performing;
} ActionMenuHosting;

typedef struct {
  ActionMenu action_menu;
  ActionMenuConfig config;
  //! Set for a hosted menu, which has no window of its own on a stack
  struct ActionMenuHosting *hosting;
  ActionMenuLayer action_menu_layer;
  CrumbsLayer crumbs_layer;
  ActionMenuViewModel view_model;
  Animation *level_change_anim;
  const ActionMenuItem *performed_item;
  Window *result_window;
  PreferredContentSize content_size;
  bool frozen;
} ActionMenuData;

// ActionMenuItem is a union of two types:
// * In the leaf case we have:
//    perform_action is a valid pointer and thus is_leaf is non 0 (== true)
//    action_data is a valid pointer, next_level is not used
// * In the level case we have:
//    is_leaf is 0 (== false), perform_action is not a valid pointer
//    next_level points to a valid ActionMenuLevel, action_data is not used
struct ActionMenuItem {
  const char *label;
  union {
    ActionMenuPerformActionCb perform_action;
    uintptr_t is_leaf;
  };
  union {
    void *action_data;
    ActionMenuLevel *next_level;
  };
};

//! Firmware-only display mode: a grid of enlarged single-glyph items, e.g. emoji replies.
//! Displays without room for it render the level like ActionMenuLevelDisplayModeThin.
#define ActionMenuLevelDisplayModeGlyphGrid \
  ((ActionMenuLevelDisplayMode)(ActionMenuLevelDisplayModeThin + 1))

struct ActionMenuLevel {
  ActionMenuLevel *parent_level;
  uint16_t max_items;
  uint16_t num_items;
  unsigned default_selected_item;
  // The separator (dotted line) will appear just above this index (an index of 0 will be ignored)
  // [PG] It should be used to help differentiate item specific actions vs global actions.
  // Double check with design before using this for another purpose.
  unsigned separator_index;
  ActionMenuLevelDisplayMode display_mode;
  ActionMenuLayerCallback selection_changed;
  ActionMenuItem items[];
};

//! Open an action menu whose view is provided by a host, e.g. a layer of another window, instead
//! of its own window. It is driven through the same ActionMenu API: items are performed with
//! action_menu_hosted_perform, freezing, closing and result windows work as for a menu window,
//! with result windows pushed onto window_stack.
ActionMenu *action_menu_open_hosted(WindowStack *window_stack, ActionMenuConfig *config,
                                    const ActionMenuHost *host, void *host_context);

//! Perform a leaf item of a hosted menu, as selecting it in a menu window would, then close the
//! menu unless the action froze it.
void action_menu_hosted_perform(ActionMenu *action_menu, const ActionMenuItem *item);

//! The host has removed the view of a closing hosted menu: call did_close and free the menu.
void action_menu_hosted_did_close(ActionMenu *action_menu);

//! Whether the menu is hosted rather than a window.
bool action_menu_is_hosted(ActionMenu *action_menu);

//! The window stack the menu and its result windows go on.
WindowStack *action_menu_get_window_stack(ActionMenu *action_menu);
