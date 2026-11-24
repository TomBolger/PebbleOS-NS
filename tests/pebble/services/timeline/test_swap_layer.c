/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <string.h>

#include <pbl/services/timeline/layout_layer.h>
#include <pbl/services/timeline/notification_layout.h>
#include <pbl/services/timeline/swap_layer.h>

#include <applib/ui/animation_private.h>
#include <applib/ui/layer.h>
#include <applib/ui/property_animation.h>
#include <applib/ui/window.h>
#include <clar.h>

// Stubs
///////////////////////////////////////////////////////////

GContext *graphics_context_get_current_context(void) {
  return nullptr;
}

static const LayoutColors s_colors = {.bg_color = {.argb = GColorRedARGB8}};
const LayoutColors *layout_get_colors(const LayoutLayer *layout) {
  return &s_colors;
}

void *applib_malloc(size_t bytes) {
  return nullptr;
}
void applib_free(void *ptr) {
}

uintptr_t evented_timer_register(uint32_t timeout_ms, bool repeating, void (*callback)(void *),
                                 void *data) {
  return 1;
}
void evented_timer_cancel(uintptr_t timer) {
}

void window_schedule_render(Window *window) {
}
void window_set_click_config_provider_with_context(Window *window, ClickConfigProvider provider,
                                                   void *context) {
}
void window_raw_click_subscribe(ButtonId button_id, ClickHandler down_handler,
                                ClickHandler up_handler, void *context) {
}
void window_multi_click_subscribe(ButtonId button_id, uint8_t min_clicks, uint8_t max_clicks,
                                  uint16_t timeout, bool last_click_only, ClickHandler handler) {
}
ButtonId click_recognizer_get_button_id(ClickRecognizerRef recognizer) {
  return BUTTON_ID_DOWN;
}
uint8_t click_number_of_clicks_counted(ClickRecognizerRef recognizer) {
  return 0;
}
bool alerts_preferences_get_notification_alternative_design(void) {
  return false;
}

void graphics_context_set_fill_color(GContext *ctx, GColor color) {
}
void graphics_context_set_compositing_mode(GContext *ctx, GCompOp mode) {
}
void graphics_fill_oval(GContext *ctx, GRect rect, GOvalScaleMode scale_mode) {
}
bool graphics_release_frame_buffer(GContext *ctx, GBitmap *buffer) {
  return true;
}

bool animation_set_duration(Animation *animation, uint32_t duration) {
  ((AnimationPrivate *)animation)->duration_ms = duration;
  return true;
}
bool animation_set_curve(Animation *animation, AnimationCurve curve) {
  return true;
}
bool animation_set_custom_interpolation(Animation *animation,
                                        InterpolateInt64Function interpolate) {
  return true;
}

// Frame animations move their layer straight to the target as they complete.
typedef struct {
  Animation *animation;
  Layer *layer;
  GRect to;
} FrameAnimation;

static FrameAnimation s_frame_animations[16];
static int s_num_frame_animations;

static void prv_frame_animation_update(Animation *animation, const AnimationProgress progress) {
  if (progress != ANIMATION_NORMALIZED_MAX) {
    return;
  }
  for (int i = 0; i < s_num_frame_animations; i++) {
    if (s_frame_animations[i].animation == animation) {
      layer_set_frame(s_frame_animations[i].layer, &s_frame_animations[i].to);
    }
  }
}

PropertyAnimation *property_animation_create_layer_frame(struct Layer *layer, GRect *from,
                                                         GRect *to) {
  static const AnimationImplementation s_implementation = {
    .update = prv_frame_animation_update,
  };
  cl_assert(s_num_frame_animations <
            (int)(sizeof(s_frame_animations) / sizeof(s_frame_animations[0])));
  Animation *animation = animation_create();
  animation_set_implementation(animation, &s_implementation);
  s_frame_animations[s_num_frame_animations++] =
      (FrameAnimation){.animation = animation, .layer = layer, .to = *to};
  return (PropertyAnimation *)animation;
}
bool property_animation_to(PropertyAnimation *property_animation, void *to, size_t size, bool set) {
  return false;
}

#if PBL_ROUND
static int s_reels;
KinoReel *kino_reel_create_with_resource_system(ResAppNum app_num, uint32_t resource_id) {
  s_reels++;
  return (KinoReel *)malloc(1);
}
void kino_reel_destroy(KinoReel *reel) {
  if (reel) {
    s_reels--;
    free(reel);
  }
}
GSize kino_reel_get_size(KinoReel *reel) {
  return GSize(25, 25);
}
void kino_reel_draw(KinoReel *reel, GContext *ctx, GPoint offset) {
}
void notification_layout_get_banner_circle(int16_t origin_y, GPoint *center, int16_t *radius) {
  *center = GPoint(0, 0);
  *radius = 0;
}
#endif

// Fakes
///////////////////////////////////////////////////////////

#define FRAME_H     240
#define MAX_LAYOUTS 4

extern void fake_animation_cleanup(void);
extern void fake_animation_complete(Animation *animation);

//! The swap layer's static scroll, called for a button press.
void prv_attempt_scroll(SwapLayer *swap_layer, ScrollDirection direction);

// A list of layouts of the given heights, shown from s_index.
static int16_t s_heights[MAX_LAYOUTS];
static int s_count;
static int s_index;
static LayoutLayer s_layouts[MAX_LAYOUTS];

static LayoutLayer *prv_get_layout(SwapLayer *swap_layer, int8_t rel_position, void *context) {
  const int index = s_index + rel_position;
  if (index < 0 || index >= s_count) {
    return nullptr;
  }
  LayoutLayer *layout = &s_layouts[index];
  *layout = (LayoutLayer){};
  layer_init(&layout->layer, &GRect(0, 0, 200, s_heights[index]));
  return layout;
}

GSize layout_get_size(GContext *ctx, LayoutLayer *layout) {
  return layout->layer.frame.size;
}

static void prv_layout_did_appear(SwapLayer *swap_layer, LayoutLayer *layout, int8_t rel_change,
                                  void *context) {
  s_index += rel_change;
}

static bool prv_get_next_bubble(SwapLayer *swap_layer, LayoutLayer *layout,
                                SwapLayerNextBubble *bubble, void *context) {
  *bubble = (SwapLayerNextBubble){.color = GColorRed};
  return true;
}

static SwapLayer s_swap_layer;

static void prv_load(int count, const int16_t *heights) {
  s_count = count;
  s_index = 0;
  memcpy(s_heights, heights, sizeof(int16_t) * count);
  swap_layer_init(&s_swap_layer, &GRect(0, 20, 200, FRAME_H));
  swap_layer_set_callbacks(&s_swap_layer, nullptr,
                           (SwapLayerCallbacks){
                             .get_layout_handler = prv_get_layout,
                             .layout_did_appear_handler = prv_layout_did_appear,
                             .get_next_bubble_handler = prv_get_next_bubble,
                           });
}

static int16_t prv_offset(void) {
  return -s_swap_layer.current->layer.frame.origin.y;
}

static void prv_press(ScrollDirection direction) {
  prv_attempt_scroll(&s_swap_layer, direction);
  fake_animation_complete(s_swap_layer.animation);
}

#if PBL_ROUND
static bool prv_bubble_shown(void) {
  return s_swap_layer.next_bubble.shown && !layer_get_hidden(&s_swap_layer.next_bubble.layer);
}
#endif

// Tests
///////////////////////////////////////////////////////////

void test_swap_layer__initialize(void) {
  s_num_frame_animations = 0;
  s_count = 0;
  s_index = 0;
}

void test_swap_layer__cleanup(void) {
  if (s_swap_layer.layer.update_proc) {
    swap_layer_deinit(&s_swap_layer);
  }
  s_swap_layer = (SwapLayer){};
  fake_animation_cleanup();
#if PBL_ROUND
  cl_assert_equal_i(s_reels, 0);
#endif
}

void test_swap_layer__max_scroll(void) {
  // Without a next layout, a layout scrolls to its own end.
  cl_assert_equal_i(swap_layer_max_scroll_dy(400, 240, false), 160);
  cl_assert_equal_i(swap_layer_max_scroll_dy(200, 240, false), 0);
  // Rect peeks the next layout's banner in below the end; round stops at the end.
  cl_assert_equal_i(swap_layer_max_scroll_dy(400, 240, true), PBL_IF_RECT_ELSE(196, 160));
  cl_assert_equal_i(swap_layer_max_scroll_dy(240, 240, true), PBL_IF_RECT_ELSE(36, 0));
  cl_assert_equal_i(swap_layer_max_scroll_dy(200, 240, true), 0);
}

void test_swap_layer__next_bubble_visible(void) {
  // Only at the end of the layout (or past it, pulled by a finger), with a next layout.
  cl_assert_equal_b(swap_layer_next_bubble_visible(160, 160, true), PBL_IF_ROUND_ELSE(true, false));
  cl_assert_equal_b(swap_layer_next_bubble_visible(170, 160, true), PBL_IF_ROUND_ELSE(true, false));
  cl_assert_equal_b(swap_layer_next_bubble_visible(159, 160, true), false);
  cl_assert_equal_b(swap_layer_next_bubble_visible(160, 160, false), false);
  // A layout that fits the screen is at its end right away.
  cl_assert_equal_b(swap_layer_next_bubble_visible(0, 0, true), PBL_IF_ROUND_ELSE(true, false));
  cl_assert_equal_b(swap_layer_next_bubble_visible(0, 0, false), false);
}

void test_swap_layer__next_bubble_handoff(void) {
  // Just below the swap layer: still the bubble.
  cl_assert_equal_i(swap_layer_next_bubble_handoff(FRAME_H, FRAME_H), 0);
  cl_assert_equal_i(swap_layer_next_bubble_handoff(FRAME_H + 30, FRAME_H), 0);
  // A quarter of the way in: half turned into the banner.
  cl_assert_equal_i(swap_layer_next_bubble_handoff(FRAME_H - FRAME_H / 4, FRAME_H), 500);
  // Halfway and beyond: the banner.
  cl_assert_equal_i(swap_layer_next_bubble_handoff(FRAME_H / 2, FRAME_H), 1000);
  cl_assert_equal_i(swap_layer_next_bubble_handoff(0, FRAME_H), 1000);
  cl_assert_equal_i(swap_layer_next_bubble_handoff(-20, FRAME_H), 1000);
}

void test_swap_layer__scrolls_to_the_end_then_swaps(void) {
  prv_load(2, (int16_t[]){500, 300});
  const int16_t end = 500 - FRAME_H + PBL_IF_RECT_ELSE(LAYOUT_BANNER_HEIGHT_RECT, 0);
  int presses = 0;
  while (prv_offset() < end) {
    prv_press(ScrollDirectionDown);
    cl_assert(++presses < 20);
  }
  cl_assert_equal_i(prv_offset(), end);
  cl_assert_equal_i(s_index, 0);
  // The next press swaps.
  prv_press(ScrollDirectionDown);
  cl_assert_equal_i(s_index, 1);
  cl_assert_equal_i(prv_offset(), 0);
}

// Round only: the next bubble.

void test_swap_layer__next_bubble_at_the_end(void) {
#if PBL_ROUND
  prv_load(2, (int16_t[]){500, 300});
  cl_assert(!prv_bubble_shown());
  // Not yet at the end.
  prv_press(ScrollDirectionDown);
  cl_assert(prv_offset() > 0);
  cl_assert(!prv_bubble_shown());
  while (prv_offset() < 500 - FRAME_H) {
    prv_press(ScrollDirectionDown);
  }
  // At the end it rises in.
  cl_assert(prv_bubble_shown());
  cl_assert(animation_is_scheduled(s_swap_layer.next_bubble.animation));
  fake_animation_complete(s_swap_layer.next_bubble.animation);
  cl_assert_equal_i(s_swap_layer.next_bubble.progress, 1000);
  // Scrolling back up hides it.
  prv_press(ScrollDirectionUp);
  cl_assert(!prv_bubble_shown());
#endif
}

void test_swap_layer__next_bubble_hands_off_on_swap(void) {
#if PBL_ROUND
  prv_load(3, (int16_t[]){FRAME_H, 500, 300});
  // A layout that fits the screen shows the bubble at once.
  cl_assert(prv_bubble_shown());
  // Swapping down carries it into the incoming banner...
  prv_attempt_scroll(&s_swap_layer, ScrollDirectionDown);
  cl_assert(s_swap_layer.next_bubble.handoff);
  cl_assert(prv_bubble_shown());
  // ...and it is gone once that has landed, until the end of the new layout.
  fake_animation_complete(s_swap_layer.animation);
  cl_assert_equal_i(s_index, 1);
  cl_assert(!s_swap_layer.next_bubble.handoff);
  cl_assert(!prv_bubble_shown());
#endif
}

void test_swap_layer__no_next_bubble_on_the_last_layout(void) {
#if PBL_ROUND
  prv_load(2, (int16_t[]){500, FRAME_H});
  while (prv_offset() < 500 - FRAME_H) {
    prv_press(ScrollDirectionDown);
  }
  prv_press(ScrollDirectionDown);
  cl_assert_equal_i(s_index, 1);
  cl_assert(!prv_bubble_shown());
  // Pressing on at the end of the last layout doesn't bring one either.
  prv_press(ScrollDirectionDown);
  cl_assert(!prv_bubble_shown());
#endif
}

void test_swap_layer__next_bubble_after_swap_up(void) {
#if PBL_ROUND
  prv_load(2, (int16_t[]){FRAME_H, FRAME_H});
  prv_press(ScrollDirectionDown);
  cl_assert_equal_i(s_index, 1);
  cl_assert(!prv_bubble_shown());
  // Back up: the first layout lands at its end, so the bubble is back.
  prv_press(ScrollDirectionUp);
  cl_assert_equal_i(s_index, 0);
  cl_assert(prv_bubble_shown());
#endif
}
