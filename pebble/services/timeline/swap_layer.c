/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/services/evented_timer.h>
#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/timeline/layout_layer.h>
#include <pbl/services/timeline/notification_layout.h>
#include <pbl/services/timeline/swap_layer.h>
#include <pbl/util/math.h>

#include <applib/applib_malloc.auto.h>
#include <applib/graphics/graphics.h>
#include <applib/graphics/gtypes.h>
#include <applib/ui/property_animation.h>
#include <applib/ui/status_bar_layer.h>
#include <applib/ui/window.h>
#include <kernel/ui/kernel_ui.h>
#include <process_state/app_state/app_state.h>
#include <resource/resource_ids.auto.h>
#include <shell/system_theme.h>

#ifdef CONFIG_TOUCH
#include <pbl/util/testing.h>

#include <applib/ui/recognizer/pan.h>
#include <applib/ui/recognizer/touch_nav.h>
#include <kernel/pebble_tasks.h>
#include <pbl/kernel/time.h>
#include <syscall/syscall.h>

#include <string.h>
#endif

// Initial pixel scroll amount: the banner, so the first press reaches the text
#define INITIAL_SCROLL_PX LAYOUT_TOP_BANNER_HEIGHT

// Max pixel scroll amount
#define SCROLL_PX 48

// Scroll and swap animation speed, both eased out
#define SCROLL_MS 200
#define SWAP_MS 200

// Pixel peek amount for next layout: its banner on rectangular displays, the top of its banner
// circle (a half dot) on round ones
#define PEEK_PX PBL_IF_RECT_ELSE(LAYOUT_BANNER_HEIGHT_RECT, STATUS_BAR_LAYER_HEIGHT)

// Get within FUDGE_PX to the end of the layout on a scroll, and will
// scroll the rest of the way.
#define FUDGE_PX PEEK_PX

// Press and hold: after HOLD_START_MS the layout scrolls on evenly to its end, rests there for
// HOLD_BUMP_MS, then swaps in HOLD_SWAP_MS ending at the hold speed and carries straight on.
// Releasing coasts HOLD_RELEASE_PX further, at least to where the press itself was heading.
#define HOLD_START_MS 120
#define HOLD_SPEED_PX_S 260
#define HOLD_SWAP_MS 240
#define HOLD_BUMP_MS 600
#define HOLD_RELEASE_PX 8
#define HOLD_RELEASE_MS 140
// Steepest end slope (x1000) of the hold swap curve
#define HOLD_SWAP_MAX_SLOPE 2500

typedef enum {
  ScrollAnimationCurveKind_Interpolator,
  ScrollAnimationCurveKind_Curve
} ScrollAnimationCurveKind;

typedef struct {
  ScrollAnimationCurveKind swap_curve_kind;
  union {
    struct {
      InterpolateInt64Function interpolator;
    };
    struct {
      AnimationCurve curve;
    };
  };
} ScrollAnimationCurve;

///////////////////////
// HELPER FUNCTIONS
///////////////////////

//! Helper to prevent casting all over the place.
static void prv_layout_set_frame(LayoutLayer *layout, const GRect *frame) {
  layer_set_frame((Layer *)layout, frame);
}

static void prv_finish_animation(SwapLayer *swap_layer) {
  Animation *animation = swap_layer->animation;
  if (animation_is_scheduled(animation)) {
    // Whoever finishes it starts the next motion, not a held button.
    const bool in_step = swap_layer->hold.in_step;
    swap_layer->hold.in_step = true;
    animation_set_elapsed(animation, animation_get_duration(animation, true, true));
    animation_unschedule(animation);
    swap_layer->hold.in_step = in_step;
  }
  swap_layer->scrolling = false;
}

//! Calls the remove callback and deinits the layout.
//! The client is responsible to removing any data that is associated with the layout, including
//! itself
static void prv_remove_old_layout(SwapLayer *swap_layer, LayoutLayer *layout) {
  if (!layout) {
    return;
  }

  layer_remove_from_parent((Layer *)layout);

  if (layout && swap_layer->callbacks.layout_removed_handler) {
    swap_layer->callbacks.layout_removed_handler(swap_layer, layout, swap_layer->context);
  }
}

//! Calls the fetch layout callback and gets the layout that is at an offset of rel_change from the
//! current index (which is stored and kept track of by the client)
static LayoutLayer *prv_fetch_next_layout(SwapLayer *swap_layer, int8_t rel_change) {
  if (swap_layer->callbacks.get_layout_handler) {
    LayoutLayer *layout =
        swap_layer->callbacks.get_layout_handler(swap_layer, rel_change, swap_layer->context);
    // if there is no layout, we return NULL
    if (!layout) {
      return nullptr;
    }

    // Calculate the size of the layout we were given and set the frame.
    GSize size = layout_get_size(graphics_context_get_current_context(), layout);
    prv_layout_set_frame(layout, &(GRect){
                                   .size.w = PBL_MAX(swap_layer->layer.frame.size.w, size.w),
                                   .size.h = PBL_MAX(swap_layer->layer.frame.size.h, size.h),
                                 });
    return layout;
  }
  return nullptr;
}

static void prv_announce_layout_will_appear(SwapLayer *swap_layer, LayoutLayer *layout) {
  swap_layer->swap_in_progress = true;
  if (swap_layer->callbacks.layout_will_appear_handler) {
    return swap_layer->callbacks.layout_will_appear_handler(swap_layer, layout,
                                                            swap_layer->context);
  }
}

static void prv_announce_layout_did_appear(SwapLayer *swap_layer, LayoutLayer *layout,
                                           int8_t rel_change) {
  swap_layer->swap_in_progress = false;
  if (swap_layer->callbacks.layout_did_appear_handler) {
    swap_layer->callbacks.layout_did_appear_handler(swap_layer, layout, rel_change,
                                                    swap_layer->context);
  }
}

static void prv_update_colors(SwapLayer *swap_layer, GColor bg_color, bool status_bar_filled) {
  if (swap_layer->callbacks.update_colors_handler) {
    swap_layer->callbacks.update_colors_handler(swap_layer, bg_color, status_bar_filled,
                                                swap_layer->context);
  }
}

static void prv_announce_interaction(SwapLayer *swap_layer) {
  if (swap_layer->callbacks.interaction_handler) {
    swap_layer->callbacks.interaction_handler(swap_layer, swap_layer->context);
  }
}

static void prv_refresh_next_layer(SwapLayer *swap_layer) {
  prv_remove_old_layout(swap_layer, swap_layer->next);
  LayoutLayer *new_next = prv_fetch_next_layout(swap_layer, +1);

  swap_layer->next = new_next;

  if (new_next) {
    const GRect *current_frame = &swap_layer->current->layer.frame;
    GRect next_frame = swap_layer->next->layer.frame;

    next_frame.origin.y = current_frame->origin.y + current_frame->size.h;
    prv_layout_set_frame(swap_layer->next, &next_frame);
    layer_add_child(&swap_layer->layer, (Layer *)swap_layer->next);
    layer_insert_below_sibling((Layer *)swap_layer->next, &swap_layer->arrow_layer.layer);
  }
}

//////////////////////////
// STATUS LAYER FUNCTIONS
//////////////////////////

static void prv_update_status_bar_color(SwapLayer *swap_layer) {
  // Set initial values to assume failure
  bool color_status_bar = false;
  GColor bg_color = GColorClear;

  // If there is a current layout, then fetch the color and whether or not the status bar
  // should be colored
  if (swap_layer->current) {
    const GRect *cur_frame = &swap_layer->current->layer.frame;
#if PBL_RECT
    // status_bar can be off, so detect within range, updated a frame later.
    color_status_bar = PBL_IN_RANGE(cur_frame->origin.y, -(3 * LAYOUT_BANNER_HEIGHT_RECT / 2) - 1,
                                    LAYOUT_BANNER_HEIGHT_RECT / 2 - 1);
#else
    color_status_bar = PBL_IN_RANGE(cur_frame->origin.y, -96, 66) || swap_layer->swap_in_progress;
#endif
    bg_color = layout_get_colors(swap_layer->current)->bg_color;

#if PBL_BW
    // On a black and white display, use the appropriate status bar color based on design style
    const bool use_alternative_design = alerts_preferences_get_notification_alternative_design();
    bg_color = use_alternative_design ? GColorBlack : GColorLightGray;
#endif
  }

  prv_update_colors(swap_layer, bg_color, color_status_bar);
}

/////////////////////////
// ARROW LAYER FUNCTIONS
/////////////////////////

//! Moves the arrow with the current layout: it rests at the bottom of the layout's first screen,
//! arrives with it on a swap and slides off the bottom as soon as it is scrolled.
static void prv_update_arrow(SwapLayer *swap_layer) {
  bool hide_it = true;
  GRect arrow_frame = swap_layer->arrow_layer.layer.frame;
  const GRect *layer_frame = &swap_layer->layer.frame;
  arrow_frame.origin.y = layer_frame->size.h - LAYOUT_ARROW_HEIGHT;

  if (swap_layer->current) {
    const GRect *cur_frame = &swap_layer->current->layer.frame;
    const bool viewing_entire_notif = (cur_frame->size.h == layer_frame->size.h);
    if (!viewing_entire_notif || swap_layer->next) {
      // The layout's own top while it is below the top, else how far it has been scrolled
      const int16_t shift = PBL_ABS(cur_frame->origin.y);
      arrow_frame.origin.y += shift;
      hide_it = (cur_frame->origin.y < 0 && shift >= LAYOUT_ARROW_HEIGHT) ||
                (arrow_frame.origin.y >= layer_frame->size.h);
    }
  }

  layer_set_frame(&swap_layer->arrow_layer.layer, &arrow_frame);
  layer_set_hidden(&swap_layer->arrow_layer.layer, hide_it);
}

static void prv_arrow_layer_update_proc(Layer *layer, GContext *ctx) {
  ArrowLayer *arrow_layer = (ArrowLayer *)layer;

  const GRect *layer_bounds = &layer->bounds;

#if PBL_RECT
  graphics_context_set_fill_color(ctx, system_theme_get_bg_color());
  graphics_fill_rect(ctx, layer_bounds);
#endif

  GRect arrow_bounds = arrow_layer->arrow_bitmap.bounds;
  const GAlign arrow_alignment = PBL_IF_RECT_ELSE(GAlignTop, GAlignBottom);
  grect_align(&arrow_bounds, layer_bounds, arrow_alignment, false /* clip */);
  const int16_t arrow_nudge_y = PBL_IF_RECT_ELSE(7, -8);
  arrow_bounds.origin.y += arrow_nudge_y;

  // For some reason the down arrow bitmap is drawn as all-black in the test_notification_window
  // unit test on Silk unless we draw it with GCompOpSet, yet this results in the arrow being drawn
  // as all-white on a real Silk watch/QEMU; choosing the compositing mode this way ensures the
  // arrow is drawn correctly in both environments
#if UNITTEST
  const GCompOp compositing_mode = GCompOpSet;
#elif PBL_COLOR
  // The arrow bitmap is black: tint it to stay visible in dark mode
  const GCompOp compositing_mode = system_theme_is_dark_mode() ? GCompOpTint : GCompOpSet;
  graphics_context_set_tint_color(ctx, system_theme_get_fg_color());
#else
  const GCompOp compositing_mode =
      system_theme_is_dark_mode() ? GCompOpAssignInverted : GCompOpAssign;
#endif
  graphics_context_set_compositing_mode(ctx, compositing_mode);

  graphics_draw_bitmap_in_rect(ctx, &arrow_layer->arrow_bitmap, &arrow_bounds);
}

////////////////////////////////
// SWAPPING/ANIMATION FUNCTIONS
////////////////////////////////

//! Returns the current offset of the notification
//! Always returns a positive number.
static int16_t prv_get_current_notification_offset(SwapLayer *swap_layer) {
  Layer *current = (Layer *)swap_layer->current;
  // produce positive representation of the offset of the layer from the top of the frame.
  int16_t offset = -current->frame.origin.y;
  return offset;
}

static void prv_frame_scroll_complete(Animation *animation, bool finished, void *context) {
  if (!finished) {
    GRect end;
    PropertyAnimation *property_animation = (PropertyAnimation *)animation;
    Layer *layer = context;
    property_animation_get_to_grect(property_animation, &end);
    layer_set_frame(layer, &end);
  }
}

//! Creates an animation that moves a layer from its original frame to another frame that is offset
//! dy (change in origin.y).
static Animation *prv_create_anim_frame_scroll(Layer *layer, uint32_t duration, int16_t dy,
                                               ScrollAnimationCurve *curve) {
  if (!layer) {
    return nullptr;
  }

  GRect to_origin = layer->frame;
  to_origin.origin.y += dy;
  Animation *result =
      (Animation *)property_animation_create_layer_frame(layer, nullptr, &to_origin);
  animation_set_handlers(result,
                         (AnimationHandlers){
                           .stopped = prv_frame_scroll_complete,
                         },
                         layer);
  animation_set_duration(result, duration);
  if (!curve) {
    animation_set_curve(result, AnimationCurveEaseOut);
  } else if (curve->swap_curve_kind == ScrollAnimationCurveKind_Curve) {
    animation_set_curve(result, curve->curve);
  } else {
    animation_set_custom_interpolation(result, curve->interpolator);
  }
  return result;
}

static void prv_hold_step(SwapLayer *swap_layer);

// ---------------------------------------------------------------------------------------------
// Hold swap curve

static int32_t s_hold_swap_end_slope;

int32_t swap_layer_hold_swap_progress(int32_t t, int32_t end_slope) {
  // p(t) = a t^3 + b t^2: p(0) = 0 and p'(0) = 0 (from the bump at rest), p(1) = 1, p'(1) = s.
  const int64_t max = ANIMATION_NORMALIZED_MAX;
  const int64_t a = end_slope - 2000;
  const int64_t b = 3000 - end_slope;
  const int64_t t2 = ((int64_t)t * t) / max;
  const int64_t t3 = (t2 * t) / max;
  const int64_t value = (a * t3 + b * t2) / 1000;
  return (int32_t)PBL_CLAMP(value, 0, max);
}

int32_t swap_layer_hold_swap_end_slope(int16_t distance) {
  if (distance <= 0) {
    return 0;
  }
  // Normalized end slope: speed * duration / distance.
  const int32_t slope = (HOLD_SPEED_PX_S * HOLD_SWAP_MS) / distance;
  return PBL_MIN(slope, HOLD_SWAP_MAX_SLOPE);
}

static int64_t prv_hold_swap_interpolate(int32_t normalized, int64_t from, int64_t to) {
  return from + ((to - from) * swap_layer_hold_swap_progress(normalized, s_hold_swap_end_slope)) /
                    ANIMATION_NORMALIZED_MAX;
}

//! The duration and curve of a swap: the stock eased out one, or for a held button one that ends
//! at the hold speed, over distance px.
static uint32_t prv_swap_curve(bool held, int16_t distance, ScrollAnimationCurve *curve) {
  if (!held) {
    *curve = (ScrollAnimationCurve){
      .swap_curve_kind = ScrollAnimationCurveKind_Curve,
      .curve = AnimationCurveEaseOut,
    };
    return SWAP_MS;
  }
  s_hold_swap_end_slope = swap_layer_hold_swap_end_slope(distance);
  *curve = (ScrollAnimationCurve){
    .swap_curve_kind = ScrollAnimationCurveKind_Interpolator,
    .interpolator = prv_hold_swap_interpolate,
  };
  return HOLD_SWAP_MS;
}

// ---------------------------------------------------------------------------------------------
// Swaps

static void prv_swap_up_start(Animation *animation, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  prv_announce_layout_will_appear(swap_layer, swap_layer->previous);
}

static void prv_swap_up_complete(Animation *animation, bool finished, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  if (swap_layer->is_deiniting) {
    return;
  }

  // remove the layout
  prv_remove_old_layout(swap_layer, swap_layer->next);

  // shift all of the indexes
  swap_layer->next = swap_layer->current;
  swap_layer->current = swap_layer->previous;
  swap_layer->previous = nullptr;

  // let the client know we have moved to another layer
  prv_announce_layout_did_appear(swap_layer, swap_layer->current, -1);

  // refresh the layer down below in case we have jumped around in our data model
  prv_refresh_next_layer(swap_layer);

  if (finished) {
    prv_hold_step(swap_layer);
  }
}

static Animation *prv_create_swap_up_animation(SwapLayer *swap_layer, bool full_swap, bool held) {
  int16_t dy;

  const GRect *prev_frame = &swap_layer->previous->layer.frame;
  const GRect *current_frame = &swap_layer->current->layer.frame;

  if (full_swap) {
    dy = prev_frame->size.h - current_frame->origin.y;
  } else {
    // Land on the end of the previous layout with this one peeking below it, also when this one
    // was pulled down past its top.
    dy = swap_layer->layer.frame.size.h - PEEK_PX - current_frame->origin.y;
  }

  ScrollAnimationCurve curve;
  const uint32_t duration = prv_swap_curve(held, dy, &curve);
  Animation *prev_down =
      prv_create_anim_frame_scroll((Layer *)swap_layer->previous, duration, dy, &curve);
  Animation *current_down =
      prv_create_anim_frame_scroll((Layer *)swap_layer->current, duration, dy, &curve);
  Animation *next_down =
      prv_create_anim_frame_scroll((Layer *)swap_layer->next, duration, dy, &curve);
  return animation_spawn_create(prev_down, current_down, next_down, nullptr);
}

static bool prv_setup_swap_up(SwapLayer *swap_layer) {
  LayoutLayer *new_previous = prv_fetch_next_layout(swap_layer, -1);

  // if there is no layout to swap up to, abort
  if (!new_previous) {
    return false;
  }

  swap_layer->previous = new_previous;

  GRect prev_frame = swap_layer->previous->layer.frame;
  const GRect *current_frame = &swap_layer->current->layer.frame;

  // set relative offset for the previous layer
  prev_frame.origin.y = -prev_frame.size.h + current_frame->origin.y;
  prv_layout_set_frame(swap_layer->previous, &prev_frame);
  layer_add_child(&swap_layer->layer, (Layer *)swap_layer->previous);
  layer_insert_below_sibling((Layer *)swap_layer->previous, &swap_layer->arrow_layer.layer);

  return true;
}

static void prv_swap_down_start(Animation *animation, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  prv_announce_layout_will_appear(swap_layer, swap_layer->current);
}

static void prv_swap_down_complete(Animation *animation, bool finished, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  if (swap_layer->is_deiniting) {
    return;
  }

  // remove the previous layer
  prv_remove_old_layout(swap_layer, swap_layer->previous);
  swap_layer->previous = nullptr;

  // let the client know we have moved to another layer
  prv_announce_layout_did_appear(swap_layer, swap_layer->current, +1);

  // refresh the layer down below in case we have jumped around in our data model
  prv_refresh_next_layer(swap_layer);

  if (finished) {
    prv_hold_step(swap_layer);
  }
}

static Animation *prv_create_swap_down_animation(SwapLayer *swap_layer, bool held) {
  // Compute the animation distance
  const GRect *prev_frame = &swap_layer->previous->layer.frame;

  int16_t dy = -(prev_frame->origin.y + prev_frame->size.h);
  ScrollAnimationCurve curve;
  const uint32_t duration = prv_swap_curve(held, -dy, &curve);
  Animation *prev_up =
      prv_create_anim_frame_scroll((Layer *)swap_layer->previous, duration, dy, &curve);
  Animation *current_up =
      prv_create_anim_frame_scroll((Layer *)swap_layer->current, duration, dy, &curve);
  // next might return NULL if there is no next. That's OK since animation_spawn CAN take two NULL's
  // and perform the creation correctly.
  Animation *next_up =
      prv_create_anim_frame_scroll((Layer *)swap_layer->next, duration, dy, &curve);
  return animation_spawn_create(prev_up, current_up, next_up, nullptr);
}

static bool prv_setup_swap_down(SwapLayer *swap_layer) {
  // if there is no layout to swap down to, return
  if (!swap_layer->next) {
    return false;
  }

  // shift all of the indexes. No need to fetch the new next layout, since we will do that
  // in the swap down complete
  swap_layer->previous = swap_layer->current;
  swap_layer->current = swap_layer->next;
  swap_layer->next = nullptr;

  return true;
}

// ---------------------------------------------------------------------------------------------
// Scrolls

//! Put the current layout at offset (not clamped) with the next one right under it.
static void prv_set_offset(SwapLayer *swap_layer, int16_t offset) {
  GRect current_frame = swap_layer->current->layer.frame;
  current_frame.origin.y = -offset;
  prv_layout_set_frame(swap_layer->current, &current_frame);

  // Pull the next layout right under the current one so its peek tracks the finger.
  if (swap_layer->next) {
    GRect next_frame = swap_layer->next->layer.frame;
    next_frame.origin.y = current_frame.origin.y + current_frame.size.h;
    prv_layout_set_frame(swap_layer->next, &next_frame);
  }
}

static void prv_scroll_update(Animation *animation, const AnimationProgress progress) {
  SwapLayer *swap_layer = animation_get_context(animation);
  if (!swap_layer->current) {
    return;
  }
  const int32_t range = swap_layer->scroll_target - swap_layer->scroll_from;
  prv_set_offset(swap_layer, (int16_t)(swap_layer->scroll_from +
                                       (range * (int32_t)progress) / ANIMATION_NORMALIZED_MAX));
}

static void prv_scroll_stopped(Animation *animation, bool finished, void *context) {
  SwapLayer *swap_layer = context;
  if (animation != swap_layer->animation) {
    return;
  }
  swap_layer->scrolling = false;
  if (finished && !swap_layer->is_deiniting) {
    prv_hold_step(swap_layer);
  }
}

//! Scroll the current (and next) layout by dy, in origin.y units, over duration_ms.
static void prv_scroll_for(SwapLayer *swap_layer, int16_t dy, AnimationCurve curve,
                           uint32_t duration_ms) {
  if (dy == 0 || !swap_layer->current) {
    return;
  }
  static const AnimationImplementation s_scroll_implementation = {
    .update = prv_scroll_update,
  };
  Animation *animation = animation_create();
  if (!animation) {
    return;
  }
  swap_layer->scroll_from = prv_get_current_notification_offset(swap_layer);
  swap_layer->scroll_target = (int16_t)(swap_layer->scroll_from - dy);
  animation_set_implementation(animation, &s_scroll_implementation);
  animation_set_duration(animation, duration_ms);
  animation_set_curve(animation, curve);
  animation_set_handlers(animation,
                         (AnimationHandlers){
                           .stopped = prv_scroll_stopped,
                         },
                         swap_layer);
  swap_layer->animation = animation;
  swap_layer->scrolling = true;
  animation_schedule(animation);
}

static void prv_scroll(SwapLayer *swap_layer, int16_t dy, AnimationCurve curve) {
  prv_scroll_for(swap_layer, dy, curve, SCROLL_MS);
}

static bool prv_is_scrolling(SwapLayer *swap_layer) {
  return swap_layer->scrolling && animation_is_scheduled(swap_layer->animation);
}

//! Stop a running scroll where it has got to (finishing a swap instead).
static void prv_stop_scroll_in_place(SwapLayer *swap_layer) {
  if (!prv_is_scrolling(swap_layer)) {
    prv_finish_animation(swap_layer);
    return;
  }
  const int16_t offset = prv_get_current_notification_offset(swap_layer);
  swap_layer->scrolling = false;
  animation_unschedule(swap_layer->animation);
  prv_set_offset(swap_layer, offset);
}

// scroll to top of current notification
static void prv_scroll_to_top(SwapLayer *swap_layer) {
  int16_t offset = prv_get_current_notification_offset(swap_layer);
  prv_scroll(swap_layer, offset, AnimationCurveEaseOut);
}

// scroll to bottom of current notification
static void prv_scroll_to_bottom(SwapLayer *swap_layer) {
  const GRect *current_frame = &swap_layer->current->layer.frame;
  const GRect *layer_frame = &swap_layer->layer.frame;
  // don't allow scrolling up
  if (current_frame->size.h < layer_frame->size.h) {
    prv_scroll(swap_layer, -current_frame->size.h - current_frame->origin.y + layer_frame->size.h,
               AnimationCurveEaseOut);
  }
}

//! Attempt to swap up or down. 'full_swap' is for swapping up, and it is used when wanting to go
//! to the top of the previous notification. 'held': a held button, carrying on at the hold speed.
static bool prv_attempt_swap_at(SwapLayer *swap_layer, ScrollDirection direction, bool full_swap,
                                bool held) {
  prv_finish_animation(swap_layer);

  Animation *animation;
  if (direction == ScrollDirectionDown) {
    if (!prv_setup_swap_down(swap_layer)) {
      // failed to swap, just scroll
      prv_scroll_to_bottom(swap_layer);
      return false;
    }

    animation = prv_create_swap_down_animation(swap_layer, held);
  } else { // ScrollDirectionUp
    if (!prv_setup_swap_up(swap_layer)) {
      // failed to swap, just scroll
      prv_scroll_to_top(swap_layer);
      return false;
    }

    animation = prv_create_swap_up_animation(swap_layer, full_swap, held);
  }

  animation_set_handlers(
      animation,
      (AnimationHandlers){
        .started = (direction == ScrollDirectionDown) ? prv_swap_down_start : prv_swap_up_start,
        .stopped =
            (direction == ScrollDirectionDown) ? prv_swap_down_complete : prv_swap_up_complete,
      },
      swap_layer);
  swap_layer->animation = animation;
  swap_layer->scrolling = false;
  animation_schedule(animation);
  return true;
}

static bool prv_attempt_swap(SwapLayer *swap_layer, ScrollDirection direction, bool full_swap) {
  return prv_attempt_swap_at(swap_layer, direction, full_swap, false /* held */);
}

//! Computes the amount the current layer frame can scroll until a swap is necessary.
//! Always returns a positive number
static int16_t prv_get_max_scroll_dy(SwapLayer *swap_layer) {
  int16_t max_dy = swap_layer->current->layer.frame.size.h - swap_layer->layer.frame.size.h;

  // we are peeking the next notification if we have a next.
  if (swap_layer->next != nullptr) {
    max_dy += PEEK_PX;
  }

  max_dy = PBL_MAX(max_dy, 0);
  return max_dy;
}

PBL_T_STATIC void prv_attempt_scroll(SwapLayer *swap_layer, ScrollDirection direction) {
  // Never drop a press because something is still moving: jump to its end and act on that.
  prv_finish_animation(swap_layer);

  int16_t offset = prv_get_current_notification_offset(swap_layer);
  int16_t max_dy = prv_get_max_scroll_dy(swap_layer);

  // distance to scroll
  int16_t dy = 0;

  // check if we are going to go off screen, if so get a new layer and set it up, then animate.
  switch (direction) {
    case ScrollDirectionUp: {
      if (offset <= 0) {
        // we are at the topmost part of the notification, swap up
        prv_attempt_swap(swap_layer, direction, false /* full_swap */);
        return;
      } else if ((offset - FUDGE_PX) < SCROLL_PX) {
        // we have a little room between the top of notification and top of frame
        dy = offset;
      } else {
        // so much roooooooooom, scroll up normal amount
        dy = SCROLL_PX;
      }
      break;
    }
    case ScrollDirectionDown: {
      if (offset >= max_dy) {
        // if we have already scrolled our maximum amount for this notification, we should swap
        prv_attempt_swap(swap_layer, direction, false /* full_swap */);
        return;
      }

      if ((max_dy - offset - FUDGE_PX) < SCROLL_PX) {
        // if we can scroll a little more, let's do that instead.
        // negative because we need to scroll down
        dy = -(max_dy - offset);
      } else if (offset == 0) {
        // scroll only the banner when it's the first scroll on a notification
        dy = -INITIAL_SCROLL_PX;
      } else {
        dy = -SCROLL_PX;
      }
      break;
    }
    default:
      return;
  }

  prv_scroll(swap_layer, dy, AnimationCurveEaseOut);
}

///////////////////////
// PRESS AND HOLD
///////////////////////

static void prv_hold_cancel_timer(SwapLayer *swap_layer) {
  if (swap_layer->hold.timer != EVENTED_TIMER_INVALID_ID) {
    evented_timer_cancel(swap_layer->hold.timer);
    swap_layer->hold.timer = EVENTED_TIMER_INVALID_ID;
  }
}

void swap_layer_stop_hold(SwapLayer *swap_layer) {
  prv_hold_cancel_timer(swap_layer);
  swap_layer->hold.active = false;
  swap_layer->hold.bumped = false;
  swap_layer->hold.direction = 0;
}

static void prv_hold_timer_fired(void *context) {
  SwapLayer *swap_layer = context;
  swap_layer->hold.timer = EVENTED_TIMER_INVALID_ID;
  prv_hold_step(swap_layer);
}

static void prv_hold_wait(SwapLayer *swap_layer, uint32_t ms) {
  prv_hold_cancel_timer(swap_layer);
  swap_layer->hold.timer = evented_timer_register(ms, false, prv_hold_timer_fired, swap_layer);
}

static void prv_hold_step(SwapLayer *swap_layer) {
  if (!swap_layer->hold.active || swap_layer->hold.in_step || !swap_layer->current) {
    return;
  }
  if (animation_is_scheduled(swap_layer->animation) && !swap_layer->scrolling) {
    // A swap is playing: carry on once it lands.
    return;
  }

  swap_layer->hold.in_step = true;
  // Pick up from wherever the press has got to, at a steady speed.
  prv_stop_scroll_in_place(swap_layer);

  const int8_t direction = swap_layer->hold.direction;
  const int16_t offset = prv_get_current_notification_offset(swap_layer);
  const int16_t end = (direction > 0) ? prv_get_max_scroll_dy(swap_layer) : 0;
  if (offset != end) {
    swap_layer->hold.bumped = false;
    const uint32_t duration_ms = PBL_MAX((PBL_ABS(end - offset) * 1000) / HOLD_SPEED_PX_S, 1);
    prv_scroll_for(swap_layer, offset - end, AnimationCurveLinear, duration_ms);
  } else if (!swap_layer->hold.bumped) {
    // Rest at the end of the layout for a moment, so it can be read or acted on.
    swap_layer->hold.bumped = true;
    prv_hold_wait(swap_layer, HOLD_BUMP_MS);
  } else {
    swap_layer->hold.bumped = false;
    prv_attempt_swap_at(swap_layer, (direction > 0) ? ScrollDirectionDown : ScrollDirectionUp,
                        false /* full_swap */, true /* held */);
  }
  swap_layer->hold.in_step = false;
}

static void prv_hold_begin(void *context) {
  SwapLayer *swap_layer = context;
  swap_layer->hold.timer = EVENTED_TIMER_INVALID_ID;
  if (swap_layer->hold.direction == 0 || !swap_layer->current) {
    return;
  }
  swap_layer->hold.active = true;
  swap_layer->hold.bumped = false;
  // Remember where the press was heading, so a press that only just became a hold goes as far.
  swap_layer->hold.press_target = prv_is_scrolling(swap_layer)
                                      ? swap_layer->scroll_target
                                      : prv_get_current_notification_offset(swap_layer);
  prv_hold_step(swap_layer);
}

static void prv_hold_release(SwapLayer *swap_layer) {
  const bool was_active = swap_layer->hold.active;
  const int8_t direction = swap_layer->hold.direction;
  swap_layer_stop_hold(swap_layer);
  if (!was_active || !swap_layer->current || !prv_is_scrolling(swap_layer)) {
    // A plain press, or a swap that finishes on its own.
    return;
  }

  prv_stop_scroll_in_place(swap_layer);
  const int16_t offset = prv_get_current_notification_offset(swap_layer);
  int16_t target = offset + direction * HOLD_RELEASE_PX;
  if ((direction > 0) ? (target < swap_layer->hold.press_target)
                      : (target > swap_layer->hold.press_target)) {
    target = swap_layer->hold.press_target;
  }
  target = PBL_CLAMP(target, 0, prv_get_max_scroll_dy(swap_layer));
  prv_scroll_for(swap_layer, offset - target, AnimationCurveEaseOut, HOLD_RELEASE_MS);
}

///////////////////////
// CLICK HANDLERS
///////////////////////

ScrollDirection prv_direction_for_recognizer(ClickRecognizerRef recognizer) {
  ButtonId button_id = click_recognizer_get_button_id(recognizer);
  return (button_id == BUTTON_ID_UP) ? ScrollDirectionUp : ScrollDirectionDown;
}

static void prv_up_multi_click_handler(ClickRecognizerRef recognizer, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  prv_finish_animation(swap_layer);

  prv_announce_interaction(swap_layer);
  // If our first click caused a swap, then just scroll to the top of the current notification
  int16_t offset = prv_get_current_notification_offset(swap_layer);
  const GRect *layer_frame = &swap_layer->layer.frame;
  const GRect *current_frame = &swap_layer->current->layer.frame;
  if ((current_frame->size.h - layer_frame->size.h - offset) == -PEEK_PX) {
    prv_scroll_to_top(swap_layer);
  } else {
    prv_attempt_swap(swap_layer, ScrollDirectionUp, true /* to_top */);
  }
}

static void prv_down_multi_click_handler(ClickRecognizerRef recognizer, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  prv_finish_animation(swap_layer);

  // If our first click caused a swap, then ignore the double click
  int16_t offset = prv_get_current_notification_offset(swap_layer);
  if (offset != 0) {
    prv_announce_interaction(swap_layer);
    prv_attempt_swap(swap_layer, ScrollDirectionDown, true /* to_top */);
  }
}

static void prv_raw_down_handler(ClickRecognizerRef recognizer, void *context) {
  SwapLayer *swap_layer = (SwapLayer *)context;
  // Ignore any press that isn't the start of a new click pattern
  if (click_number_of_clicks_counted(recognizer) >= 1 || !swap_layer->current) {
    return;
  }
  prv_announce_interaction(swap_layer);
  swap_layer_stop_hold(swap_layer);
  const ScrollDirection direction = prv_direction_for_recognizer(recognizer);
  prv_attempt_scroll(swap_layer, direction);
  swap_layer->hold.direction = (direction == ScrollDirectionUp) ? -1 : 1;
  swap_layer->hold.timer =
      evented_timer_register(HOLD_START_MS, false, prv_hold_begin, swap_layer);
}

static void prv_raw_up_handler(ClickRecognizerRef recognizer, void *context) {
  prv_hold_release((SwapLayer *)context);
}

static void prv_swap_layer_click_config_provider(void *context) {
  // Raw presses avoid the single click delay of multi-click, and drive press and hold
  window_raw_click_subscribe(BUTTON_ID_UP, prv_raw_down_handler, prv_raw_up_handler, context);
  window_multi_click_subscribe(BUTTON_ID_UP, 2, 2, 100, false, prv_up_multi_click_handler);

  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_raw_down_handler, prv_raw_up_handler, context);
  window_multi_click_subscribe(BUTTON_ID_DOWN, 2, 2, 100, false, prv_down_multi_click_handler);

  SwapLayer *swap_layer = context;
  // A press held while the window was covered never got its release
  swap_layer_stop_hold(swap_layer);
#ifdef CONFIG_TOUCH
  // Re-registering here (idempotent) makes the click-config-provider the re-show hook: after a
  // higher modal that released touch is dismissed and this window regains input focus, the swap
  // layer is re-subscribed as the sole Tier-1 touch handler.
  swap_layer_touch_register(swap_layer);
#endif
  if (swap_layer->callbacks.click_config_provider) {
    swap_layer->callbacks.click_config_provider(swap_layer->context);
  }
}

///////////////////////
// MISC FUNCTIONS
///////////////////////

static void prv_swap_layer_update_proc(Layer *layer, GContext *ctx) {
  SwapLayer *swap_layer = (SwapLayer *)layer;
  prv_update_arrow(swap_layer);
  prv_update_status_bar_color(swap_layer);
}

static void prv_swap_layer_reset(SwapLayer *swap_layer) {
  prv_finish_animation(swap_layer);

  prv_remove_old_layout(swap_layer, swap_layer->previous);
  swap_layer->previous = nullptr;

  prv_remove_old_layout(swap_layer, swap_layer->current);
  swap_layer->current = nullptr;

  prv_remove_old_layout(swap_layer, swap_layer->next);
  swap_layer->next = nullptr;
}

void swap_layer_reload_data(SwapLayer *swap_layer) {
  prv_swap_layer_reset(swap_layer);

  LayoutLayer *current = prv_fetch_next_layout(swap_layer, 0);
  if (!current) {
    return;
  }

  GRect current_frame = current->layer.frame;
  current_frame.origin = (GPoint){0, 0};
  prv_layout_set_frame(current, &current_frame);
  layer_add_child(&swap_layer->layer, (Layer *)current);
  layer_insert_below_sibling((Layer *)current, &swap_layer->arrow_layer.layer);
  swap_layer->current = current;

  prv_announce_layout_will_appear(swap_layer, swap_layer->current);
  prv_announce_layout_did_appear(swap_layer, swap_layer->current, 0);

  LayoutLayer *next = prv_fetch_next_layout(swap_layer, +1);

  if (next) {
    GRect next_frame = next->layer.frame;
    next_frame.origin.y = swap_layer->current->layer.frame.size.h;
    prv_layout_set_frame(next, &next_frame);
    layer_add_child(&swap_layer->layer, (Layer *)next);
    layer_insert_below_sibling((Layer *)next, &swap_layer->arrow_layer.layer);
    swap_layer->next = next;
  }

  // A held button carries on into the new layouts
  prv_hold_step(swap_layer);
}

bool swap_layer_attempt_layer_swap(SwapLayer *swap_layer, ScrollDirection direction) {
  return prv_attempt_swap(swap_layer, direction, true /* to_top */);
}

#ifdef CONFIG_TOUCH
///////////////////////
// TIER-1 TOUCH NAVIGATION
//
// A SwapLayer registers itself as a Tier-1 touch widget in swap_layer_init() / the
// click-config-provider and deregisters in swap_layer_deinit() (and, while covered by a higher
// modal, in swap_layer_touch_release()). The recognizers and gesture state are not owned
// per-widget: the unified widget (tap, pan, swipe) set, owned by TouchNavState, drives whichever
// migrated widget the finger lands on through a per-node apply vtable. A SwapLayer supplies that
// vtable (s_swap_touch_nav_ops) at registration; the apply functions below stay the per-swap
// gesture surface (and the unit-test entry points).
//
// A pan drives the content offset live and 1:1 (base captured at pan Start, throttled by the core),
// resisting beyond [0, max_scroll] (rubber band). On liftoff a pull past an edge by more than
// SWAP_OVERPULL_PX swaps to the neighbouring notification; otherwise the content coasts on with the
// release velocity and eases to a stop inside the range. A pan that starts in the bottom edge strip
// moving up jumps to the top of the next notification instead of scrolling. A tap only stops the
// motion; a left swipe emulates SELECT and a right swipe BACK. The notification body may not be
// loaded when the widget registers, so the can_start op gates the gesture on a non-NULL current
// layout (prv_get_current_notification_offset dereferences it), declining the whole gesture until a
// notification is present.
///////////////////////

// Finger travel, in pixels, before a pan counts as a drag. Below this a liftoff is a no-op, so a
// normal scroll to the edge (or a jittery tap) cannot accidentally scroll or swap.
#define DRAG_THRESHOLD_PX 10
// How far the finger must pull PAST an edge to swap to the neighbouring notification. Kept
// separate from DRAG_THRESHOLD_PX so reaching the edge without over-pulling never swaps.
#define SWAP_OVERPULL_PX 56
// Beyond an edge the content follows the finger at this fraction.
#define RUBBER_BAND_DIVISOR 3
// Momentum: how quickly a flick slows down, in px/s^2, and how long it may coast.
#define FLING_DECELERATION 2600
#define FLING_MIN_MS 180
#define FLING_MAX_MS 900
// The release velocity is measured over the last VELOCITY_WINDOW_MS, and is zero if the finger was
// still for more than FLING_STALE_MS before lifting.
#define VELOCITY_WINDOW_MS 120
#define FLING_STALE_MS 90
// A swipe up starting in the bottom EDGE_ZONE_PX of the screen and moving at least EDGE_SWIPE_PX
// jumps to the top of the next notification.
#define EDGE_ZONE_PX 26
#define EDGE_SWIPE_PX 40

// Provided by the owning shells; forward-declared to avoid pulling the kernel modal header in.
struct TouchNavState *modal_manager_get_touch_nav_state(void);

static_assert(sizeof(((SwapLayer *)0)->touch_nav_node) == sizeof(TouchNavWidgetNode),
              "SwapLayer touch_nav_node must match TouchNavWidgetNode layout");

static bool prv_is_app_task(void) {
  return pebble_task_get_current() == PebbleTask_App;
}

static TouchNavState *prv_task_touch_nav_state(void) {
  return prv_is_app_task() ? app_state_get_touch_nav_state() : modal_manager_get_touch_nav_state();
}

// Test seam (declared in swap_layer.h under CONFIG_TOUCH). The unified widget set holds no per-task
// singleton to reset; the touch-nav state is owned by TouchNavState, so this is a no-op kept for
// source compatibility with tests that call it in their setup.
void swap_layer_touch_nav_reset_all(void) {
}

bool swap_layer_touch_is_gesture_target(const SwapLayer *swap_layer) {
  const TouchNavState *state = prv_task_touch_nav_state();
  return state && state->latched_target && state->latched_target->widget == swap_layer;
}

// ---------------------------------------------------------------------------------------------
// Pure gesture math

int16_t swap_layer_touch_rubber_band(int16_t raw, int16_t max_dy) {
  if (raw < 0) {
    return raw / RUBBER_BAND_DIVISOR;
  }
  if (raw > max_dy) {
    return max_dy + (raw - max_dy) / RUBBER_BAND_DIVISOR;
  }
  return raw;
}

int32_t swap_layer_touch_release_velocity(const int16_t *y, const uint32_t *ms, uint8_t count,
                                          uint32_t release_ms) {
  if (count < 2) {
    return 0;
  }
  const uint8_t newest = count - 1;
  if (release_ms - ms[newest] > FLING_STALE_MS) {
    return 0;
  }
  uint8_t oldest = 0;
  while (oldest < newest && ms[newest] - ms[oldest] > VELOCITY_WINDOW_MS) {
    oldest++;
  }
  const int32_t dt = (int32_t)(ms[newest] - ms[oldest]);
  if (dt <= 0) {
    return 0;
  }
  return ((int32_t)(y[newest] - y[oldest]) * 1000) / dt;
}

int16_t swap_layer_touch_fling_target(int16_t offset, int32_t velocity, int16_t max_dy,
                                      uint32_t *duration_ms) {
  const int32_t speed = PBL_ABS(velocity);
  int32_t travel = (speed * speed) / (2 * FLING_DECELERATION);
  // Finger moving down (positive velocity) scrolls back toward the top.
  if (velocity > 0) {
    travel = -travel;
  }
  if (duration_ms) {
    *duration_ms = PBL_CLAMP((uint32_t)((speed * 1000) / FLING_DECELERATION), FLING_MIN_MS,
                             FLING_MAX_MS);
  }
  return (int16_t)PBL_CLAMP((int32_t)offset + travel, 0, max_dy);
}

bool swap_layer_touch_is_edge_swipe(int16_t start_y, int16_t total_dy, int16_t screen_h) {
  return (start_y >= screen_h - EDGE_ZONE_PX) && (total_dy < 0);
}

// ---------------------------------------------------------------------------------------------
// The scroll point and the two-threshold liftoff decision

void swap_layer_touch_scroll_by(SwapLayer *swap_layer, int16_t dy) {
  if (!swap_layer || !swap_layer->current) {
    return;
  }
  prv_finish_animation(swap_layer);

  const int16_t offset = prv_get_current_notification_offset(swap_layer); // >= 0
  const int16_t max_dy = prv_get_max_scroll_dy(swap_layer);               // >= 0
  // offset == -origin.y; dy is the requested change to origin.y, so the new offset is offset - dy.
  prv_set_offset(swap_layer, PBL_CLAMP(offset - dy, 0, max_dy));

  // Refresh the auto-close timer: without this a long notification closes under the finger
  // mid-read.
  prv_announce_interaction(swap_layer);
}

SwapTouchLiftoffAction swap_layer_touch_liftoff_action(int16_t base_offset, int16_t delta_y,
                                                       int16_t max_dy) {
  if (PBL_ABS(delta_y) < DRAG_THRESHOLD_PX) {
    // Sub-threshold: not a drag, so neither scroll nor swap.
    return SwapTouchLiftoff_None;
  }
  // Content-scroll convention: finger up (delta_y < 0) scrolls into the content (offset increases).
  const int16_t requested_offset = base_offset - delta_y;
  if (requested_offset < -SWAP_OVERPULL_PX) {
    return SwapTouchLiftoff_SwapPrev; // over-pull at the top (finger down) -> previous notification
  }
  if (requested_offset > max_dy + SWAP_OVERPULL_PX) {
    return SwapTouchLiftoff_SwapNext; // over-pull at the bottom (finger up) -> next notification
  }
  return SwapTouchLiftoff_Settle;
}

int16_t swap_layer_touch_settle_offset(int16_t base_offset, int16_t delta_y, int16_t max_dy,
                                       int16_t page_h) {
  int16_t target = PBL_CLAMP(base_offset - delta_y, 0, max_dy);
  if (page_h > 0) {
    // Land on the nearest page boundary, never past the last one (max_dy may include the
    // next-notification peek, which is not a rest position).
    const int16_t last_page = (int16_t)((max_dy / page_h) * page_h);
    target = (int16_t)(((target + page_h / 2) / page_h) * page_h);
    target = PBL_MIN(target, last_page);
  }
  return target;
}

static uint32_t prv_now_ms(void) {
  return (uint32_t)((sys_get_ticks() * 1000) / PBL_TICK_HZ);
}

static void prv_touch_add_sample(SwapLayer *swap_layer, int16_t y) {
  if (swap_layer->touch.count == SWAP_LAYER_TOUCH_SAMPLES) {
    memmove(&swap_layer->touch.y[0], &swap_layer->touch.y[1],
            sizeof(swap_layer->touch.y[0]) * (SWAP_LAYER_TOUCH_SAMPLES - 1));
    memmove(&swap_layer->touch.ms[0], &swap_layer->touch.ms[1],
            sizeof(swap_layer->touch.ms[0]) * (SWAP_LAYER_TOUCH_SAMPLES - 1));
    swap_layer->touch.count--;
  }
  swap_layer->touch.y[swap_layer->touch.count] = y;
  swap_layer->touch.ms[swap_layer->touch.count] = prv_now_ms();
  swap_layer->touch.count++;
}

// Apply a live drag: move to base_offset - delta_y, rubber-banded beyond the edges. Sub-threshold
// movement is a no-op so the notification does not creep on a tap.
static void prv_swap_touch_apply_drag(SwapLayer *swap_layer, int16_t base_offset, int16_t delta_y) {
  if (!swap_layer->current) {
    // No notification loaded yet (registration can happen before the layout loads).
    return;
  }
  const int16_t count = swap_layer->touch.count;
  if (count == 0 || swap_layer->touch.y[count - 1] != delta_y) {
    prv_touch_add_sample(swap_layer, delta_y);
  }
  if (swap_layer->touch.edge || PBL_ABS(delta_y) < DRAG_THRESHOLD_PX) {
    return;
  }
  prv_finish_animation(swap_layer);
  const int16_t max_dy = prv_get_max_scroll_dy(swap_layer);
  prv_set_offset(swap_layer, swap_layer_touch_rubber_band(base_offset - delta_y, max_dy));
  prv_announce_interaction(swap_layer);
}

//! Coast from wherever the content is (possibly past an edge) with the release velocity.
static void prv_swap_touch_fling(SwapLayer *swap_layer, int32_t velocity) {
  const int16_t cur = prv_get_current_notification_offset(swap_layer);
  const int16_t max_dy = prv_get_max_scroll_dy(swap_layer);
  uint32_t duration_ms;
  const int16_t target = swap_layer_touch_fling_target(cur, velocity, max_dy, &duration_ms);
  prv_scroll_for(swap_layer, cur - target, AnimationCurveEaseOut, duration_ms);
}

// Liftoff: swap to a neighbour on over-pull, otherwise coast to a stop within the range.
static void prv_swap_touch_liftoff(SwapLayer *swap_layer, int16_t base_offset, int16_t delta_y) {
  if (!swap_layer->current) {
    return;
  }
  const uint32_t now = prv_now_ms();
  const int16_t count = swap_layer->touch.count;
  if (count == 0 || swap_layer->touch.y[count - 1] != delta_y) {
    prv_touch_add_sample(swap_layer, delta_y);
    swap_layer->touch.ms[swap_layer->touch.count - 1] = now;
  }

  if (swap_layer->touch.edge) {
    swap_layer->touch.edge = false;
    const TouchNavState *state = prv_task_touch_nav_state();
    const int16_t total_dy =
        (state && state->widget_pan) ? pan_recognizer_get_total_delta(state->widget_pan).y : delta_y;
    if (total_dy <= -EDGE_SWIPE_PX) {
      prv_attempt_swap(swap_layer, ScrollDirectionDown, true /* full_swap */);
    }
    prv_announce_interaction(swap_layer);
    return;
  }

  const int16_t max_dy = prv_get_max_scroll_dy(swap_layer);
  bool swapped = false;
  switch (swap_layer_touch_liftoff_action(base_offset, delta_y, max_dy)) {
    case SwapTouchLiftoff_SwapPrev:
      swapped = prv_attempt_swap(swap_layer, ScrollDirectionUp, false /* full_swap */);
      break;
    case SwapTouchLiftoff_SwapNext:
      swapped = prv_attempt_swap(swap_layer, ScrollDirectionDown, true /* full_swap */);
      break;
    case SwapTouchLiftoff_Settle:
    case SwapTouchLiftoff_None:
      break;
  }
  if (!swapped) {
    // A failed swap may have started a scroll to the edge; coast from where the finger left it.
    prv_stop_scroll_in_place(swap_layer);
    const int32_t velocity = swap_layer_touch_release_velocity(
        swap_layer->touch.y, swap_layer->touch.ms, swap_layer->touch.count, now);
    prv_swap_touch_fling(swap_layer, velocity);
  }
  prv_announce_interaction(swap_layer);
}

// ---------------------------------------------------------------------------------------------
// Button emulation for tap / swipe, mirroring the Tier-2 bridge through the task's TouchNavOps.

static void prv_swap_touch_emit(SwapLayer *swap_layer, ButtonId button) {
  const TouchNavState *state = prv_task_touch_nav_state();
  if (!state || !state->ops) {
    return;
  }
  const TouchNavOps *ops = state->ops;
  // A gesture that lands mid-transition is dropped, matching the bridge.
  if (ops->is_animating && ops->is_animating(ops->ctx)) {
    return;
  }
  if (button == BUTTON_ID_BACK && !(ops->top_overrides_back && ops->top_overrides_back(ops->ctx))) {
    // BACK on a window with no back handler pops the stack rather than feeding the click
    // recognizer.
    if (ops->pop_top) {
      ops->pop_top(ops->ctx);
    }
  } else if (ops->emit_button) {
    ops->emit_button(ops->ctx, button);
  }
  prv_announce_interaction(swap_layer);
}

// ---------------------------------------------------------------------------------------------
// Unified widget-set ops
//
// Thin void*->SwapLayer* wrappers over the apply functions above. The unified widget set (owned by
// TouchNavState) drives the swap layer through these; direct assignment of the apply functions
// would not compile because their first parameter is SwapLayer*, not void*. Swap's content axis is
// vertical, so the GPoint base/delta carried by the vtable use only the .y component.

// The notification body may not be loaded yet (registration can happen before the layout loads) and
// prv_get_current_notification_offset dereferences current, so gate the whole gesture on a present
// layout. A declined gesture never Starts the pan, so no base offset is latched or applied.
static bool prv_swap_ops_can_start(void *w) {
  return ((SwapLayer *)w)->current != nullptr;
}

static void prv_swap_ops_touchdown(void *w) {
  // Catch-to-stop: a finger on the screen stops a coasting card where it is, and a held button.
  SwapLayer *swap_layer = w;
  swap_layer_stop_hold(swap_layer);
  swap_layer->touch.count = 0;
  swap_layer->touch.edge = false;
  if (swap_layer->current) {
    prv_stop_scroll_in_place(swap_layer);
  }
}

static void prv_swap_ops_pan_started(void *w) {
  SwapLayer *swap_layer = w;
  // Stop any running swap/scroll animation so the finger takes over, and refresh the auto-close
  // timer so a long notification cannot close under the finger while it is being dragged. The base
  // offset is latched by the core via get_base_offset next.
  prv_stop_scroll_in_place(swap_layer);
  swap_layer->touch.count = 0;
  prv_touch_add_sample(swap_layer, 0);
  const TouchNavState *state = prv_task_touch_nav_state();
  if (state && state->widget_pan) {
    const GPoint start = pan_recognizer_get_touch_down_point(state->widget_pan);
    const GPoint total = pan_recognizer_get_total_delta(state->widget_pan);
    swap_layer->touch.edge = swap_layer_touch_is_edge_swipe(start.y, total.y, PBL_DISPLAY_HEIGHT);
  }
  prv_announce_interaction(swap_layer);
}

static GPointReturn prv_swap_ops_get_base_offset(void *w) {
  // Vertical-only: carry the notification scroll offset in .y (current is non-NULL, gated by
  // can_start above).
  return GPoint(0, prv_get_current_notification_offset((SwapLayer *)w));
}

static void prv_swap_ops_pan_update(void *w, GPoint base, GPoint delta) {
  prv_swap_touch_apply_drag((SwapLayer *)w, base.y, delta.y);
}

static void prv_swap_ops_pan_snap(void *w, GPoint base, GPoint final_delta, GPoint velocity) {
  // The release velocity comes from the swap layer's own samples, which know when the finger
  // stopped moving.
  (void)velocity;
  prv_swap_touch_liftoff((SwapLayer *)w, base.y, final_delta.y);
}

static void prv_swap_ops_pan_cancel(void *w) {
  // Settle back inside the range with no swap. Per the pan engine this does not fire on
  // pre-emption (the pan recognizer never emits Cancelled there); it is reached only on an
  // explicit mid-gesture cancel.
  SwapLayer *swap_layer = w;
  if (!swap_layer->current) {
    return;
  }
  swap_layer->touch.edge = false;
  const int16_t cur = prv_get_current_notification_offset(swap_layer);
  const int16_t max_dy = prv_get_max_scroll_dy(swap_layer);
  const int16_t target = swap_layer_touch_settle_offset(cur, 0, max_dy, 0 /* page_h */);
  prv_scroll(swap_layer, cur - target, AnimationCurveEaseOut);
}

static void prv_swap_ops_tap(void *w, GPoint point_on_screen) {
  // A tap on the notification body only stops the motion (see touchdown). The action menu is
  // opened by a left swipe (prv_swap_ops_swipe -> SELECT); a bare tap must not open it by accident.
  // The handler is kept in the vtable (rather than NULL) so the tap gesture is still consumed here
  // instead of leaking to another handler.
  (void)w;
  (void)point_on_screen;
}

static void prv_swap_ops_swipe(void *w, SwipeDirection dir) {
  SwapLayer *swap_layer = w;
  // As with tap: no notification loaded -> the gesture is inert.
  if (!swap_layer->current) {
    return;
  }
  // Called only for horizontal swipes (the unified set masks vertical swipes into pans): swiping
  // right = BACK, left = SELECT.
  if (dir == SwipeDirection_Right) {
    prv_swap_touch_emit(swap_layer, BUTTON_ID_BACK);
  } else if (dir == SwipeDirection_Left) {
    prv_swap_touch_emit(swap_layer, BUTTON_ID_SELECT);
  }
}

static const TouchNavWidgetOps s_swap_touch_nav_ops = {
  .can_start = prv_swap_ops_can_start,
  .touchdown = prv_swap_ops_touchdown,
  .pan_started = prv_swap_ops_pan_started,
  .get_base_offset = prv_swap_ops_get_base_offset,
  .pan_update = prv_swap_ops_pan_update,
  .pan_snap = prv_swap_ops_pan_snap,
  .pan_cancel = prv_swap_ops_pan_cancel,
  .tap = prv_swap_ops_tap,
  .swipe = prv_swap_ops_swipe,
};

// ---------------------------------------------------------------------------------------------
// Registration lifecycle

void swap_layer_touch_register(SwapLayer *swap_layer) {
  if (!swap_layer || swap_layer->touch_registered) {
    return;
  }
  TouchNavState *state = prv_task_touch_nav_state();
  if (!state || !state->manager) {
    // Touch is not available on this task (nav disabled / no manager): stay on the button path.
    return;
  }

  // Register the swap layer as a migrated Tier-1 widget: the unified widget set drives it through
  // s_swap_touch_nav_ops. The registry add dedupes by address and re-applies the ops/layer, so a
  // repeated init/re-show on the same swap layer (no intervening deinit) keeps routing to and
  // driving it.
  touch_nav_registry_add(state, TouchNavWidgetType_Swap,
                         (TouchNavWidgetNode *)&swap_layer->touch_nav_node, &swap_layer->layer,
                         &s_swap_touch_nav_ops, swap_layer);
  swap_layer->touch_registered = true;
}

void swap_layer_touch_deregister(SwapLayer *swap_layer) {
  if (!swap_layer || !swap_layer->touch_registered) {
    return;
  }
  TouchNavState *state = prv_task_touch_nav_state();
  if (state) {
    // If this swap layer is the live gesture target and is about to go away under a live window,
    // cancel the gesture with NO client callbacks so a settle cannot reach freed state.
    // touch_nav_registry_remove clears the latched target BEFORE we cancel (its UAF hook), so the
    // resulting Cancelled dispatch finds no target and re-enters nothing.
    const bool was_target = swap_layer_touch_is_gesture_target(swap_layer);
    // Idempotent: removing a node that is not in the registry (double deinit) is a safe no-op.
    touch_nav_registry_remove(state, TouchNavWidgetType_Swap,
                              (TouchNavWidgetNode *)&swap_layer->touch_nav_node);
    if (was_target && state->manager) {
      recognizer_manager_cancel_and_reset(state->manager);
    }
  }
  swap_layer->touch_registered = false;
}

void swap_layer_touch_release(SwapLayer *swap_layer) {
  // Covered by a higher modal: deregister from the Tier-1 registry (removing the swap node and
  // clearing the gesture latch). In our system-slot architecture the bridge lives on the
  // touch-service system handler and the unified widget set is owned by TouchNavState, so removing
  // the widget from the registry is what stops touch from routing into the now-hidden notification
  // body; the now-focused modal owns touch through the same system-slot bridge.
  swap_layer_touch_deregister(swap_layer);
}
#endif // CONFIG_TOUCH

///////////////////////
// ACCESSOR FUNCTIONS
///////////////////////

void swap_layer_set_click_config_onto_window(SwapLayer *swap_layer, struct Window *window) {
  window_set_click_config_provider_with_context(window, prv_swap_layer_click_config_provider,
                                                swap_layer);
}

void swap_layer_set_callbacks(SwapLayer *swap_layer, void *callback_context,
                              SwapLayerCallbacks callbacks) {
  swap_layer->context = callback_context;
  swap_layer->callbacks = callbacks;

  swap_layer_reload_data(swap_layer);
}

Layer *swap_layer_get_layer(const SwapLayer *swap_layer) {
  return &((SwapLayer *)swap_layer)->layer;
}

LayoutLayer *swap_layer_get_current_layout(const SwapLayer *swap_layer) {
  return swap_layer->current;
}

///////////////////////
// INIT FUNCTIONS
///////////////////////

void swap_layer_init(SwapLayer *swap_layer, const GRect *frame) {
  *swap_layer = (SwapLayer){};

  Layer *layer = &swap_layer->layer;
  layer_init(layer, frame);
  layer_set_update_proc(layer, prv_swap_layer_update_proc);

  gbitmap_init_with_resource(&swap_layer->arrow_layer.arrow_bitmap, RESOURCE_ID_ARROW_DOWN);

  const GRect arrow_frame =
      GRect(0, frame->size.h - LAYOUT_ARROW_HEIGHT, frame->size.w, LAYOUT_ARROW_HEIGHT);
  layer_init(&swap_layer->arrow_layer.layer, &arrow_frame);
  layer_set_update_proc(&swap_layer->arrow_layer.layer, prv_arrow_layer_update_proc);
  layer_add_child(layer, &swap_layer->arrow_layer.layer);

#ifdef CONFIG_TOUCH
  swap_layer_touch_register(swap_layer);
#endif
}

void swap_layer_deinit(SwapLayer *swap_layer) {
  swap_layer->is_deiniting = true;
  swap_layer_stop_hold(swap_layer);
#ifdef CONFIG_TOUCH
  // Deinit-safety: deregister removes the swap node from the Tier-1 registry, detaches the shared
  // recognizer set, and clears the active-gesture pointer (cancelling the in-flight gesture) if it
  // targets this layer, so a torn-down widget cannot be reached by a later touch event.
  swap_layer_touch_deregister(swap_layer);
#endif
  gbitmap_deinit(&swap_layer->arrow_layer.arrow_bitmap);
  layer_deinit(&swap_layer->arrow_layer.layer);
  prv_swap_layer_reset(swap_layer);
  layer_deinit(&swap_layer->layer);
}

SwapLayer *swap_layer_create(GRect frame) {
  // Note: Not yet exported for 3rd party apps so no padding is necessary
  SwapLayer *layer = applib_malloc(sizeof(SwapLayer));
  if (layer) {
    swap_layer_init(layer, &frame);
  }
  return layer;
}

void swap_layer_destroy(SwapLayer *swap_layer) {
  if (swap_layer == nullptr) {
    return;
  }
  swap_layer_deinit(swap_layer);
  applib_free(swap_layer);
}
