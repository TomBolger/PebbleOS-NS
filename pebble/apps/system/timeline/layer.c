/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "layer.h"

#include "animations.h"
#include "list_layout.h"
#include "model.h"
#include "relbar.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#include <pbl/kernel/assert.h>
#include <pbl/logging/logging.h>
#include <pbl/services/clock.h>
#include <pbl/services/timeline/layout_layer.h>
#include <pbl/services/timeline/timeline_layout.h>
#include <pbl/services/timeline/timeline_resources.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/struct.h>
#include <pbl/util/trig.h>

#include <applib/app_malloc.h>
#include <applib/fonts/fonts.h>
#include <applib/graphics/gpath.h>
#include <applib/graphics/graphics.h>
#include <applib/preferred_content_size.h>
#include <applib/ui/action_bar_layer.h>
#include <applib/ui/action_bar_layer_private.h>
#include <applib/ui/kino/kino_layer.h>
#include <applib/ui/property_animation.h>
#include <applib/ui/window.h>
#include <kernel/ui/kernel_ui.h>
#include <kernel/ui/system_icons.h>
#include <popups/timeline/peek_animations.h>
#include <process_state/app_state/app_state.h>
#include <resource/resource_ids.auto.h>

#define PAST_TOP_MARGIN_EXTRA   PBL_IF_RECT_ELSE(10, 38)
#define FUTURE_TOP_MARGIN_EXTRA PBL_IF_RECT_ELSE(10, 18)

typedef struct TimelineLayerStyle {
  GSize sidebar_arrow_size;
  GPoint day_sep_offset;
  uint16_t sidebar_width;
  int16_t fin_offset_x;
  int16_t past_fin_offset_y;
  int16_t future_fin_offset_y;
  uint16_t past_top_margin;
  uint16_t past_thin_pin_margin;
  uint16_t future_top_margin;
  uint16_t left_margin;
  uint16_t right_margin;
  int16_t icon_offset_y;
  uint16_t icon_right_margin;
  uint16_t fat_pin_height;
  uint16_t thin_pin_height;
  uint16_t day_sep_dot_diameter;
  uint16_t day_sep_subtitle_margin;
  int16_t past_day_sep_dot_offset_y;
  int16_t future_day_sep_dot_offset_y;
} TimelineLayerStyle;

#define MARGIN_MEDIUM PBL_IF_RECT_ELSE(4, 13)

static const TimelineLayerStyle s_style_medium = {
  .sidebar_arrow_size.w = PBL_IF_RECT_ELSE(10, 7),
  .sidebar_arrow_size.h = PBL_IF_RECT_ELSE(20, 28),
  .sidebar_width = PBL_IF_RECT_ELSE(30, 48),
  .past_fin_offset_y = PBL_IF_ROUND_ELSE(-12, 0),
  .future_fin_offset_y = PBL_IF_ROUND_ELSE(-20, 0),
  .past_top_margin = PBL_IF_RECT_ELSE(10, 18),
  .future_top_margin = PBL_IF_RECT_ELSE(10, 39),
  .left_margin = MARGIN_MEDIUM,
  .right_margin = MARGIN_MEDIUM,
  .icon_right_margin = MARGIN_MEDIUM,
  .fat_pin_height = 110,
  // Remainder of the screen space after the fat pin
  .thin_pin_height = PBL_IF_RECT_ELSE(66, 43),
  .day_sep_dot_diameter = 9,
  .day_sep_offset.x = PBL_IF_ROUND_ELSE(12, 0),
  .day_sep_offset.y = -12,
  .day_sep_subtitle_margin = PEEK_LAYER_SUBTITLE_MARGIN,
  .past_day_sep_dot_offset_y = PBL_IF_ROUND_ELSE(-17, 0),
  .future_day_sep_dot_offset_y = PBL_IF_ROUND_ELSE(-13, 0),
};

static const TimelineLayerStyle s_style_large = {
  .sidebar_arrow_size.w = PBL_IF_RECT_ELSE(14, 10),
  .sidebar_arrow_size.h = PBL_IF_RECT_ELSE(28, 40),
  .sidebar_width = PBL_IF_RECT_ELSE(34, 51),
  .fin_offset_x = 4,
  .future_fin_offset_y = 37,
  .past_top_margin = PBL_IF_RECT_ELSE(7, 18),
  .past_thin_pin_margin = 11,
  .future_top_margin = PBL_IF_RECT_ELSE(7, 39),
  .left_margin = 9,
  .right_margin = 14,
  .icon_offset_y = 3,
  .icon_right_margin = PBL_IF_RECT_ELSE(6, 12),
  .fat_pin_height = 131,
  .thin_pin_height = 88,
  .day_sep_dot_diameter = 12,
  .day_sep_offset.y = -21,
  .past_day_sep_dot_offset_y = PBL_IF_RECT_ELSE(-16, -32),
  .future_day_sep_dot_offset_y = 16,
};

static const TimelineLayerStyle *const s_styles[NumPreferredContentSizes] = {
  [PreferredContentSizeSmall] = &s_style_medium,
  [PreferredContentSizeMedium] = &s_style_medium,
  [PreferredContentSizeLarge] = &s_style_large,
  [PreferredContentSizeExtraLarge] = &s_style_large,
};

static const int s_visible_items[] = {1, 2};
static const int s_nonvisible_items[] = {0, TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER - 1};

static const TimelineLayerStyle *prv_get_style(void) {
  // Round pin heights and icon positions depend on the display, not the font size.
  return s_styles[PBL_IF_ROUND_ELSE(PreferredContentSizeDefault, system_theme_get_content_size())];
}

// The end-of-timeline and day separator sit relative to the display, not the font size.
static const TimelineLayerStyle *prv_get_display_style(void) {
  return s_styles[PreferredContentSizeDefault];
}

uint16_t timeline_layer_get_fat_pin_height(void) {
  return prv_get_style()->fat_pin_height;
}

int16_t timeline_layer_get_icon_offset_y(void) {
  return prv_get_style()->icon_offset_y;
}

uint16_t timeline_layer_get_ideal_sidebar_width(void) {
  return prv_get_style()->sidebar_width;
}

uint16_t timeline_layer_get_sidebar_arrow_width(void) {
  return prv_get_style()->sidebar_arrow_size.w;
}

bool timeline_layer_sidebar_is_on_right(void) {
  return action_bar_layer_is_on_right();
}

int16_t timeline_layer_get_icon_outer_inset(void) {
  return prv_get_style()->icon_right_margin;
}

int16_t timeline_layer_get_pin_text_origin_x(void) {
  if (timeline_layer_sidebar_is_on_right()) {
    return 0;
  }
  const TimelineLayerStyle *style = prv_get_style();
  return (int16_t)(style->sidebar_width + style->right_margin - style->left_margin);
}

static void prv_inset_rect_for_sidebar(GRect *frame, int16_t sidebar_width) {
  frame->size.w -= sidebar_width;
  if (!timeline_layer_sidebar_is_on_right()) {
    frame->origin.x += sidebar_width;
  }
}

///////////////////////////////////////////////////////////
// Drawing functions
///////////////////////////////////////////////////////////

static int prv_get_scroll_delta(TimelineLayer *timeline_layer) {
  return (timeline_layer->scroll_direction == TimelineScrollDirectionUp ? -1 : 1);
}

static int prv_get_index_delta(TimelineLayer *timeline_layer) {
  return timeline_layer->move_delta * prv_get_scroll_delta(timeline_layer);
}

static void prv_set_layout_hidden(TimelineLayout *layout, bool hidden) {
  layer_set_hidden((Layer *)layout, hidden);
  layer_set_hidden((Layer *)&layout->icon_layer, hidden);
}

static LayoutLayerMode prv_get_mode(int index) {
  switch (index) {
    default:
    case 1:
      return LayoutLayerModePinnedFat;
    case 2:
      return LayoutLayerModePinnedThin;
  }
}

static int16_t prv_get_height_offset(TimelineLayer *layer, const TimelineLayerStyle *style,
                                     int index) {
  if (layer->scroll_direction == TimelineScrollDirectionUp) {
    switch (index) {
      case 0:
        return PAST_TOP_MARGIN_EXTRA + style->thin_pin_height + (2 * style->fat_pin_height);
      case 1:
        return style->past_top_margin + style->thin_pin_height + style->past_thin_pin_margin;
      case 2:
        return style->past_top_margin;
      default:
        return style->past_top_margin - 2 * style->fat_pin_height;
    }
  }
  switch (index) {
    case 0:
      return FUTURE_TOP_MARGIN_EXTRA - 2 * style->fat_pin_height;
    case 1:
      return style->future_top_margin;
    case 2:
      return style->future_top_margin + style->fat_pin_height;
    default:
      return style->future_top_margin + 3 * style->fat_pin_height;
  }
}

static void prv_get_frame_for_style(TimelineLayer *layer, const TimelineLayerStyle *style,
                                    int index, GRect *frame) {
  index = PBL_CLAMP(index, 0, TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER - 1);
  const int16_t width = layer->layer.bounds.size.w - (style->left_margin + style->right_margin);
  const uint16_t height = (prv_get_mode(index) == LayoutLayerModePinnedThin)
                              ? style->thin_pin_height
                              : style->fat_pin_height;
  *frame = GRect(style->left_margin, prv_get_height_offset(layer, style, index), width, height);
}

static void prv_get_frame(TimelineLayer *layer, int index, GRect *frame) {
  prv_get_frame_for_style(layer, prv_get_style(), index, frame);
}

// Horizontal placement follows the font size style, vertical placement the display style.
static void prv_get_indicator_frame(TimelineLayer *layer, int index, GRect *frame) {
  GRect display_frame;
  prv_get_frame_for_style(layer, prv_get_display_style(), index, &display_frame);
  prv_get_frame(layer, index, frame);
  frame->origin.y = display_frame.origin.y;
  frame->size.h = display_frame.size.h;
}

static void prv_get_pin_icon_frame(TimelineLayer *layer, const GRect *pin_frame,
                                   GRect *icon_frame) {
  const TimelineLayerStyle *style = prv_get_style();
  GRect frame = *pin_frame;
  frame.origin.y += style->icon_offset_y;
  // On the right, grow the pin frame so the icon sits icon_right_margin from the screen edge.
  if (timeline_layer_sidebar_is_on_right()) {
    frame.size.w += style->right_margin - style->icon_right_margin;
  }
  timeline_layout_get_icon_frame(&frame, layer->scroll_direction, icon_frame);
}

static void prv_get_icon_frame_exact(TimelineLayer *layer, int index, GRect *icon_frame) {
  GRect frame;
  prv_get_frame(layer, index, &frame);
  prv_get_pin_icon_frame(layer, &frame, icon_frame);
}

static void prv_list_get_pin_icon_frame(TimelineLayer *layer, int offset, GRect *icon_frame);

#if PBL_ROUND
static void prv_get_icon_frame_centered(TimelineLayer *layer, int index, GRect *icon_frame) {
  const int center_index = 1;
  const GRect *bounds = &((Layer *)layer)->bounds;
  prv_get_icon_frame_exact(layer, center_index, icon_frame);
  icon_frame->origin.y += prv_get_scroll_delta(layer) * (index - center_index) * bounds->size.h / 2;
}
#endif

void timeline_layer_get_icon_frame(TimelineLayer *layer, int index, GRect *icon_frame) {
  if (layer->compact) {
    prv_list_get_pin_icon_frame(layer, index - TIMELINE_LAYER_FIRST_VISIBLE_LAYOUT, icon_frame);
    return;
  }
  return PBL_IF_RECT_ELSE(prv_get_icon_frame_exact, prv_get_icon_frame_centered)(layer, index,
                                                                                 icon_frame);
}

static void prv_get_end_of_timeline_frame(TimelineLayer *layer, int index, GRect *frame) {
  prv_get_indicator_frame(layer, index, frame);
  const bool is_future = (layer->scroll_direction == TimelineScrollDirectionDown);
  const TimelineLayerStyle *style = prv_get_style();
  const TimelineLayerStyle *display_style = prv_get_display_style();
  gpoint_add_eq(&frame->origin,
                GPoint(style->fin_offset_x, is_future ? display_style->future_fin_offset_y
                                                      : display_style->past_fin_offset_y));
  prv_inset_rect_for_sidebar(frame, PBL_IF_RECT_ELSE(style->sidebar_width, 0));
}

static void prv_get_day_sep_frame(TimelineLayer *layer, int index, GRect *frame) {
  prv_get_indicator_frame(layer, index, frame);
  const bool is_future = (layer->scroll_direction == TimelineScrollDirectionDown);
  const TimelineLayerStyle *style = prv_get_style();
  const TimelineLayerStyle *display_style = prv_get_display_style();
  frame->origin.y += is_future ? display_style->future_day_sep_dot_offset_y
                               : display_style->past_day_sep_dot_offset_y;
  // Remove the built-in margins and subtract the sidebar
  frame->origin.x -= style->left_margin;
  frame->size.w += (style->left_margin + style->right_margin);
  prv_inset_rect_for_sidebar(frame, PBL_IF_RECT_ELSE(style->sidebar_width, 0));
}

static void prv_get_day_sep_show_frame(TimelineLayer *layer, GRect *frame) {
  const GRect *bounds = &((Layer *)layer)->bounds;
  const TimelineLayerStyle *style = prv_get_style();
  *frame = (GRect){
    .origin = gpoint_add(bounds->origin, style->day_sep_offset),
    .size.w = bounds->size.w,
    .size.h = bounds->size.h,
  };
  prv_inset_rect_for_sidebar(frame, style->sidebar_width);
}

static void prv_create_layout(TimelineLayer *layer, TimelineIterState *state, int index) {
  TimelineItem *item = &state->pin;
  TimelineLayoutInfo *info = app_malloc_check(sizeof(TimelineLayoutInfo));
  timeline_layout_init_info(info, item, state->current_day);
  info->scroll_direction = layer->scroll_direction;
  info->app_id = item->header.parent_id;

  GRect rect;
  prv_get_frame(layer, index, &rect);
  const LayoutLayerConfig config = {
    .frame = &rect,
    .attributes = &item->attr_list,
    .mode = prv_get_mode(index),
    .app_id = &item->header.parent_id,
    .context = info,
  };
  TimelineLayout *layout = (TimelineLayout *)layout_create(item->header.layout, &config);
  layer_add_child(&layer->layouts_layer, (Layer *)layout);
  GRect icon_rect;
  timeline_layer_get_icon_frame(layer, index, &icon_rect);
  layer_set_frame((Layer *)&layout->icon_layer, &icon_rect);
  layer_add_child(&layer->layouts_layer, (Layer *)&layout->icon_layer);
  layer->layouts[index] = layout;
  layer->layouts_info[index] = info;
}

static void prv_destroy_layout(TimelineLayer *layer, int index) {
  TimelineLayout *timeline_layout = layer->layouts[index];
  timeline_layout->is_being_destroyed = true;
  layer_remove_from_parent((Layer *)timeline_layout);
  layout_destroy((LayoutLayer *)timeline_layout);
  layer->layouts[index] = nullptr;

  app_free(layer->layouts_info[index]);
  layer->layouts_info[index] = nullptr;
}

static void prv_destroy_nonvisible_items(TimelineLayer *layer) {
  for (int i = 0; i < (int)PBL_ARRAY_SIZE(s_nonvisible_items); i++) {
    TimelineLayout *timeline_layout = layer->layouts[s_nonvisible_items[i]];
    if (timeline_layout) {
      prv_destroy_layout(layer, s_nonvisible_items[i]);
      layer->layouts[s_nonvisible_items[i]] = nullptr;
    }
  }
}

static void prv_set_layouts_to_final_position(TimelineLayer *layer) {
  for (int i = 0; i < TIMELINE_NUM_VISIBLE_ITEMS; i++) {
    TimelineLayout *layout = layer->layouts[s_visible_items[i]];
    if (layout) {
      GRect frame;
      prv_get_frame(layer, i + 1, &frame);
      layer_set_frame((Layer *)layout, &frame);
    }
  }
}

static void prv_hide_non_current_day_items(TimelineLayer *layer) {
  for (int i = 0; i < TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER; i++) {
    TimelineLayout *layout = layer->layouts[i];
    TimelineIterState *state = timeline_model_get_iter_state(i - 1);
    if (layout && state->current_day != layer->current_day) {
      prv_set_layout_hidden(layout, true);
    }
  }
}

static void prv_reset_layouts(TimelineLayer *layer) {
  int num_items = timeline_model_get_num_items();
  for (int i = 0; i < (int)PBL_ARRAY_SIZE(s_visible_items); i++) {
    if (layer->layouts[s_visible_items[i]]) {
      prv_destroy_layout(layer, s_visible_items[i]);
    }
    TimelineIterState *state = timeline_model_get_iter_state(i);
    if (!state) {
      continue;
    }
    TimelineNode *node = state->node;
    if (i < num_items && node) {
      prv_create_layout(layer, state, s_visible_items[i]);
    }
  }
}

static void prv_update_pins_mode(TimelineLayer *layer) {
  for (int i = 0; i < (int)PBL_ARRAY_SIZE(s_visible_items); i++) {
    TimelineLayout *timeline_layout = layer->layouts[s_visible_items[i]];
    if (timeline_layout) {
      LayoutLayerMode mode = prv_get_mode(i + 1);
      layout_set_mode((LayoutLayer *)timeline_layout, mode);
    }
  }
}

///////////////////////////////////////////////////////////
// Drawing functions
///////////////////////////////////////////////////////////

// An animation that moves a layer from an initial position to its final position
static Animation *prv_create_layout_up_down_animation(TimelineLayout *timeline_layout, int to_index,
                                                      TimelineLayer *timeline_layer,
                                                      uint32_t duration,
                                                      InterpolateInt64Function interpolate) {
  const int from_index = to_index + prv_get_index_delta(timeline_layer);
  GRect from, to, icon_from, icon_to;
  prv_get_frame(timeline_layer, from_index, &from);
  prv_get_frame(timeline_layer, to_index, &to);
  timeline_layer_get_icon_frame(timeline_layer, from_index, &icon_from);
  timeline_layer_get_icon_frame(timeline_layer, to_index, &icon_to);
  layer_set_frame((Layer *)timeline_layout, &from);
  return timeline_layout_create_up_down_animation(timeline_layout, &from, &to, &icon_from, &icon_to,
                                                  duration, interpolate);
}

static Animation *prv_create_end_of_timeline_animation(TimelineLayer *layer, int to_index,
                                                       uint32_t duration,
                                                       InterpolateInt64Function interpolate) {
  const int from_index = to_index + prv_get_index_delta(layer);
  GRect from_frame, to_frame;
  prv_get_end_of_timeline_frame(layer, from_index, &from_frame);
  prv_get_end_of_timeline_frame(layer, to_index, &to_frame);
  PropertyAnimation *prop_animation = property_animation_create_layer_frame(
      (Layer *)&layer->end_of_timeline, &from_frame, &to_frame);
  Animation *animation = property_animation_get_animation(prop_animation);
  animation_set_duration(animation, duration);
  animation_set_custom_interpolation(animation, interpolate);
  animation_set_handlers(animation,
                         (AnimationHandlers){
                           .stopped = timeline_animation_layer_stopped_cut_to_end,
                         },
                         prop_animation);
  return animation;
}

Animation *timeline_layer_create_day_sep_hide(TimelineLayer *timeline_layer) {
  GRect frame;
  layer_get_global_frame((Layer *)&timeline_layer->day_separator, &frame);
  const TimelineLayerStyle *style = prv_get_style();
  const int16_t expanded_layer_height =
      frame.size.h + 2 * (style->left_margin + style->right_margin);
  // move way off screen: 2x height * (1/2 - move_delta) in integer logic
  const int16_t TARGET_Y = (2 * expanded_layer_height * (1 - 2 * timeline_layer->move_delta)) / 2;
  const GRect scale_to = GRect(frame.origin.x + frame.size.w / 2, // go to the center
                               TARGET_Y, 0, 0);
  peek_layer_set_scale_to(&timeline_layer->day_separator, scale_to);

  // out anim
  GRect to = timeline_layer->day_separator.layer.frame;
  to.origin = GPoint(0, frame.size.h * timeline_layer->move_delta); // all the way off screen
  PropertyAnimation *prop_anim =
      property_animation_create_layer_frame((Layer *)&timeline_layer->day_separator, nullptr, &to);
  Animation *anim = property_animation_get_animation(prop_anim);
  animation_set_duration(anim, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS);
  animation_set_custom_interpolation(anim, timeline_animation_interpolate_moook_soft);

  peek_layer_clear_fields(&timeline_layer->day_separator);
  peek_layer_play(&timeline_layer->day_separator);

  return anim;
}

void timeline_layer_set_day_sep_frame(TimelineLayer *timeline_layer, const GRect *frame) {
  layer_set_hidden((Layer *)&timeline_layer->day_separator, false);
  peek_layer_set_frame(&timeline_layer->day_separator, frame);
}

static void prv_show_day_sep(TimelineLayer *timeline_layer, bool slide) {
  // update the day to show the right date
  timeline_layer->current_day = timeline_model_get_current_state()->current_day;
  char friendly_date[TIME_STRING_REQUIRED_LENGTH];
  char month_and_day[TIME_STRING_REQUIRED_LENGTH];
  clock_get_friendly_date(friendly_date, TIME_STRING_REQUIRED_LENGTH, timeline_layer->current_day);
  clock_get_month_named_date(month_and_day, TIME_STRING_REQUIRED_LENGTH,
                             timeline_layer->current_day);

  GRect frame;
  layer_get_global_frame((Layer *)&timeline_layer->day_separator, &frame);
  const GRect icon_from = {grect_center_point(&frame), GSizeZero};
  prv_get_day_sep_show_frame(timeline_layer, &frame);
  peek_layer_set_frame(&timeline_layer->day_separator, &frame);
  TimelineResourceInfo timeline_res = {
    .res_id = TIMELINE_RESOURCE_DAY_SEPARATOR,
  };
  peek_layer_set_icon_with_size_invert(&timeline_layer->day_separator, &timeline_res,
                                       TimelineResourceSizeLarge, icon_from,
                                       system_theme_is_dark_mode());

  if (slide) {
    const bool align_in_frame = true;
    frame.origin.y += PEEK_LAYER_ICON_OFFSET_Y;
    peek_layer_set_scale_to_image(&timeline_layer->day_separator, &timeline_res,
                                  TimelineResourceSizeLarge, frame, align_in_frame);
    peek_layer_set_fields_hidden(&timeline_layer->day_separator, true);
  }

  peek_layer_set_fields(&timeline_layer->day_separator, "", friendly_date, month_and_day);
  peek_layer_play(&timeline_layer->day_separator);
}

void timeline_layer_unfold_day_sep(TimelineLayer *timeline_layer) {
  const bool slide = false;
  prv_show_day_sep(timeline_layer, slide);
}

void timeline_layer_slide_day_sep(TimelineLayer *timeline_layer) {
  const bool slide = true;
  prv_show_day_sep(timeline_layer, slide);
}

static void prv_day_sep_anim_stopped(Animation *anim, bool finished, void *context) {
  TimelineLayer *timeline_layer = context;
  if (finished) {
    timeline_layer_unfold_day_sep(timeline_layer);
  }
}

Animation *timeline_layer_create_day_sep_show(TimelineLayer *timeline_layer) {
  GRect *from = &((Layer *)&timeline_layer->day_separator)->frame;
  GRect to;
  prv_get_day_sep_show_frame(timeline_layer, &to);
  // Keep the x-axis values until the actual unfold to maintain alignment
  to.origin.x = from->origin.x;
  to.size.w = from->size.w;

  PropertyAnimation *prop_anim =
      property_animation_create_layer_frame((Layer *)&timeline_layer->day_separator, from, &to);
  Animation *anim = property_animation_get_animation(prop_anim);
  animation_set_handlers(anim,
                         (AnimationHandlers){
                           .stopped = prv_day_sep_anim_stopped,
                         },
                         timeline_layer);
  animation_set_duration(anim, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS);
  animation_set_custom_interpolation(anim, timeline_animation_interpolate_moook_soft);
  return anim;
}

///////////////////////////////////////////////////////////
// Compact list
///////////////////////////////////////////////////////////

//! Space kept free at the screen edge the compact list starts from, above the pins' own padding
#define LIST_EDGE_MARGIN 4
//! Distance from the pins to the day separator dot and the end-of-timeline fin
#define LIST_INDICATOR_SPACING 12

static LayoutLayerMode prv_list_mode(bool selected) {
  return selected ? LayoutLayerModePinnedFat : LayoutLayerModePinnedThin;
}

static int16_t prv_list_pin_height(const TimelineListPinView *pin) {
  return TIMELINE_LAYOUT_COMPACT_PADDING_TOP + pin->layout->view_size.h;
}

//! The selected pin is expanded to show its details, the others show only their time and title
static void prv_list_pin_set_selected(TimelineListPinView *pin, bool selected) {
  LayoutLayer *layout = (LayoutLayer *)pin->layout;
  if (layout->mode != prv_list_mode(selected)) {
    layout_set_mode(layout, prv_list_mode(selected));
  }
  // The round arrow points at the title, the rect list centers the whole pin
  pin->anchor_y = PBL_IF_ROUND_ELSE(
      TIMELINE_LAYOUT_COMPACT_PADDING_TOP + timeline_layout_get_title_center_y(pin->layout),
      prv_list_pin_height(pin) / 2);
}

static void prv_list_pin_free(TimelineListPinView *pin) {
  timeline_item_free_allocated_buffer(&pin->state.pin);
  app_free(pin);
}

static void prv_list_pin_destroy(TimelineListPinView *pin) {
  TimelineLayout *layout = pin->layout;
  layout->is_being_destroyed = true;
  layer_remove_from_parent((Layer *)layout);
  layout_destroy((LayoutLayer *)layout);
  prv_list_pin_free(pin);
}

//! Loads the pin `step` pins away from a pin, or returns nullptr past the end of the timeline
static TimelineListPinView *prv_list_pin_load(TimelineIterState *state, Iterator *iter, int step) {
  TimelineListPinView *pin = app_zalloc_check(sizeof(TimelineListPinView));
  timeline_iter_copy_state(&pin->state, state, &pin->iter, iter);
  bool found = true;
  if (step == 0) {
    pin->state.pin.header.id = state->pin.header.id;
    timeline_iter_refresh_pin(&pin->state);
  } else {
    found = (step > 0) ? iter_next(&pin->iter) : iter_prev(&pin->iter);
  }
  if (!found) {
    prv_list_pin_free(pin);
    return nullptr;
  }
  return pin;
}

static void prv_list_pin_create_layout(TimelineLayer *layer, TimelineListPinView *pin,
                                       bool selected) {
  TimelineItem *item = &pin->state.pin;
  timeline_layout_init_info(&pin->info, item, pin->state.current_day);
  pin->info.scroll_direction = layer->scroll_direction;
  pin->info.app_id = item->header.parent_id;
  pin->info.compact = true;

  const TimelineLayerStyle *style = prv_get_style();
  const GRect frame = {
    .origin.x = style->left_margin,
    .size.w = layer->layer.bounds.size.w - (style->left_margin + style->right_margin),
  };
  const LayoutLayerConfig config = {
    .frame = &frame,
    .attributes = &item->attr_list,
    .mode = prv_list_mode(selected),
    .app_id = &item->header.parent_id,
    .context = &pin->info,
  };
  pin->layout = (TimelineLayout *)layout_create(item->header.layout, &config);
  layer_add_child(&layer->layouts_layer, (Layer *)pin->layout);
  layer_add_child(&layer->layouts_layer, (Layer *)&pin->layout->icon_layer);
  prv_list_pin_set_selected(pin, selected);
}

static void prv_list_get_icon_frame(TimelineLayer *layer, const TimelineListPinView *pin,
                                    const GRect *frame, GRect *icon_frame) {
  // The icon sits by the content, below the pin's padding
  GRect content_frame = *frame;
  content_frame.origin.y += TIMELINE_LAYOUT_COMPACT_PADDING_TOP;
  prv_get_pin_icon_frame(layer, &content_frame, icon_frame);
#if PBL_ROUND
  icon_frame->origin.y = frame->origin.y + pin->anchor_y - icon_frame->size.h / 2;
#endif
}

static void prv_list_set_frame(TimelineLayer *layer, TimelineListPinView *pin, const GRect *frame) {
  layer_set_frame((Layer *)pin->layout, frame);
  GRect icon_frame;
  prv_list_get_icon_frame(layer, pin, frame, &icon_frame);
  layer_set_frame((Layer *)&pin->layout->icon_layer, &icon_frame);
}

//! Whether the pins after the selected one are below it on screen
static bool prv_list_next_is_down(TimelineLayer *layer) {
  return (prv_get_scroll_delta(layer) > 0);
}

//! Index of a timeline-ordered sequence on screen, from the top
static int prv_list_screen_index(TimelineLayer *layer, int num_pins, int index) {
  return prv_list_next_is_down(layer) ? index : (num_pins - 1 - index);
}

//! Adds pins on one side of the selected one until they cover the screen.
//! @param reused Pins of the previous list on that side, nearest first
//! @return number of pins written to side_out, nearest first
static int prv_list_fill_side(TimelineLayer *layer, TimelineListPinView *selected, int step,
                              TimelineListPinView **reused, int num_reused,
                              TimelineListPinView **side_out, bool *ends_timeline_out,
                              bool *ends_day_out) {
  const int16_t height = layer->layer.bounds.size.h;
  int16_t covered = 0;
  int num_pins = 0;
  TimelineListPinView *edge = selected;
  *ends_timeline_out = false;
  *ends_day_out = false;
  while (covered < height && num_pins < TIMELINE_LIST_MAX_PINS_PER_SIDE) {
    TimelineListPinView *pin;
    if (num_pins < num_reused) {
      pin = reused[num_pins];
      prv_list_pin_set_selected(pin, false);
    } else {
      pin = prv_list_pin_load(&edge->state, &edge->iter, step);
      if (!pin) {
        *ends_timeline_out = true;
        break;
      }
      // Pins of other days are behind the day separator
      if (pin->state.current_day != layer->current_day) {
        prv_list_pin_free(pin);
        *ends_day_out = true;
        break;
      }
      prv_list_pin_create_layout(layer, pin, false);
    }
    side_out[num_pins++] = pin;
    covered += prv_list_pin_height(pin);
    edge = pin;
  }
  return num_pins;
}

//! Fills the list around the model's current pin. Pins of the previous list that are still in it
//! are kept, the others move to the leaving pins.
static void prv_list_build(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  TimelineListPinView *old[TIMELINE_LIST_MAX_PINS];
  const int num_old = list->num_pins;
  memcpy(old, list->pins, sizeof(old));
  list->num_pins = 0;

  TimelineIterState *current = timeline_model_get_current_state();
  int old_selected = -1;
  for (int i = 0; current && i < num_old; i++) {
    if (old[i]->state.node == current->node) {
      old_selected = i;
    }
  }

  int first_kept = num_old;
  int last_kept = -1;
  if (current) {
    TimelineListPinView *selected;
    if (old_selected >= 0) {
      selected = old[old_selected];
      prv_list_pin_set_selected(selected, true);
      first_kept = last_kept = old_selected;
    } else {
      selected = prv_list_pin_load(current, timeline_model_get_current_iter(), 0);
      prv_list_pin_create_layout(layer, selected, true);
    }

    // The previous list is contiguous, so its pins around the selected one are the nearest ones
    TimelineListPinView *reused[TIMELINE_LIST_MAX_PINS_PER_SIDE];
    int num_reused = 0;
    for (int i = old_selected - 1;
         old_selected >= 0 && i >= 0 && num_reused < TIMELINE_LIST_MAX_PINS_PER_SIDE; i--) {
      reused[num_reused++] = old[i];
    }
    TimelineListPinView *before[TIMELINE_LIST_MAX_PINS_PER_SIDE];
    bool before_ends_timeline;
    bool before_ends_day;
    const int num_before = prv_list_fill_side(layer, selected, -1, reused, num_reused, before,
                                              &before_ends_timeline, &before_ends_day);
    first_kept -= PBL_MIN(num_before, num_reused);

    num_reused = 0;
    for (int i = old_selected + 1;
         old_selected >= 0 && i < num_old && num_reused < TIMELINE_LIST_MAX_PINS_PER_SIDE; i++) {
      reused[num_reused++] = old[i];
    }
    TimelineListPinView *after[TIMELINE_LIST_MAX_PINS_PER_SIDE];
    const int num_after = prv_list_fill_side(layer, selected, 1, reused, num_reused, after,
                                             &list->ends_timeline, &list->ends_day);
    last_kept += PBL_MIN(num_after, num_reused);
    list->starts_day = (before_ends_timeline || before_ends_day);

    for (int i = num_before - 1; i >= 0; i--) {
      list->pins[list->num_pins++] = before[i];
    }
    list->selected = list->num_pins;
    list->pins[list->num_pins++] = selected;
    for (int i = 0; i < num_after; i++) {
      list->pins[list->num_pins++] = after[i];
    }
  }

  for (int i = 0; i < num_old; i++) {
    if (i < first_kept || i > last_kept) {
      list->leaving[list->num_leaving++] = old[i];
    }
  }
}

#if PBL_ROUND
//! Narrows a pin so that its one-line text, aligned to the sidebar, ends before the display's
//! edge on the line of the pin farthest from the middle
static void prv_list_fit_to_display(TimelineLayer *layer, GRect *frame) {
  const GRect *bounds = &layer->layer.bounds;
  const int16_t center_y = bounds->size.h / 2;
  const int16_t top = PBL_CLAMP(frame->origin.y + TIMELINE_LAYOUT_COMPACT_PADDING_TOP, 0,
                                bounds->size.h);
  const int16_t bottom = PBL_CLAMP(grect_get_max_y(frame), 0, bounds->size.h);
  const int16_t distance_y = PBL_MAX(PBL_ABS(top - center_y), PBL_ABS(bottom - center_y));
  const int16_t edge_x = timeline_list_round_edge_inset(bounds->size.w / 2, distance_y);
  const int16_t inset = PBL_CLAMP(edge_x - prv_get_style()->left_margin, 0, frame->size.w / 2);
  frame->size.w -= inset;
  if (timeline_layer_sidebar_is_on_right()) {
    frame->origin.x += inset;
  }
}
#endif

//! Places the pins at their resting frames
static void prv_list_layout(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  const int num_pins = list->num_pins;
  if (num_pins == 0) {
    return;
  }
  TimelineListPin pins[TIMELINE_LIST_MAX_PINS];
  for (int i = 0; i < num_pins; i++) {
    const TimelineListPinView *pin = list->pins[i];
    pins[prv_list_screen_index(layer, num_pins, i)] = (TimelineListPin){
      .height = prv_list_pin_height(pin),
      .anchor_y = pin->anchor_y,
    };
  }
  const int16_t height = layer->layer.bounds.size.h;
  const bool next_is_down = prv_list_next_is_down(layer);
  // The round arrow stays in the middle; the rect list does not leave space before its first pin
  const bool fill_start = PBL_IF_RECT_ELSE(list->starts_day, false);
  const TimelineListGeometry geometry = {
    .height = height,
    .anchor_y = height / 2,
    .margin = LIST_EDGE_MARGIN,
    .fill_top = fill_start && next_is_down,
    .fill_bottom = fill_start && !next_is_down,
  };
  int16_t tops[TIMELINE_LIST_MAX_PINS];
  timeline_list_layout(&geometry, pins, num_pins,
                       prv_list_screen_index(layer, num_pins, list->selected), tops);

  const TimelineLayerStyle *style = prv_get_style();
  for (int i = 0; i < num_pins; i++) {
    const int screen_index = prv_list_screen_index(layer, num_pins, i);
    list->pins[i]->frame = (GRect){
      .origin = {style->left_margin, tops[screen_index]},
      .size = {layer->layer.bounds.size.w - (style->left_margin + style->right_margin),
               pins[screen_index].height},
    };
#if PBL_ROUND
    // The selected pin's lines may wrap, so it keeps its width and its height
    if (i != list->selected) {
      prv_list_fit_to_display(layer, &list->pins[i]->frame);
    }
#endif
  }
}

//! Stacks the frames of a timeline-ordered run of pins against the pins whose frames are known
static void prv_list_stack(TimelineLayer *layer, TimelineListPinView **run, int num_pins,
                           int first_known, int last_known, bool to_frames) {
  if (first_known > last_known) {
    return;
  }
  TimelineListPin pins[2 * TIMELINE_LIST_MAX_PINS];
  int16_t tops[2 * TIMELINE_LIST_MAX_PINS];
  for (int i = 0; i < num_pins; i++) {
    const GRect *frame = to_frames ? &run[i]->frame : &run[i]->from;
    const int screen_index = prv_list_screen_index(layer, num_pins, i);
    pins[screen_index] = (TimelineListPin){.height = frame->size.h};
    tops[screen_index] = frame->origin.y;
  }
  const int first = prv_list_screen_index(layer, num_pins, first_known);
  const int last = prv_list_screen_index(layer, num_pins, last_known);
  timeline_list_stack(pins, num_pins, PBL_MIN(first, last), PBL_MAX(first, last), tops);
  for (int i = 0; i < num_pins; i++) {
    GRect *frame = to_frames ? &run[i]->frame : &run[i]->from;
    frame->origin.y = tops[prv_list_screen_index(layer, num_pins, i)];
  }
}

//! Gives the pins that come in a frame to scroll from, and the pins that leave a frame to scroll
//! to, by stacking them against the pins on screen before and after the move
static void prv_list_place_offscreen(TimelineLayer *layer, TimelineListPinView **old, int num_old) {
  TimelineList *list = &layer->list;
  // The pins kept by the move are one run in both lists
  int old_start = num_old;
  int new_start = 0;
  for (int i = 0; i < num_old && old_start == num_old; i++) {
    for (int j = 0; j < list->num_pins; j++) {
      if (old[i] == list->pins[j]) {
        old_start = i;
        new_start = j;
        break;
      }
    }
  }
  int num_kept = 0;
  while ((old_start + num_kept < num_old) && (new_start + num_kept < list->num_pins) &&
         (old[old_start + num_kept] == list->pins[new_start + num_kept])) {
    num_kept++;
  }
  // Pins coming in start the size they rest at, pins leaving keep theirs
  for (int i = 0; i < list->num_pins; i++) {
    if (i < new_start || i >= new_start + num_kept) {
      list->pins[i]->from = list->pins[i]->frame;
    }
  }
  for (int i = 0; i < num_old; i++) {
    if (i < old_start || i >= old_start + num_kept) {
      old[i]->frame = old[i]->from;
    }
  }
  if (num_kept == 0) {
    return;
  }

  // On each side of the kept pins, pins either come in or leave, so in timeline order the pins of
  // both lists form one run: old before, new before, kept, old after, new after
  TimelineListPinView *run[2 * TIMELINE_LIST_MAX_PINS];
  int num_run = 0;
  for (int i = 0; i < old_start; i++) {
    run[num_run++] = old[i];
  }
  for (int i = 0; i < new_start + num_kept; i++) {
    run[num_run++] = list->pins[i];
  }
  const int num_old_after = num_old - (old_start + num_kept);
  for (int i = old_start + num_kept; i < num_old; i++) {
    run[num_run++] = old[i];
  }
  for (int i = new_start + num_kept; i < list->num_pins; i++) {
    run[num_run++] = list->pins[i];
  }
  const int first_old = (old_start > 0) ? 0 : new_start;
  const int last_old = old_start + new_start + num_kept + num_old_after - 1;
  const int first_new = old_start;
  const int last_new = (num_old_after > 0) ? (old_start + new_start + num_kept - 1) : (num_run - 1);
  prv_list_stack(layer, run, num_run, first_old, last_old, false /* to_frames */);
  prv_list_stack(layer, run, num_run, first_new, last_new, true /* to_frames */);
}

static TimelineListPinView *prv_list_get_selected(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  return (list->num_pins > 0) ? list->pins[list->selected] : nullptr;
}

//! Pin `offset` pins after the selected one, or nullptr
static TimelineListPinView *prv_list_get_pin(TimelineLayer *layer, int offset) {
  TimelineList *list = &layer->list;
  const int index = list->selected + offset;
  return (list->num_pins > 0 && index >= 0 && index < list->num_pins) ? list->pins[index] : nullptr;
}

static void prv_list_get_pin_icon_frame(TimelineLayer *layer, int offset, GRect *icon_frame) {
  TimelineListPinView *pin = prv_list_get_pin(layer, offset);
  if (!pin) {
    *icon_frame = GRectZero;
    return;
  }
  prv_list_get_icon_frame(layer, pin, &pin->frame, icon_frame);
}

//! The pin at the edge of the list in the given direction on screen
static TimelineListPinView *prv_list_get_edge_pin(TimelineLayer *layer, bool down) {
  TimelineList *list = &layer->list;
  if (list->num_pins == 0) {
    return nullptr;
  }
  const bool last = (down == prv_list_next_is_down(layer));
  return list->pins[last ? list->num_pins - 1 : 0];
}

//! Places an indicator of a height at a distance from the edge pin of the list in a direction
static void prv_list_place_indicator_frame(const GRect *edge_frame, bool down, int16_t height,
                                           GRect *frame) {
  frame->origin.y = down ? (grect_get_max_y(edge_frame) + LIST_INDICATOR_SPACING)
                         : (edge_frame->origin.y - LIST_INDICATOR_SPACING - height);
}

static void prv_list_place_day_sep(TimelineLayer *layer, const GRect *edge_frame, bool down) {
  GRect frame;
  prv_get_day_sep_frame(layer, 2, &frame);
  // The dot is drawn in the middle of the frame
  prv_list_place_indicator_frame(edge_frame, down, 0 /* height */, &frame);
  frame.origin.y -= frame.size.h / 2;
  layer_set_frame((Layer *)&layer->day_separator, &frame);
  layer_set_hidden((Layer *)&layer->day_separator, false);
}

//! Whether the end-of-timeline fin follows the list, and where
static bool prv_list_get_fin_frame(TimelineLayer *layer, GRect *frame) {
  const bool down = prv_list_next_is_down(layer);
  TimelineListPinView *edge = prv_list_get_edge_pin(layer, down);
  if (!edge || !layer->list.ends_timeline) {
    return false;
  }
  prv_get_end_of_timeline_frame(layer, 2, frame);
  const KinoReel *reel = kino_layer_get_reel(&layer->end_of_timeline);
  const int16_t fin_height = reel ? kino_reel_get_size((KinoReel *)reel).h : 0;
  prv_list_place_indicator_frame(&edge->frame, down, fin_height, frame);
  return true;
}

static void prv_list_place_indicators(TimelineLayer *layer) {
  layer_set_hidden((Layer *)&layer->day_separator, true);
  const bool down = prv_list_next_is_down(layer);
  TimelineListPinView *edge = prv_list_get_edge_pin(layer, down);
  if (edge && layer->list.ends_day) {
    prv_list_place_day_sep(layer, &edge->frame, down);
  }

  GRect fin_frame;
  const bool has_fin = prv_list_get_fin_frame(layer, &fin_frame);
  const bool was_hidden = layer_get_hidden((Layer *)&layer->end_of_timeline);
  layer_set_hidden((Layer *)&layer->end_of_timeline, !has_fin);
  if (has_fin) {
    layer_set_frame((Layer *)&layer->end_of_timeline, &fin_frame);
    if (was_hidden) {
      kino_layer_rewind(&layer->end_of_timeline);
    }
    kino_layer_play(&layer->end_of_timeline);
  }
}

static void prv_list_destroy_leaving(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  for (int i = 0; i < list->num_leaving; i++) {
    prv_list_pin_destroy(list->leaving[i]);
  }
  list->num_leaving = 0;
}

static void prv_list_move(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  prv_list_destroy_leaving(layer);
  for (int i = 0; i < list->num_pins; i++) {
    list->pins[i]->from = ((Layer *)list->pins[i]->layout)->frame;
  }
  TimelineIterState *current = timeline_model_get_current_state();
  if (current && current->current_day != layer->current_day) {
    // The pins scroll out ahead of the day separator, which comes in where the dot was
    const bool down = (layer->move_delta > 0);
    TimelineListPinView *edge = prv_list_get_edge_pin(layer, down);
    if (edge) {
      prv_list_place_day_sep(layer, &edge->from, down);
    }
    const int16_t shift = -layer->move_delta * layer->layer.bounds.size.h;
    for (int i = 0; i < list->num_pins; i++) {
      TimelineListPinView *pin = list->pins[i];
      pin->frame = pin->from;
      pin->frame.origin.y += shift;
      list->leaving[list->num_leaving++] = pin;
    }
    list->num_pins = 0;
    return;
  }

  // The dot comes back once the pins rest
  layer_set_hidden((Layer *)&layer->day_separator, true);
  TimelineListPinView *old[TIMELINE_LIST_MAX_PINS];
  const int num_old = list->num_pins;
  memcpy(old, list->pins, sizeof(old));
  TimelineListPinView *was_selected = prv_list_get_selected(layer);
  prv_list_build(layer);
  prv_list_layout(layer);
  prv_list_place_offscreen(layer, old, num_old);
  // The pin that was selected keeps its details while its frame shrinks over them, and takes its
  // resting mode when the pins stop
  if (was_selected && was_selected != prv_list_get_selected(layer)) {
    layout_set_mode((LayoutLayer *)was_selected->layout, prv_list_mode(true));
  }
}

static void prv_list_reset(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  prv_list_destroy_leaving(layer);
  // Pins are reloaded in case they changed
  for (int i = 0; i < list->num_pins; i++) {
    prv_list_pin_destroy(list->pins[i]);
  }
  list->num_pins = 0;
  TimelineIterState *current = timeline_model_get_current_state();
  if (current) {
    layer->current_day = current->current_day;
  }
  prv_list_build(layer);
  prv_list_layout(layer);
  // Pins slide in from the way of the last move, as after the day separator
  const int16_t slide = layer->move_delta * layer->layer.bounds.size.h / 2;
  for (int i = 0; i < list->num_pins; i++) {
    TimelineListPinView *pin = list->pins[i];
    prv_list_set_frame(layer, pin, &pin->frame);
    pin->from = pin->frame;
    pin->from.origin.y += slide;
  }
  prv_list_place_indicators(layer);
}

static Animation *prv_list_create_end_of_timeline_animation(TimelineLayer *layer, uint32_t duration,
                                                            InterpolateInt64Function interpolate) {
  Layer *fin = (Layer *)&layer->end_of_timeline;
  GRect to;
  if (!prv_list_get_fin_frame(layer, &to)) {
    layer_set_hidden(fin, true);
    return nullptr;
  }
  GRect from = fin->frame;
  if (layer_get_hidden(fin)) {
    // Come in with the selected pin
    const TimelineListPinView *selected = prv_list_get_selected(layer);
    from = to;
    from.origin.y += selected->from.origin.y - selected->frame.origin.y;
    layer_set_hidden(fin, false);
    kino_layer_rewind(&layer->end_of_timeline);
    kino_layer_play(&layer->end_of_timeline);
  }
  PropertyAnimation *prop_animation = property_animation_create_layer_frame(fin, &from, &to);
  Animation *animation = property_animation_get_animation(prop_animation);
  animation_set_duration(animation, duration);
  animation_set_custom_interpolation(animation, interpolate);
  animation_set_handlers(animation,
                         (AnimationHandlers){
                           .stopped = timeline_animation_layer_stopped_cut_to_end,
                         },
                         prop_animation);
  return animation;
}

//! Calls a function for each pin on screen or scrolling out
static void prv_list_each_pin(TimelineLayer *layer,
                              void (*callback)(TimelineLayer *layer, TimelineListPinView *pin)) {
  TimelineList *list = &layer->list;
  for (int i = 0; i < list->num_pins + list->num_leaving; i++) {
    callback(layer, (i < list->num_pins) ? list->pins[i] : list->leaving[i - list->num_pins]);
  }
}

//! Pins on screen and scrolling out, from the top of the screen
static int prv_list_get_stack(TimelineLayer *layer, TimelineListPinView **stack) {
  TimelineList *list = &layer->list;
  int num_pins = 0;
  for (int i = 0; i < list->num_pins + list->num_leaving; i++) {
    TimelineListPinView *pin =
        (i < list->num_pins) ? list->pins[i] : list->leaving[i - list->num_pins];
    int j = num_pins++;
    for (; j > 0 && stack[j - 1]->from.origin.y > pin->from.origin.y; j--) {
      stack[j] = stack[j - 1];
    }
    stack[j] = pin;
  }
  return num_pins;
}

//! Moves the pins as one stack: each pin ends where the next one starts, so a pin that grows
//! pushes the next one exactly as much. Interpolating each frame on its own would not keep them
//! together, as the interpolation overshoots by a fixed distance.
static void prv_list_frames_update(Animation *animation, AnimationProgress progress) {
  TimelineLayer *layer = animation_get_context(animation);
  const InterpolateInt64Function interpolate = layer->list.interpolate;
  TimelineListPinView *stack[2 * TIMELINE_LIST_MAX_PINS];
  const int num_pins = prv_list_get_stack(layer, stack);
  if (num_pins == 0) {
    return;
  }
  const TimelineListPinView *last = stack[num_pins - 1];
  int16_t bottom =
      interpolate(progress, grect_get_max_y(&last->from), grect_get_max_y(&last->frame));
  for (int i = num_pins - 1; i >= 0; i--) {
    TimelineListPinView *pin = stack[i];
    GRect frame = {
      .origin.x = interpolate(progress, pin->from.origin.x, pin->frame.origin.x),
      .origin.y = interpolate(progress, pin->from.origin.y, pin->frame.origin.y),
      .size.w = interpolate(progress, pin->from.size.w, pin->frame.size.w),
    };
    frame.size.h = bottom - frame.origin.y;
    layer_set_frame((Layer *)pin->layout, &frame);
    bottom = frame.origin.y;
  }
}

static void prv_list_start_pin_move(TimelineLayer *layer, TimelineListPinView *pin,
                                    uint32_t duration) {
  GRect icon_from;
  GRect icon_to;
  prv_list_get_icon_frame(layer, pin, &pin->from, &icon_from);
  prv_list_get_icon_frame(layer, pin, &pin->frame, &icon_to);
  prv_list_set_frame(layer, pin, &pin->from);
  // A pin growing into the selected one reveals its details as its frame grows
  layer_set_clips((Layer *)pin->layout, true);
  timeline_layout_animate_icon_up_down(pin->layout, &icon_from, &icon_to, duration,
                                       layer->list.interpolate);
}

static void prv_list_finish_pin_move(TimelineLayer *layer, TimelineListPinView *pin) {
  prv_list_set_frame(layer, pin, &pin->frame);
  timeline_layout_finish_icon_up_down(pin->layout);
  // At rest, glyphs may reach past the frame
  layer_set_clips((Layer *)pin->layout, false);
}

#define MAX_LIST_UP_DOWN_ANIMATIONS (2 * TIMELINE_LIST_MAX_PINS + 3)

static void prv_list_add_animation(Animation **animations, int *num_animations,
                                   Animation *animation) {
  if (animation) {
    animations[(*num_animations)++] = animation;
  }
}

static Animation *prv_list_create_up_down_animation(TimelineLayer *layer, uint32_t duration,
                                                    InterpolateInt64Function interpolate) {
  TimelineList *list = &layer->list;
  list->interpolate = interpolate;

  Animation *animations[MAX_LIST_UP_DOWN_ANIMATIONS] = {};
  int num_animations = 0;
  static const AnimationImplementation s_frames_implementation = {
    .update = prv_list_frames_update,
  };
  Animation *frames = animation_create();
  animation_set_implementation(frames, &s_frames_implementation);
  animation_set_handlers(frames, (AnimationHandlers){}, layer);
  animation_set_duration(frames, duration);
  prv_list_add_animation(animations, &num_animations, frames);
  for (int i = 0; i < list->num_pins + list->num_leaving; i++) {
    TimelineListPinView *pin =
        (i < list->num_pins) ? list->pins[i] : list->leaving[i - list->num_pins];
    prv_list_start_pin_move(layer, pin, duration);
    prv_list_add_animation(
        animations, &num_animations,
        (Animation *)kino_layer_create_play_section_animation(
            &pin->layout->icon_layer, 0, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS));
  }
  prv_list_add_animation(animations, &num_animations,
                         prv_list_create_end_of_timeline_animation(layer, duration, interpolate));
#if PBL_RECT
  prv_list_add_animation(animations, &num_animations,
                         timeline_relbar_layer_create_animation(layer, duration, interpolate));
#endif
  return animation_spawn_create_from_array(animations, num_animations);
}

static void prv_list_up_down_stopped(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  prv_list_each_pin(layer, prv_list_finish_pin_move);
  for (int i = 0; i < list->num_pins; i++) {
    prv_list_pin_set_selected(list->pins[i], (i == list->selected));
  }
  prv_list_destroy_leaving(layer);
  if (!timeline_layer_should_animate_day_separator(layer)) {
    prv_list_place_indicators(layer);
  }
}

static void prv_list_set_hidden(TimelineLayer *layer, bool hidden) {
  TimelineList *list = &layer->list;
  for (int i = 0; i < list->num_pins + list->num_leaving; i++) {
    TimelineListPinView *pin =
        (i < list->num_pins) ? list->pins[i] : list->leaving[i - list->num_pins];
    prv_set_layout_hidden(pin->layout, hidden);
  }
}

static void prv_list_deinit(TimelineLayer *layer) {
  TimelineList *list = &layer->list;
  prv_list_destroy_leaving(layer);
  for (int i = 0; i < list->num_pins; i++) {
    prv_list_pin_destroy(list->pins[i]);
  }
  list->num_pins = 0;
}

#define MAX_UP_DOWN_ANIMATIONS (2 * TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER + 1)

static Animation *prv_create_up_down_animation(TimelineLayer *layer, uint32_t duration,
                                               InterpolateInt64Function interpolate) {
  Animation *animations[MAX_UP_DOWN_ANIMATIONS] = {};
  int num_animations = 0;
  for (int i = 0; i < TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER; i++) {
    if (layer->layouts[i]) {
      animations[num_animations++] =
          prv_create_layout_up_down_animation(layer->layouts[i], i, layer, duration, interpolate);
      animations[num_animations++] = (Animation *)kino_layer_create_play_section_animation(
          &layer->layouts[i]->icon_layer, 0, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS);
    } else if (i == 2 || i == 3) {
      animations[num_animations++] =
          prv_create_end_of_timeline_animation(layer, i, duration, interpolate);
      break;
    }
  }

#if PBL_RECT
  Animation *relbar_animation =
      timeline_relbar_layer_create_animation(layer, duration, interpolate);
  if (relbar_animation) {
    animations[num_animations++] = relbar_animation;
  }
#endif

  Animation *animation = animation_spawn_create_from_array(animations, num_animations);
  return animation;
}

static void prv_place_day_separator(TimelineLayer *layer) {
  if (!layer_get_hidden((Layer *)&layer->day_separator)) {
    // already on screen
    return;
  }

  GRect day_sep_frame = ((Layer *)&layer->day_separator)->frame;
  // substitute the day separator for the hidden pin
  TimelineLayout *prev = layer->layouts[s_nonvisible_items[0]];
  TimelineLayout *next = layer->layouts[s_visible_items[1]];
  if (prev && layer_get_hidden(&prev->layout_layer.layer)) {
    prv_get_day_sep_frame(layer, 0, &day_sep_frame);
  } else if (next && layer_get_hidden(&next->layout_layer.layer)) {
    prv_get_day_sep_frame(layer, 2, &day_sep_frame);
  } else {
    // don't show the day separator
    return;
  }

  layer_set_frame((Layer *)&layer->day_separator, &day_sep_frame);
  layer_set_hidden((Layer *)&layer->day_separator, false);
}

static void prv_place_end_of_timeline(TimelineLayer *timeline_layer) {
  const bool was_hidden = layer_get_hidden((Layer *)&timeline_layer->end_of_timeline);
  const bool is_hidden =
      (timeline_layer_should_animate_day_separator(timeline_layer) || timeline_layer->layouts[2]);
  layer_set_hidden((Layer *)&timeline_layer->end_of_timeline, is_hidden);
  GRect frame;
  prv_get_end_of_timeline_frame(timeline_layer, is_hidden ? 3 : 2, &frame);
  layer_set_frame((Layer *)&timeline_layer->end_of_timeline, &frame);
  if (was_hidden) {
    kino_layer_rewind(&timeline_layer->end_of_timeline);
  }
  kino_layer_play(&timeline_layer->end_of_timeline);
};

static void prv_up_down_stopped(Animation *animation, bool is_finished, void *context) {
  TimelineLayer *layer = context;
  if (layer->compact) {
    prv_list_up_down_stopped(layer);
    return;
  }
  prv_update_pins_mode(layer);
  prv_set_layouts_to_final_position(layer);
  prv_destroy_nonvisible_items(layer);
  prv_place_day_separator(layer);
  prv_place_end_of_timeline(layer);
}

static void prv_mode_change_update(Animation *animation, AnimationProgress normalized) {
  TimelineLayer *timeline_layer = animation_get_context(animation);
  const AnimationProgress bounce_back_length =
      (interpolate_moook_out_duration() * ANIMATION_NORMALIZED_MAX) / interpolate_moook_duration();
  if (normalized >= ANIMATION_NORMALIZED_MAX - bounce_back_length) {
    prv_update_pins_mode(timeline_layer);
    animation_unschedule(animation);
  }
}

Animation *timeline_layer_create_up_down_animation(TimelineLayer *layer, uint32_t duration,
                                                   InterpolateInt64Function interpolate) {
  Animation *animation =
      (layer->compact ? prv_list_create_up_down_animation : prv_create_up_down_animation)(
          layer, duration, interpolate);

  animation_set_handlers(animation,
                         (AnimationHandlers){
                           .stopped = prv_up_down_stopped,
                         },
                         layer);

  if (layer->compact) {
    // Compact pins change mode as the move starts; wrap so the caller's handlers keep this one
    return animation_spawn_create_from_array(&animation, 1);
  }

  static const AnimationImplementation s_mode_change_impl = {
    .update = prv_mode_change_update,
  };

  Animation *mode_change = animation_create();
  animation_set_implementation(mode_change, &s_mode_change_impl);
  animation_set_handlers(mode_change, (AnimationHandlers){}, layer);

  return animation_spawn_create(animation, mode_change, nullptr);
}

#if PBL_ROUND
//! sidebar_x is the strip's origin as if it were on the right; mirrored when it is on the left.
static void prv_draw_round_flip(GContext *ctx, const GRect *layer_bounds, const int sidebar_x,
                                bool on_right) {
  // Use a radius larger than the screen's radius so we don't see the top/bottom of the circle
  int16_t circle_radius = PBL_DISPLAY_WIDTH * 3 / 4;
  const int16_t flip_overlap_region_width = layer_bounds->size.w / 5;
  const GPoint bounds_center = grect_center_point(layer_bounds);
  const int16_t flip_point_x = bounds_center.x - (flip_overlap_region_width / 2);
  // If the origin x value is not past the flip point, draw a colored circle starting at the x pos
  if (sidebar_x <= flip_point_x) {
    // Don't draw the circle in the flip region overlap; we want an instantaneous jump past this
    // region during the flip
    const int16_t circle_left_edge_x = PBL_MIN(sidebar_x, flip_point_x);
    GPoint circle_center = GPoint(circle_left_edge_x + circle_radius, bounds_center.y);
    if (!on_right) {
      circle_center.x = layer_bounds->size.w - circle_center.x;
    }
    graphics_fill_circle(ctx, circle_center, circle_radius);
  } else {
    // Otherwise, use fill_radial to fill the sidebar as a radial on the sidebar side of the screen
    GPoint circle_center = GPoint(sidebar_x - circle_radius, bounds_center.y);
    const int16_t outer_radius = layer_bounds->size.w - circle_center.x;
    if (!on_right) {
      circle_center.x = layer_bounds->size.w - circle_center.x;
    }
    // Add half the final sidebar width to the radius so we see a bounce-back effect at the end
    const TimelineLayerStyle *style = prv_get_style();
    circle_radius += style->sidebar_width / 2;
    graphics_fill_radial_internal(ctx, circle_center, circle_radius, outer_radius, 0,
                                  TRIG_MAX_ANGLE);
  }
}
#endif

static void prv_update_proc(struct Layer *layer, GContext *ctx) {
  TimelineLayer *timeline_layer = (TimelineLayer *)layer;
  const GRect *bounds = &layer->bounds;

  graphics_context_set_fill_color(ctx, system_theme_get_bg_color());
  graphics_fill_rect(ctx, &(GRect){.size = bounds->size});

  // The theme can change while the timeline is open, so keep the stored text color in sync. This
  // only marks the day separator dirty when the color actually changes.
  peek_layer_set_text_color(&timeline_layer->day_separator, system_theme_get_fg_color());
  // The fin is a child layer, so it draws after this and picks up the current theme.
  kino_layer_set_invert_colors(&timeline_layer->end_of_timeline, system_theme_is_dark_mode());

  AnimationProgress progress;
  if (timeline_layer->animating_intro_or_exit &&
      animation_get_progress(timeline_layer->animation, &progress)) {
    const GPoint offset = {
      PEEK_ANIMATIONS_SPEED_LINES_OFFSET_X,
      interpolate_int64_linear(progress, 0, -PBL_DISPLAY_HEIGHT)
    };
    graphics_context_set_fill_color(ctx, system_theme_get_fg_color());
    peek_animations_draw_timeline_speed_lines(ctx, offset);
  }

  const int16_t sidebar_width = timeline_layer->sidebar_width;
  const int16_t sidebar_x =
      timeline_layer_sidebar_is_on_right() ? (bounds->size.w - sidebar_width) : 0;
  const GRect sidebar_rect = GRect(sidebar_x, 0, sidebar_width, bounds->size.h);
  graphics_context_set_fill_color(ctx, timeline_layer->sidebar_color);

  // On round displays, draw the round flip effect if we're animating the intro or exit and then
  // return early so we don't draw the arrow notch
#if PBL_ROUND
  if (timeline_layer->animating_intro_or_exit) {
    prv_draw_round_flip(ctx, bounds, bounds->size.w - sidebar_width,
                        timeline_layer_sidebar_is_on_right());
    return;
  }
#endif

  graphics_fill_rect(ctx, &sidebar_rect);
  const bool sidebar_on_right = timeline_layer_sidebar_is_on_right();
  int16_t arrow_base_x = sidebar_on_right ? (bounds->size.w - sidebar_width) : (sidebar_width - 1);
#if PBL_ROUND
  arrow_base_x += sidebar_on_right ? -1 : 1;
#endif
  const TimelineLayerStyle *style = prv_get_style();
  const GSize arrow_size = style->sidebar_arrow_size;
  int16_t arrow_base_center_y = PBL_IF_RECT_ELSE(16, bounds->size.h / 2);
#if PBL_RECT
  // The compact list moves the selected pin, so the arrow follows its icon
  TimelineListPinView *selected =
      timeline_layer->compact ? prv_list_get_selected(timeline_layer) : nullptr;
  if (selected) {
    GRect icon_frame;
    prv_list_get_icon_frame(timeline_layer, selected, &((Layer *)selected->layout)->frame,
                            &icon_frame);
    arrow_base_center_y = icon_frame.origin.y + icon_frame.size.h / 2;
  }
#endif
  const int16_t toward_content = sidebar_on_right ? -1 : 1;
  const int16_t arrow_point_x_offset =
      PBL_IF_RECT_ELSE(toward_content, -toward_content) * arrow_size.w;
  GPath arrow_path = {
    .num_points = 3,
    .points = (GPoint[]){
      {arrow_base_x, arrow_base_center_y - (arrow_size.h / 2)},
      {arrow_base_x + arrow_point_x_offset, arrow_base_center_y},
      {arrow_base_x, arrow_base_center_y + (arrow_size.h / 2)}
    }
  };

  if (timeline_layer->scroll_direction == TimelineScrollDirectionUp && !timeline_layer->compact) {
    // arrow is in a different position for past & future, but only on rectangular displays
    gpath_move_to(
        &arrow_path,
        PBL_IF_RECT_ELSE(GPoint(0, (style->thin_pin_height + style->past_thin_pin_margin)),
                         GPointZero));
  }

  graphics_context_set_antialiased(ctx, true);
  // On round the arrow is a notch cut into the sidebar, so it takes the background color
  const GColor arrow_fill_color =
      PBL_IF_RECT_ELSE(timeline_layer->sidebar_color, system_theme_get_bg_color());
  graphics_context_set_fill_color(ctx, arrow_fill_color);
  gpath_draw_filled(ctx, &arrow_path);
  graphics_context_set_antialiased(ctx, false);
}

/////////////////////////////////////////
// Public functions
/////////////////////////////////////////

// when we create a new next or previous item, we want it out of view so we can animate it in
void timeline_layer_set_next_item(TimelineLayer *layer, int index) {
  if (layer->compact) {
    // The compact list loads the pins it needs as it moves
    return;
  }
  PBL_LOG_DBG("Setting next item with index %d", index);
  TimelineIterState *iter_state = timeline_model_get_iter_state_with_timeline_idx(index);
  if (!iter_state) {
    return;
  }
  if (layer->layouts[s_nonvisible_items[1]]) {
    prv_destroy_layout(layer, s_nonvisible_items[1]);
  }
  prv_create_layout(layer, iter_state, s_nonvisible_items[1]);
}

void timeline_layer_set_prev_item(TimelineLayer *layer, int index) {
  if (layer->compact) {
    return;
  }
  PBL_LOG_DBG("Setting prev item with index %d", index);
  TimelineIterState *iter_state = timeline_model_get_iter_state_with_timeline_idx(index);
  if (!iter_state) {
    return;
  }
  if (layer->layouts[s_nonvisible_items[0]]) {
    prv_destroy_layout(layer, s_nonvisible_items[0]);
  }
  prv_create_layout(layer, iter_state, s_nonvisible_items[0]);

  if (iter_state->current_day != layer->current_day) {
    // we moved back to an item from the previous day, display the day separator
    // by first hiding the pin of the "last" day that is about to come in and placing it there
    if (layer->layouts[0]) {
      prv_set_layout_hidden(layer->layouts[0], true);
    }
    prv_place_day_separator(layer);
  } else {
    // continue to hide the day separator
    layer_set_hidden((Layer *)&layer->day_separator, true);
  }
}

bool timeline_layer_should_animate_day_separator(TimelineLayer *layer) {
  return !layer_get_hidden((Layer *)&layer->day_separator);
}

void timeline_layer_move_data(TimelineLayer *layer, int delta) {
  PBL_ASSERTN(delta == 1 || delta == -1);
  if (layer->compact) {
    layer->move_delta = delta * prv_get_scroll_delta(layer);
    prv_list_move(layer);
    return;
  }
  if (delta == 1) {
    if (layer->layouts[s_nonvisible_items[0]]) {
      prv_destroy_layout(layer, s_nonvisible_items[0]);
    }
    for (int i = 0; i < TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER - 1; i++) {
      layer->layouts[i] = layer->layouts[i + 1];
      layer->layouts_info[i] = layer->layouts_info[i + 1];
    }
    layer->layouts[TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER - 1] = nullptr;
    layer->layouts_info[TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER - 1] = nullptr;
  } else if (delta == -1) {
    if (layer->layouts[s_nonvisible_items[1]]) {
      prv_destroy_layout(layer, s_nonvisible_items[1]);
    }
    for (int i = TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER - 1; i > 0; i--) {
      layer->layouts[i] = layer->layouts[i - 1];
      layer->layouts_info[i] = layer->layouts_info[i - 1];
    }
    layer->layouts[0] = nullptr;
    layer->layouts_info[0] = nullptr;
  }

  layer->move_delta = delta * prv_get_scroll_delta(layer);

  // hide other day's pins before the animation shows them
  prv_hide_non_current_day_items(layer);
}

TimelineLayout *timeline_layer_get_current_layout(TimelineLayer *timeline_layer) {
  if (timeline_layer->compact) {
    TimelineListPinView *selected = prv_list_get_selected(timeline_layer);
    return selected ? selected->layout : nullptr;
  }
  return timeline_layer->layouts[TIMELINE_LAYER_FIRST_VISIBLE_LAYOUT];
}

void timeline_layer_get_current_icon_frame(TimelineLayer *layer, GRect *frame_out) {
  if (layer->compact) {
    prv_list_get_pin_icon_frame(layer, 0, frame_out);
    return;
  }
  GRect frame;
  prv_get_frame(layer, TIMELINE_LAYER_FIRST_VISIBLE_LAYOUT, &frame);
  timeline_layout_get_icon_frame(&frame, layer->scroll_direction, frame_out);
}

TimelineLayoutInfo *timeline_layer_get_layout_info(TimelineLayer *layer, int index) {
  if (layer->compact) {
    TimelineListPinView *pin = prv_list_get_pin(layer, index - TIMELINE_LAYER_FIRST_VISIBLE_LAYOUT);
    return pin ? &pin->info : nullptr;
  }
  return (index >= 0 && index < TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER) ? layer->layouts_info[index]
                                                                      : nullptr;
}

void timeline_layer_reset(TimelineLayer *layer) {
  // reset the animation
  animation_unschedule(layer->animation);
  layer->animation = nullptr;

  // reset the day separator
  layer_set_hidden((Layer *)&layer->day_separator, true);

  TimelineResourceInfo timeline_res = {
    .res_id = TIMELINE_RESOURCE_DAY_SEPARATOR,
  };
  peek_layer_set_icon(&layer->day_separator, &timeline_res);

  if (layer->compact) {
    prv_list_reset(layer);
#if PBL_RECT
    timeline_relbar_layer_reset(layer);
#endif
    return;
  }

  // reset the layouts
  prv_reset_layouts(layer);
  prv_destroy_nonvisible_items(layer);
  timeline_layer_set_layouts_hidden(layer, false);

  // Reset the current day in case the pin was deleted
  const int index = TIMELINE_LAYER_FIRST_VISIBLE_LAYOUT;
  if (layer->layouts[index]) {
    layer->current_day = layer->layouts[index]->info->current_day;
  }

  prv_hide_non_current_day_items(layer);
  prv_place_day_separator(layer);
  prv_place_end_of_timeline(layer);

#if PBL_RECT
  timeline_relbar_layer_reset(layer);
#endif
}

void timeline_layer_set_sidebar_color(TimelineLayer *timeline_layer, GColor color) {
  timeline_layer->sidebar_color = color;
}

void timeline_layer_set_sidebar_width(TimelineLayer *timeline_layer, int16_t width) {
  timeline_layer->sidebar_width = width;
}

static void prv_sidebar_setter(void *context, int16_t value) {
  TimelineLayer *timeline_layer = context;
  timeline_layer->sidebar_width = value;
  layer_mark_dirty((Layer *)timeline_layer);
}

static int16_t prv_sidebar_getter(void *context) {
  TimelineLayer *timeline_layer = context;
  return timeline_layer->sidebar_width;
}

Animation *timeline_layer_create_sidebar_animation(TimelineLayer *timeline_layer,
                                                   int16_t to_sidebar_width) {
  static const PropertyAnimationImplementation s_implementation = {
    .base.update = (AnimationUpdateImplementation)property_animation_update_int16,
    .accessors.setter.int16 = prv_sidebar_setter,
    .accessors.getter.int16 = prv_sidebar_getter,
  };
  Animation *animation = (Animation *)property_animation_create(
      &s_implementation, timeline_layer, &timeline_layer->sidebar_width, &to_sidebar_width);
  animation_set_duration(animation, interpolate_moook_in_duration());
  animation_set_custom_interpolation(animation, interpolate_moook_in_only);
  return animation;
}

static void prv_speed_lines_update(Animation *animation, AnimationProgress progress) {
}

Animation *timeline_layer_create_speed_lines_animation(TimelineLayer *timeline_layer) {
  static const AnimationImplementation s_speed_lines_impl = {
    .update = prv_speed_lines_update,
  };
  Animation *animation = animation_create();
  animation_set_implementation(animation, &s_speed_lines_impl);
  const unsigned int num_jump_frames = 3;
  animation_set_duration(animation, num_jump_frames * ANIMATION_TARGET_FRAME_INTERVAL_MS);
  timeline_layer->animation = animation;
  return animation;
}

static Animation *prv_create_bounce_back_animation(Layer *layer, const GRect *to_orig,
                                                   GPoint direction) {
  GRect from = *to_orig;
  GRect to = *to_orig;
  gpoint_add_eq(&from.origin, GPoint(direction.x * INTERPOLATE_MOOOK_BOUNCE_BACK,
                                     direction.y * INTERPOLATE_MOOOK_BOUNCE_BACK));
  layer_set_frame(layer, &from);
  Animation *animation = (Animation *)property_animation_create_layer_frame(layer, &from, &to);
  animation_set_curve(animation, AnimationCurveEaseOut);
  animation_set_duration(animation, TIMELINE_LAYER_SLIDE_MS);
  animation_set_handlers(animation,
                         (AnimationHandlers){
                           .stopped = timeline_animation_layer_stopped_cut_to_end,
                         },
                         animation);
  return animation;
}

Animation *timeline_layer_create_bounce_back_animation(TimelineLayer *layer, GPoint direction) {
  Animation *animations[TIMELINE_LIST_MAX_PINS + 2] = {};
  int num_animations = 0;

  for (int i = 0; layer->compact && i < layer->list.num_pins; i++) {
    TimelineListPinView *pin = layer->list.pins[i];
    animations[num_animations++] =
        prv_create_bounce_back_animation((Layer *)pin->layout, &pin->frame, direction);
  }
  for (int i = 0; !layer->compact && i < TIMELINE_NUM_VISIBLE_ITEMS; i++) {
    TimelineLayout *layout = layer->layouts[s_visible_items[i]];
    if (layout) {
      GRect frame;
      prv_get_frame(layer, i + 1, &frame);
      animations[num_animations++] =
          prv_create_bounce_back_animation((Layer *)layout, &frame, direction);
    }
  }

  const GRect *day_sep_from = &((Layer *)&layer->day_separator)->frame;
  animations[num_animations++] =
      prv_create_bounce_back_animation((Layer *)&layer->day_separator, day_sep_from, direction);

  const GRect *fin_from = &layer->end_of_timeline.layer.frame;
  animations[num_animations++] =
      prv_create_bounce_back_animation(&layer->end_of_timeline.layer, fin_from, direction);

  Animation *animation = animation_spawn_create_from_array(animations, num_animations);
  return animation;
}

void timeline_layer_set_layouts_hidden(TimelineLayer *layer, bool hidden) {
  prv_list_set_hidden(layer, hidden);
  for (int i = 0; i < TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER; i++) {
    TimelineLayout *layout = layer->layouts[i];
    if (layout) {
      prv_set_layout_hidden(layout, hidden);
    }
  }

  layer_set_hidden((Layer *)&layer->end_of_timeline, hidden);
}

void timeline_layer_init(TimelineLayer *layer, const GRect *frame_ref,
                         TimelineScrollDirection scroll_direction) {
  *layer = (TimelineLayer){};
  // timeline layer
  layer_init(&layer->layer, frame_ref);
  layer_set_clips(&layer->layer, false);
  layer_set_update_proc(&layer->layer, prv_update_proc);
  TimelineIterState *state = timeline_model_get_current_state();
  layer->current_day = PBL_NULL_SAFE_FIELD_ACCESS(state, current_day, 0);
  // Smaller text leaves room for more pins than the fixed slots show
  layer->compact = (system_theme_get_content_size() < PreferredContentSizeDefault);
  const TimelineLayerStyle *style = prv_get_style();
  // The arrow is inverted on round, so hide it by extending the width of the sidebar
  layer->sidebar_width = frame_ref->size.w + PBL_IF_ROUND_ELSE(style->sidebar_arrow_size.w, 0);
  // layouts
  layer->scroll_direction = scroll_direction;
  layer->move_delta = prv_get_scroll_delta(layer);
  // layouts layer - contains all the pin
  layer_init(&layer->layouts_layer, &(GRect){.size = frame_ref->size});
  layer_set_clips(&layer->layouts_layer, false);
  layer_add_child((Layer *)layer, (Layer *)&layer->layouts_layer);

  // day separator
  GRect frame;
  prv_get_day_sep_show_frame(layer, &frame);
  peek_layer_init(&layer->day_separator, &frame);
  const GFont title_font = system_theme_get_font(TextStyleFont_Title);
  peek_layer_set_title_font(&layer->day_separator, title_font);
  const GFont subtitle_font = system_theme_get_font(TextStyleFont_PinSubtitle);
  peek_layer_set_subtitle_font(&layer->day_separator, subtitle_font,
                               style->day_sep_subtitle_margin);

  TimelineResourceInfo timeline_res = {
    .res_id = TIMELINE_RESOURCE_DAY_SEPARATOR,
  };
  peek_layer_set_icon(&layer->day_separator, &timeline_res);
  peek_layer_set_background_color(&layer->day_separator, GColorClear);
  peek_layer_set_text_color(&layer->day_separator, system_theme_get_fg_color());
  peek_layer_set_dot_diameter(&layer->day_separator, style->day_sep_dot_diameter);
  layer_set_hidden((Layer *)&layer->day_separator, true);
  layer_add_child((Layer *)layer, (Layer *)&layer->day_separator);

  // end-of-timeline indicator
  prv_get_end_of_timeline_frame(layer, 3, &frame);
  kino_layer_init(&layer->end_of_timeline, &frame);
  kino_layer_set_reel_with_resource(&layer->end_of_timeline, RESOURCE_ID_END_OF_TIMELINE);
  kino_layer_set_alignment(&layer->end_of_timeline, GAlignTop);
  layer_add_child((Layer *)layer, (Layer *)&layer->end_of_timeline);

  // populate the timeline with items
  timeline_layer_reset(layer);

#if PBL_RECT
  // Initialize Relationship bar
  timeline_relbar_layer_init(layer);
#endif
}

void timeline_layer_deinit(TimelineLayer *layer) {
  animation_unschedule_all();
  prv_list_deinit(layer);
  for (int i = 0; i < TIMELINE_NUM_ITEMS_IN_TIMELINE_LAYER; i++) {
    if (layer->layouts[i]) {
      prv_destroy_layout(layer, i);
    }
  }
  peek_layer_deinit(&layer->day_separator);
  kino_layer_deinit(&layer->end_of_timeline);

#if PBL_RECT
  timeline_relbar_layer_deinit(layer);
#endif
}
