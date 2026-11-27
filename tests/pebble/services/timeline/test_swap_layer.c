/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <string.h>

#include <pbl/services/timeline/layout_layer.h>
#include <pbl/services/timeline/notification_layout.h>
#include <pbl/services/timeline/swap_layer.h>

#include <applib/ui/animation_interpolate.h>
#include <applib/ui/animation_private.h>
#include <applib/ui/layer.h>
#include <applib/ui/property_animation.h>
#include <applib/ui/window.h>
#include <clar.h>

// Stubs
InterpolateInt64Function animation_private_current_interpolate_override(void) {
  return nullptr;
}
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

// The last timer registered, fired by the tests
static void (*s_timer_callback)(void *);
static void *s_timer_data;
static uint32_t s_timer_ms;

uintptr_t evented_timer_register(uint32_t timeout_ms, bool repeating, void (*callback)(void *),
                                 void *data) {
  s_timer_callback = callback;
  s_timer_data = data;
  s_timer_ms = timeout_ms;
  return 1;
}
void evented_timer_cancel(uintptr_t timer) {
  s_timer_callback = nullptr;
}

static void prv_fire_timer(void) {
  void (*callback)(void *) = s_timer_callback;
  s_timer_callback = nullptr;
  cl_assert(callback);
  callback(s_timer_data);
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
void notification_layout_fill_banner_round(GContext *ctx, int16_t card_top_y, int16_t below_rest) {
}
int16_t notification_layout_get_banner_icon_top_round(int16_t below_rest) {
  return 0;
}
#endif

// Fakes
///////////////////////////////////////////////////////////

#define FRAME_H     240
#define MAX_LAYOUTS 4

extern void fake_animation_cleanup(void);
extern void fake_animation_complete(Animation *animation);
extern void fake_animation_update(Animation *animation, AnimationProgress progress);

//! The swap layer's static scroll, called for a button press.
void prv_attempt_scroll(SwapLayer *swap_layer, ScrollDirection direction);
//! The swap layer's raw button handlers.
void prv_raw_down_handler(ClickRecognizerRef recognizer, void *context);
void prv_raw_up_handler(ClickRecognizerRef recognizer, void *context);

// A list of layouts of the given heights, shown from s_index.
static int16_t s_heights[MAX_LAYOUTS];
static int s_count;
static int s_index;
static LayoutLayer s_layouts[MAX_LAYOUTS];
static int s_color_shift;

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

// Whether the layouts lay their content out by how far they are scrolled, and where they last
// were asked to lay it out, and to show it
static bool s_snaps;
static int16_t s_prepared;
static int16_t s_shown;

bool layout_set_scroll(LayoutLayer *layout, int16_t offset, bool show) {
  if (!s_snaps) {
    return false;
  }
  if (show) {
    s_shown = offset;
  } else {
    s_prepared = offset;
  }
  return true;
}

static void prv_layout_did_appear(SwapLayer *swap_layer, LayoutLayer *layout, int8_t rel_change,
                                  void *context) {
  s_index += rel_change;
}

// Each layout's banner has its own color, so the tests can tell them apart.
static GColor prv_banner_color(LayoutLayer *layout) {
  return (GColor){.argb = (uint8_t)(0xC0 + (layout - s_layouts) + s_color_shift)};
}

static bool prv_get_banner_look(SwapLayer *swap_layer, LayoutLayer *layout,
                                SwapLayerBannerLook *look, void *context) {
  *look = (SwapLayerBannerLook){.color = prv_banner_color(layout)};
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
                             .get_banner_look_handler = prv_get_banner_look,
                           });
}

static int16_t prv_offset(void) {
  return -s_swap_layer.current->layer.frame.origin.y;
}

static void prv_press(ScrollDirection direction) {
  prv_attempt_scroll(&s_swap_layer, direction);
  fake_animation_complete(s_swap_layer.animation);
}

// Tests
///////////////////////////////////////////////////////////

void test_swap_layer__initialize(void) {
  s_snaps = false;
  s_prepared = -1;
  s_shown = -1;
  s_timer_callback = nullptr;
  s_num_frame_animations = 0;
  s_count = 0;
  s_index = 0;
  s_color_shift = 0;
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

// Content laid out by how far it is scrolled moves in Moook steps.

static AnimationProgress prv_progress_of_frame(int frame, int num_frames) {
  return (AnimationProgress)((ANIMATION_NORMALIZED_MAX * frame) / num_frames);
}

void test_swap_layer__snap_step_cuts_over_once(void) {
  s_snaps = true;
  prv_load(1, (int16_t[]){600});
  prv_attempt_scroll(&s_swap_layer, ScrollDirectionDown);
  Animation *step = s_swap_layer.animation;
  const int16_t target = s_swap_layer.scroll_target;
  cl_assert(target > 0);
  // Laid out for where it will rest as it starts, shown as it was
  cl_assert_equal_i(s_prepared, target);
  cl_assert_equal_i(s_shown, -1);
  cl_assert_equal_i(animation_get_duration(step, false, false), interpolate_moook_duration());

  // The lead-in moves it as it is
  const int num_frames = 7;
  fake_animation_update(step, prv_progress_of_frame(2, num_frames));
  cl_assert(prv_offset() > 0 && prv_offset() < target);
  cl_assert_equal_i(s_shown, -1);
  // Then it cuts over to its new layout, overshooting, and settles there
  fake_animation_update(step, prv_progress_of_frame(3, num_frames));
  cl_assert_equal_i(s_shown, target);
  cl_assert(prv_offset() > target);
  fake_animation_complete(step);
  cl_assert_equal_i(prv_offset(), target);
  cl_assert_equal_i(s_shown, target);
}

void test_swap_layer__snap_press_mid_step_carries_on(void) {
  s_snaps = true;
  prv_load(1, (int16_t[]){800});
  prv_press(ScrollDirectionDown);
  const int16_t first = prv_offset();
  prv_attempt_scroll(&s_swap_layer, ScrollDirectionDown);
  Animation *step = s_swap_layer.animation;
  const int16_t second = s_swap_layer.scroll_target;
  cl_assert(second > first);
  fake_animation_update(step, prv_progress_of_frame(1, 7));
  // A press during the lead-in heads on from where it is to a step past where it was heading
  const int16_t where = prv_offset();
  prv_attempt_scroll(&s_swap_layer, ScrollDirectionDown);
  cl_assert_equal_i(s_swap_layer.scroll_from, where);
  cl_assert(s_swap_layer.scroll_target > second);
  const int16_t third = s_swap_layer.scroll_target;
  cl_assert_equal_i(s_prepared, third);
  fake_animation_complete(s_swap_layer.animation);
  cl_assert_equal_i(prv_offset(), third);
  cl_assert_equal_i(s_shown, third);
}

void test_swap_layer__snap_hold_steps_to_the_end(void) {
  s_snaps = true;
  prv_load(1, (int16_t[]){600});
  const int16_t end = 600 - FRAME_H;
  prv_raw_down_handler(nullptr, &s_swap_layer);
  // A press only repeats once held well past its own step
  cl_assert(s_timer_ms > interpolate_moook_duration());
  fake_animation_complete(s_swap_layer.animation);
  const int16_t first = prv_offset();
  prv_fire_timer();
  // Held, it steps on a press at a time, each step starting as the last one lands
  int steps = 0;
  int16_t prev = first;
  while (s_swap_layer.scrolling && steps < 20) {
    cl_assert(s_swap_layer.scroll_target > prev);
    cl_assert(s_swap_layer.scroll_target - prev <= 48);
    prev = s_swap_layer.scroll_target;
    fake_animation_complete(s_swap_layer.animation);
    steps++;
  }
  cl_assert_equal_i(prv_offset(), end);
  cl_assert(steps > 1);
  prv_raw_up_handler(nullptr, &s_swap_layer);
}

void test_swap_layer__snap_release_lets_the_step_land(void) {
  s_snaps = true;
  prv_load(1, (int16_t[]){600});
  prv_raw_down_handler(nullptr, &s_swap_layer);
  fake_animation_complete(s_swap_layer.animation);
  prv_fire_timer();
  const int16_t target = s_swap_layer.scroll_target;
  prv_raw_up_handler(nullptr, &s_swap_layer);
  // Released mid-step: that step lands, and nothing follows it
  cl_assert(s_swap_layer.scrolling);
  fake_animation_complete(s_swap_layer.animation);
  cl_assert_equal_i(prv_offset(), target);
  cl_assert(!s_swap_layer.scrolling);
}

// Round only: the banners.

void test_swap_layer__banners_follow_the_layouts(void) {
#if PBL_ROUND
  prv_load(3, (int16_t[]){FRAME_H, 500, 300});
  cl_assert(s_swap_layer.banners.current.valid);
  cl_assert(s_swap_layer.banners.next.valid);
  cl_assert(gcolor_equal(s_swap_layer.banners.next.look.color, prv_banner_color(&s_layouts[1])));
  // Nothing rises at first: the window brings the layouts in.
  cl_assert(!animation_is_scheduled(s_swap_layer.banners.rise));
  // Swapping down brings the layout after along under the incoming one, so its peek rises with it.
  prv_attempt_scroll(&s_swap_layer, ScrollDirectionDown);
  cl_assert(s_swap_layer.next);
  cl_assert_equal_i(
      s_swap_layer.next->layer.frame.origin.y,
      s_swap_layer.current->layer.frame.origin.y + s_swap_layer.current->layer.frame.size.h);
  cl_assert(gcolor_equal(s_swap_layer.banners.current.look.color, prv_banner_color(&s_layouts[1])));
  fake_animation_complete(s_swap_layer.animation);
  cl_assert_equal_i(s_index, 1);
  cl_assert(gcolor_equal(s_swap_layer.banners.next.look.color, prv_banner_color(&s_layouts[2])));
  cl_assert(!animation_is_scheduled(s_swap_layer.banners.rise));
  // And back up: the outgoing layout's banner becomes the peek again, without rising.
  prv_press(ScrollDirectionUp);
  cl_assert_equal_i(s_index, 0);
  cl_assert(gcolor_equal(s_swap_layer.banners.next.look.color, prv_banner_color(&s_layouts[1])));
  cl_assert(!animation_is_scheduled(s_swap_layer.banners.rise));
#endif
}

void test_swap_layer__new_peek_rises_in(void) {
#if PBL_ROUND
  prv_load(2, (int16_t[]){FRAME_H, FRAME_H});
  // The same layouts again: nothing to rise.
  swap_layer_reload_data(&s_swap_layer);
  cl_assert(!animation_is_scheduled(s_swap_layer.banners.rise));
  // A different next layout rises in.
  s_color_shift = 1;
  swap_layer_reload_data(&s_swap_layer);
  cl_assert(animation_is_scheduled(s_swap_layer.banners.rise));
  fake_animation_complete(s_swap_layer.banners.rise);
  cl_assert_equal_i(s_swap_layer.banners.rise_progress, 1000);
#endif
}

void test_swap_layer__no_peek_on_the_last_layout(void) {
#if PBL_ROUND
  prv_load(2, (int16_t[]){500, FRAME_H});
  while (prv_offset() < 500 - FRAME_H) {
    prv_press(ScrollDirectionDown);
  }
  prv_press(ScrollDirectionDown);
  cl_assert_equal_i(s_index, 1);
  cl_assert(!s_swap_layer.next);
  cl_assert(!s_swap_layer.banners.next.valid);
#endif
}
