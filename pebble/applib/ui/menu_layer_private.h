/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "menu_layer.h"

struct MenuIterator;

typedef void (*MenuIteratorCallback)(struct MenuIterator *it);

typedef struct MenuIterator {
  MenuLayer *menu_layer;
  MenuCellSpan cursor;
  int16_t cell_bottom_y;
  MenuIteratorCallback row_callback_before_geometry;
  MenuIteratorCallback row_callback_after_geometry;
  MenuIteratorCallback section_callback;
  bool should_continue; // callback can set this to false if the row-loop should be exited.
} MenuIterator;

typedef struct MenuRenderIterator {
  MenuIterator it;
  GContext *ctx;
  int16_t content_top_y;
  int16_t content_bottom_y;
  bool cache_set : 1;
  bool cursor_in_frame : 1;
  MenuCellSpan new_cache;
  Layer cell_layer;
} MenuRenderIterator;

//! The short pulse a menu plays where it cannot scroll further, or as it wraps around
void menu_layer_play_scroll_vibe(void);

#if PBL_ROUND
#include "menu_layer_round.h"

//! Gap between a right-aligned cell icon and the bezel on round displays, keeping the icon well
//! inside the end of the highlight pill
#define MENU_CELL_ROUND_BEZEL_MARGIN 11

//! How much narrower the display is at \a frame's row, in the cell being drawn, than across its
//! middle, on each side.
int16_t menu_cell_round_chord_inset(const GContext *ctx, GRect frame);

//! Moves \a frame, an icon on the right of the cell being drawn, into the column right-aligned
//! icons share in every row: inside the round end of the pill of a row as tall resting a row from
//! the middle, as far in as MENU_CELL_ROUND_BEZEL_MARGIN is from the bezel in the middle.
GRect menu_cell_round_follow_bezel(const GContext *ctx, GRect frame);

//! The highlight pill of \a menu_layer's selection as it is on screen now.
//! @return false if the menu doesn't draw a pill, or there is no room for one
bool menu_layer_get_round_highlight(MenuLayer *menu_layer, MenuRoundShape *shape);

//! Hides the highlight pill of \a menu_layer, a round list with the pill, e.g. while something
//! drawn over the list takes its place. The selected row keeps its layout, in the normal colours.
void menu_layer_set_round_pill_hidden(MenuLayer *menu_layer, bool hidden);

//! Moves the left end of the highlight pill \a margin further in from the bezel, e.g. to keep it
//! clear of something drawn along the rim. 0 restores the default.
void menu_layer_set_round_pill_left_margin(MenuLayer *menu_layer, uint8_t margin);

//! Lays every row of \a menu_layer, a round list with the pill, out \a height taller, for the
//! subtitle line its rows show under their title, keeping the padding of a single line. 0, the
//! default, is for lists without subtitles.
void menu_layer_set_round_subtitle_height(MenuLayer *menu_layer, uint8_t height);

//! Height of the subtitle line of a system menu cell, see menu_layer_set_round_subtitle_height()
int16_t menu_cell_round_subtitle_height(void);
#endif

#ifdef CONFIG_TOUCH
#include <applib/ui/recognizer/swipe.h>
#include <applib/ui/recognizer/touch_nav.h>

//! Default gesture operations for menus that override a subset of the widget behavior.
const TouchNavWidgetOps *menu_layer_touch_get_default_ops(void);

//! Locate a row and its geometry in content coordinates, skipping headers and gaps.
bool menu_layer_touch_find_cell_at_content_y(MenuLayer *menu_layer, int16_t content_y,
                                             MenuCellSpan *cell_out);

//! @internal
//! Touch-navigation (Tier-1) gesture handlers, split out so they can be unit tested directly
//! without driving the full recognizer stack. All coordinates that flow in are already resolved to
//! the scroll layer's frame (screen) space; the handlers convert to content space internally using
//! the live content offset. See menu_layer.c for the wiring into the per-task recognizer set.

//! Locate the selectable row whose cell spans \a content_y (content-space y). Section headers and
//! separators are not selectable and are skipped. Walks downward then upward from the render
//! anchor.
//! @return true and fills \a index_out if a row is hit; false if \a content_y lands on a
//! header/gap.
bool menu_layer_touch_find_row_at_content_y(MenuLayer *menu_layer, int16_t content_y,
                                            MenuIndex *index_out);

//! Finger down: catch a coasting fling (stop it at touchdown, not at the pan threshold) and mark
//! the gesture's tap, if it becomes one, to be swallowed — a catch is a stop, not a select. On a
//! carousel the catch glides into the nearest row centre instead of dead-stopping off-grid.
void menu_layer_touch_handle_touchdown(MenuLayer *menu_layer);

//! Live scroll during a pan: move the content to \a base + \a delta_since_start; past the coarse
//! bounds it rubber-bands (damped overscroll) instead of hard-clamping, springing back on liftoff.
//! On a plain menu the selection index is intentionally left unchanged. On a center-focused menu
//! the focus stays pinned at the viewport centre: the row crossing the centre becomes the
//! selection live, through the full selection_will_change contract (veto/redirect honoured).
void menu_layer_touch_handle_pan_update(MenuLayer *menu_layer, GPoint base,
                                        GPoint delta_since_start);

//! Liftoff. Applies the last (unthrottled) pan delta. A plain menu only settles the offset — the
//! selection never moves on a pan. A center-focused menu re-tracks the row under the centre and
//! then glides it to the exact centre (animated). \a velocity is the liftoff velocity in px/s.
void menu_layer_touch_handle_snap(MenuLayer *menu_layer, GPoint base, GPoint final_delta,
                                  GPoint velocity);

//! One-step tap activation through the same contract: map \a point_on_screen into the scroll
//! layer's frame, hit-test it, run selection_will_change, and activate (select_click) only if the
//! final selection is the tapped row. A veto (final == old) does nothing; a redirect to a third row
//! selects it without activating.
void menu_layer_touch_handle_tap(MenuLayer *menu_layer, GPoint point_on_screen);

//! Horizontal swipe: right emits BACK through the touch bridge; left has no action.
void menu_layer_touch_handle_swipe(MenuLayer *menu_layer, SwipeDirection direction);

//! Cancelled gesture: a plain menu has nothing to settle; a center-focused menu re-centres its
//! selection synchronously (no animation) so the handover leaves a stable state.
void menu_layer_touch_handle_cancel(MenuLayer *menu_layer);

//! @internal Test seam: zero the per-task Tier-1 gesture singletons for cross-test isolation.
void menu_layer_touch_nav_reset_all(void);

//! @internal Test seam: whether \a menu_layer is the current per-task gesture target.
bool menu_layer_touch_is_gesture_target(const MenuLayer *menu_layer);
#endif
