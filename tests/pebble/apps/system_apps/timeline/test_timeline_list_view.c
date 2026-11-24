/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "test_timeline_app_includes.h"

#include <pbl/kernel/assert.h>
#include <pbl/services/timeline/notification_layout.h>
#include <pbl/services/timeline/timeline_resources.h>
#include <pbl/util/math.h>
#include <pbl/util/units.h>

#include <apps/system/timeline/animations.h>
#include <apps/system/timeline/timeline.h>
#include <fake_settings.h>
#include <fixtures/screen_grid.h>
#include <resource/resource.h>
#include <resource/resource_ids.auto.h>

// Setup and Teardown
////////////////////////////////////

typedef struct TimelineTestData {
  TimelineModel model;
} TimelineTestData;

TimelineTestData s_data;
static bool s_relationship_only;
static bool s_action_bar_on_right = true;

bool action_bar_layer_is_on_right(void) {
  return s_action_bar_on_right;
}

void test_timeline_list_view__initialize(void) {
  s_action_bar_on_right = true;
  s_relationship_only = false;
  fake_app_state_init();
  load_system_resources_fixture();

  s_data = (TimelineTestData){};
  pbl_realtime_set(3 * PBL_SEC_PER_DAY);
}

void test_timeline_list_view__cleanup(void) {
  system_theme_set_content_size(PreferredContentSizeDefault);
}

// Helpers
//////////////////////

typedef struct TimelineItemConfig {
  time_t relative_timestamp;
  uint16_t duration;
  const char *title;
  const char *subtitle;
  const char *body;
  TimelineResourceId icon;
  bool all_day;
} TimelineItemConfig;

typedef struct ListViewConfig {
  TimelineItemConfig *pins[6];
  bool past;
  bool day_separator;
  //! Number of pins to scroll past before rendering
  int num_scrolls;
  //! Progress of the last scroll when rendering, or 0 to let it end
  AnimationProgress scroll_progress;
  //! Number of pages to scroll through the selected pin's text after the scrolls
  int num_page_scrolls;
  //! Progress of a scroll to the next pin after the page scrolls, or 0 for none
  AnimationProgress then_scroll_progress;
} ListViewConfig;

static void prv_add_timeline_item(const TimelineItemConfig *config, bool past, uint8_t pin_index) {
  PBL_ASSERTN(config);
  TimelineItem *item = nullptr;
  const time_t now = pbl_realtime();
  const time_t timestamp = now + ((past ? -1 : 1) * config->relative_timestamp);
  if (config) {
    AttributeList list;
    attribute_list_init_list(0 /* num_attributes */, &list);
    attribute_list_add_cstring(&list, AttributeIdTitle, config->title);
    if (config->subtitle) {
      attribute_list_add_cstring(&list, AttributeIdSubtitle, config->subtitle);
    }
    if (config->body) {
      attribute_list_add_cstring(&list, AttributeIdBody, config->body);
    }
    attribute_list_add_uint32(&list, AttributeIdIconPin, config->icon);
    item = timeline_item_create_with_attributes(timestamp, config->duration, TimelineItemTypePin,
                                                LayoutIdGeneric, &list, nullptr);
    attribute_list_destroy_list(&list);
    PBL_ASSERTN(item);
    item->header.all_day = config->all_day;
    // Use distinct keys in the fake settings store, which cannot handle CRC8 collisions.
    item->header.id = (Uuid){.byte15 = pin_index + 1};
  }
  if (item) {
    pin_db_insert_item(item);
    timeline_item_destroy(item);
  }
}

#if PBL_ROUND
//! Hides or shows what the compact list draws close to the selected pin: the other pins, the
//! end-of-timeline fin and the day separator dot
static void prv_set_neighbours_hidden(TimelineLayer *timeline_layer, bool hidden) {
  if (!timeline_layer->compact) {
    return;
  }
  static bool s_was_hidden[2];
  Layer *indicators[] = {
    (Layer *)&timeline_layer->end_of_timeline, (Layer *)&timeline_layer->day_separator
  };
  for (size_t i = 0; i < PBL_ARRAY_SIZE(indicators); i++) {
    if (hidden) {
      s_was_hidden[i] = layer_get_hidden(indicators[i]);
    }
    layer_set_hidden(indicators[i], hidden || s_was_hidden[i]);
  }
  TimelineList *list = &timeline_layer->list;
  for (int i = 0; i < list->num_pins; i++) {
    if (i != list->selected) {
      layer_set_hidden((Layer *)list->pins[i]->layout, hidden);
      layer_set_hidden((Layer *)&list->pins[i]->layout->icon_layer, hidden);
    }
  }
}

static void prv_assert_title_centered(Window *window, TimelineLayer *timeline_layer) {
  TimelineLayout *layout = timeline_layer_get_current_layout(timeline_layer);
  prv_set_neighbours_hidden(timeline_layer, true);
  GTextNodeContainer *container = (GTextNodeContainer *)layout->view_node;
  GTextNode *time_node = container->nodes[0];
  GTextNodeText *other_text[4];
  size_t num_other_text = 0;
  if (time_node->type == GTextNodeType_Horizontal) {
    GTextNodeContainer *time_container = (GTextNodeContainer *)time_node;
    for (size_t i = 0; i < time_container->num_nodes; i++) {
      other_text[num_other_text++] = (GTextNodeText *)time_container->nodes[i];
    }
  } else {
    other_text[num_other_text++] = (GTextNodeText *)time_node;
  }
  // The location and the description follow the title
  for (size_t i = 2; i < container->num_nodes; i++) {
    other_text[num_other_text++] = (GTextNodeText *)container->nodes[i];
  }
  GColor colors[4];
  for (size_t i = 0; i < num_other_text; i++) {
    colors[i] = other_text[i]->color;
    other_text[i]->color = GColorWhite;
  }
  window_render(window, fake_graphics_context_get_context());
  const GBitmap *bitmap = &fake_graphics_context_get_context()->dest_bitmap;
  int16_t top = PBL_DISPLAY_HEIGHT;
  int16_t bottom = 0;
  // The text side, clear of the sidebar and its icons
  const int16_t text_width = PBL_DISPLAY_WIDTH - timeline_layer_get_ideal_sidebar_width() - 15;
  const int16_t left = s_action_bar_on_right ? 0 : (PBL_DISPLAY_WIDTH - text_width);
  const int16_t right = left + text_width;
  for (int16_t y = PBL_DISPLAY_HEIGHT / 4; y < 3 * PBL_DISPLAY_HEIGHT / 4; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(bitmap, y);
    for (int16_t x = PBL_MAX(row.min_x, left); x < PBL_MIN(row.max_x + 1, right); x++) {
      if (row.data[x] == GColorBlackARGB8) {
        top = PBL_MIN(top, y);
        bottom = PBL_MAX(bottom, y);
      }
    }
  }
  cl_assert(top <= bottom);
  cl_assert(PBL_ABS(top + bottom - PBL_DISPLAY_HEIGHT) <= 4);
  for (size_t i = 0; i < num_other_text; i++) {
    other_text[i]->color = colors[i];
  }
  prv_set_neighbours_hidden(timeline_layer, false);
}

//! Checks that no text of the compact list runs over the round sidebar's circle, at rest or
//! moving: the only black inside the circle is its icons'
static void prv_assert_text_clear_of_arc(TimelineLayer *timeline_layer) {
  if (!timeline_layer->compact) {
    return;
  }
  const GBitmap *bitmap = &fake_graphics_context_get_context()->dest_bitmap;
  const int16_t sidebar_width = timeline_layer_get_ideal_sidebar_width();
  const GPoint center = {
    .x = s_action_bar_on_right ? (PBL_DISPLAY_WIDTH - sidebar_width + BANNER_CIRCLE_RADIUS)
                               : (sidebar_width - 1 - BANNER_CIRCLE_RADIUS),
    .y = PBL_DISPLAY_HEIGHT / 2,
  };
  const TimelineList *list = &timeline_layer->list;
  for (int16_t y = 0; y < PBL_DISPLAY_HEIGHT; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(bitmap, y);
    for (int16_t x = row.min_x; x <= row.max_x; x++) {
      const int32_t dx = x - center.x;
      const int32_t dy = y - center.y;
      if (row.data[x] != GColorBlackARGB8 ||
          dx * dx + dy * dy > BANNER_CIRCLE_RADIUS * BANNER_CIRCLE_RADIUS) {
        continue;
      }
      bool in_icon = false;
      for (int i = 0; i < list->num_pins + list->num_leaving; i++) {
        const TimelineListPinView *pin =
            (i < list->num_pins) ? list->pins[i] : list->leaving[i - list->num_pins];
        in_icon |= grect_contains_point(&pin->layout->icon_layer.layer.frame, &GPoint(x, y));
      }
      cl_assert(in_icon);
    }
  }
}
#endif

static void prv_create_list_view_and_render(ListViewConfig *config) {
  pin_db_init();

  for (int i = 0; i < (int)PBL_ARRAY_SIZE(config->pins); i++) {
    if (config->pins[i]) {
      prv_add_timeline_item(config->pins[i], config->past, i);
    }
  }

  s_data.model = (TimelineModel){};
  s_data.model.direction = config->past ? TimelineIterDirectionPast : TimelineIterDirectionFuture;
  timeline_model_init(pbl_realtime(), &s_data.model);

  Window window;
  window_init(&window, "Timeline");

  TimelineLayer timeline_layer = {};
  const TimelineScrollDirection scroll_direction =
      config->past ? TimelineScrollDirectionUp : TimelineScrollDirectionDown;
  timeline_layer_init(&timeline_layer, &window.layer.frame, scroll_direction);
  const GColor color = config->past ? TIMELINE_PAST_COLOR : TIMELINE_FUTURE_COLOR;
  timeline_layer_set_sidebar_color(&timeline_layer, color);
  timeline_layer_set_sidebar_width(&timeline_layer, timeline_layer_get_ideal_sidebar_width());
  layer_add_child(&window.layer, &timeline_layer.layer);
  timeline_layer_reset(&timeline_layer);

#if PBL_ROUND
  GRect icon_frame;
  timeline_layer_get_icon_frame(&timeline_layer, TIMELINE_LAYER_FIRST_VISIBLE_LAYOUT, &icon_frame);
  cl_assert_equal_i(icon_frame.origin.y + icon_frame.size.h / 2, PBL_DISPLAY_HEIGHT / 2);
#endif

  Animation *running_scroll = nullptr;
  for (int i = 0; i < config->num_scrolls; i++) {
    int new_idx;
    bool has_new;
    cl_assert(timeline_model_iter_next(&new_idx, &has_new));
    if (has_new) {
      timeline_layer_set_next_item(&timeline_layer, new_idx);
    }
    timeline_layer_move_data(&timeline_layer, 1);
    Animation *animation = timeline_layer_create_up_down_animation(
        &timeline_layer, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS,
        timeline_animation_interpolate_moook_soft);
    if (i == config->num_scrolls - 1 && config->scroll_progress) {
      // Stop the last scroll where it is
      animation_schedule(animation);
      fake_animation_update(animation, config->scroll_progress);
      running_scroll = animation;
      break;
    }
    fake_animation_complete(animation);
  }

  for (int i = 0; i < config->num_page_scrolls; i++) {
    cl_assert(timeline_layer_scroll_selected(&timeline_layer, true /* down */));
    fake_animation_complete(timeline_layer_create_up_down_animation(
        &timeline_layer, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS,
        timeline_animation_interpolate_moook_soft));
  }

  if (config->then_scroll_progress) {
    TimelineLayout *scrolled = timeline_layer_get_current_layout(&timeline_layer);
    const int16_t page_scroll = scrolled->page_scroll;
    int new_idx;
    bool has_new;
    cl_assert(timeline_model_iter_next(&new_idx, &has_new));
    timeline_layer_move_data(&timeline_layer, 1);
    running_scroll = timeline_layer_create_up_down_animation(
        &timeline_layer, TIMELINE_UP_DOWN_ANIMATION_DURATION_MS,
        timeline_animation_interpolate_moook_soft);
    animation_schedule(running_scroll);
    fake_animation_update(running_scroll, config->then_scroll_progress);
    // The pin that was selected keeps its text where it was scrolled to while it folds away
    cl_assert_equal_i(scrolled->page_scroll, page_scroll);
  }

  if (config->day_separator) {
    // Simulate showing the day separator
    int new_idx;
    bool has_new;
    cl_assert(timeline_model_iter_next(&new_idx, &has_new));
    if (has_new) {
      timeline_layer_set_next_item(&timeline_layer, new_idx);
    }
    timeline_layer_move_data(&timeline_layer, 1);
    cl_assert(timeline_layer_should_animate_day_separator(&timeline_layer));
    fake_animation_complete(timeline_layer_create_day_sep_show(&timeline_layer));
    fake_animation_complete(timeline_layer.day_separator.kino_layer.player.animation);
    timeline_layer_set_layouts_hidden(&timeline_layer, true);
  }

  window_set_on_screen(&window, true, true);

#if PBL_ROUND
  // Scrolling the fixed slots brings the next pin's title near the middle, and pins only rest on
  // the middle once they stop
  if (!config->day_separator && !config->scroll_progress && !config->num_page_scrolls &&
      !config->then_scroll_progress && (timeline_layer.compact || config->num_scrolls == 0)) {
    prv_assert_title_centered(&window, &timeline_layer);
  }
#endif
  window_render(&window, fake_graphics_context_get_context());
#if PBL_ROUND
  if (!config->day_separator) {
    prv_assert_text_clear_of_arc(&timeline_layer);
  }
#endif

  if (s_relationship_only) {
    FrameBuffer *fb = fake_graphics_context_get_framebuffer();
    GContext *ctx = fake_graphics_context_get_context();
    framebuffer_clear(fb);
    graphics_context_init(ctx, fb, GContextInitializationMode_App);
    cl_assert_equal_i(timeline_layer.relbar_layer.curr_rel_bar.rel_bar_type,
                      RelationshipBarTypeOverlap);
    timeline_layer.relbar_layer.curr_rel_bar.anim_offset = 20;
    Layer *relbar = &timeline_layer.relbar_layer.layer;
    layer_set_hidden(relbar, false);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, &ctx->dest_bitmap.bounds);
    relbar->update_proc(relbar, ctx);
  }

  // Leaving the timeline stops what is running before the pins go
  animation_unschedule(running_scroll);
  timeline_layer_deinit(&timeline_layer);
  timeline_model_deinit();
  pin_db_flush();
  fake_settings_reset();
}

// Content size grids
//////////////////////

//! Renders once per content size and checks the screens side by side, from Small
static void prv_check_for_each_size(void (*render)(void), const char *pbi_file) {
  ScreenGrid grid;
  screen_grid_init(&grid, 1);
  for (PreferredContentSize size = grid.first_size; size <= grid.last_size; size++) {
    system_theme_set_content_size(size);
    s_data = (TimelineTestData){};
    render();
    screen_grid_add(&grid, fake_graphics_context_get_context(), size, 0);
  }

  screen_grid_check(&grid, pbi_file);
}

// Tests
//////////////////////

static void prv_create_and_render_title_and_subtitle(bool past, uint16_t first_duration_m) {
  prv_create_list_view_and_render(&(ListViewConfig){
    .pins =
        {&(TimelineItemConfig){
           .relative_timestamp = (11 * PBL_SEC_PER_HOUR) + (30 * PBL_SEC_PER_MIN),
           .duration = first_duration_m,
           .title = "Jon Byrd birthday party",
           .subtitle = "Kaboom, Redwood City",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         },
         &(TimelineItemConfig){
           .relative_timestamp = 12 * PBL_SEC_PER_HOUR,
           .duration = PBL_MIN_PER_HOUR,
           .title = "Design Review Meeting",
           .subtitle = "Batavia, Palo Alto",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         }},
    .past = past,
  });
}

void test_timeline_list_view__title_and_subtitle_overlap_future(void) {
  prv_create_and_render_title_and_subtitle(false /* past */, PBL_MIN_PER_HOUR);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__title_and_subtitle_back_to_back_future(void) {
  prv_create_and_render_title_and_subtitle(false /* past */, PBL_MIN_PER_HOUR / 2);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__title_and_subtitle_free_time_future(void) {
  prv_create_and_render_title_and_subtitle(false /* past */, 5 /* first_duration_m */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__title_and_subtitle_free_time_past(void) {
  prv_create_and_render_title_and_subtitle(true /* past */, 5 /* first_duration_m */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP(TEST_PBI_FILE);
}

void prv_create_and_render_pin_and_dot(bool past) {
  prv_create_list_view_and_render(&(ListViewConfig){
    .pins =
        {&(TimelineItemConfig){
           // In the future, 10:00 PM, the widest time in the time font
           .relative_timestamp =
               past ? (11 * PBL_SEC_PER_HOUR) + (30 * PBL_SEC_PER_MIN) : 22 * PBL_SEC_PER_HOUR,
           .duration = PBL_MIN_PER_HOUR,
           .title = "Jon Byrd birthday party",
           .subtitle = "Kaboom, Redwood City",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         },
         &(TimelineItemConfig){
           .relative_timestamp = PBL_SEC_PER_DAY + PBL_SEC_PER_HOUR,
           .duration = PBL_MIN_PER_HOUR,
           .title = "Design Review Meeting",
           .subtitle = "Batavia, Palo Alto",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         }},
    .past = past,
  });
}

static void prv_render_pin_and_dot_future(void) {
  prv_create_and_render_pin_and_dot(false /* past */);
}

void test_timeline_list_view__content_sizes_pin_and_dot_future(void) {
  prv_check_for_each_size(prv_render_pin_and_dot_future, TEST_PBI_FILE);
}

void test_timeline_list_view__pin_and_dot_past(void) {
  prv_create_and_render_pin_and_dot(true /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

static void prv_render_pin_and_dot_past(void) {
  prv_create_and_render_pin_and_dot(true /* past */);
}

void test_timeline_list_view__content_sizes_pin_and_dot_past(void) {
  prv_check_for_each_size(prv_render_pin_and_dot_past, TEST_PBI_FILE);
}

void test_timeline_list_view__content_sizes_no_subtitle(void) {
  ScreenGrid grid;
  screen_grid_init(&grid, 2);
  for (PreferredContentSize size = grid.first_size; size <= grid.last_size; size++) {
    system_theme_set_content_size(size);
    for (unsigned int past = 0; past < 2; past++) {
      prv_create_list_view_and_render(&(ListViewConfig){
        .pins = {&(TimelineItemConfig){
          .relative_timestamp = 12 * PBL_SEC_PER_HOUR,
          .duration = PBL_MIN_PER_HOUR,
          .title = "Design review meeting",
          .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
        }},
        .past = past,
      });
      screen_grid_add(&grid, fake_graphics_context_get_context(), size, past);
    }
  }
  screen_grid_check(&grid, TEST_PBI_FILE);
}

void prv_create_and_render_day_sep_tomorrow(bool past) {
  prv_create_list_view_and_render(&(ListViewConfig){
    .pins =
        {&(TimelineItemConfig){
           .relative_timestamp = (11 * PBL_SEC_PER_HOUR) + (30 * PBL_SEC_PER_MIN),
           .duration = PBL_MIN_PER_HOUR,
           .title = "Jon Byrd birthday party",
           .subtitle = "Kaboom, Redwood City",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         },
         &(TimelineItemConfig){
           .relative_timestamp =
               ((11 * PBL_SEC_PER_HOUR) + (30 * PBL_SEC_PER_MIN) + PBL_SEC_PER_DAY),
           .duration = PBL_MIN_PER_HOUR,
           .title = "Design Review Meeting",
           .subtitle = "Batavia, Palo Alto",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         }},
    .past = past,
    .day_separator = true,
  });
}

void test_timeline_list_view__day_sep_tomorrow_future(void) {
  prv_create_and_render_day_sep_tomorrow(false /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__day_sep_tomorrow_past(void) {
  prv_create_and_render_day_sep_tomorrow(true /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

static void prv_render_day_sep_tomorrow_future(void) {
  prv_create_and_render_day_sep_tomorrow(false /* past */);
}

void test_timeline_list_view__content_sizes_day_sep_tomorrow_future(void) {
  prv_check_for_each_size(prv_render_day_sep_tomorrow_future, TEST_PBI_FILE);
}

void prv_create_and_render_pin_and_fin(bool past) {
  prv_create_list_view_and_render(&(ListViewConfig){
    .pins = {&(TimelineItemConfig){
      .relative_timestamp = (11 * PBL_SEC_PER_HOUR) + (30 * PBL_SEC_PER_MIN),
      .title = "Jon Byrd birthday party",
      .duration = PBL_MIN_PER_HOUR,
      .subtitle = "Kaboom, Redwood City",
      .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
    }},
    .past = past,
  });
}

void test_timeline_list_view__pin_and_fin_future(void) {
  prv_create_and_render_pin_and_fin(false /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__pin_and_fin_past(void) {
  prv_create_and_render_pin_and_fin(true /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void prv_create_and_render_all_day(bool past) {
  prv_create_list_view_and_render(&(ListViewConfig){
    .pins = {&(TimelineItemConfig){
      // Must sit further back than the duration, or the past view correctly excludes it
      // for still being in progress.
      .relative_timestamp = 2 * PBL_SEC_PER_DAY,
      .title = "Independence Day",
      .duration = PBL_MIN_PER_DAY,
      .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
      .all_day = true,
    }},
    .past = past,
  });
}

void test_timeline_list_view__all_day_future(void) {
  prv_create_and_render_all_day(false /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__all_day_past(void) {
  prv_create_and_render_all_day(true /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

static void prv_create_and_render_short_titles(bool past) {
  prv_create_list_view_and_render(&(ListViewConfig){
    .pins =
        {&(TimelineItemConfig){
           .relative_timestamp = PBL_SEC_PER_HOUR,
           .title = "Sunrise",
           .subtitle = "92°/70°",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         },
         &(TimelineItemConfig){
           .relative_timestamp = 12 * PBL_SEC_PER_HOUR,
           .title = "Sunset",
           .subtitle = "94°/75°",
           .icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR,
         }},
    .past = past,
  });
}

void test_timeline_list_view__content_sizes_time_title_spacing(void) {
  ScreenGrid grid;
  screen_grid_init(&grid, 2);
  for (PreferredContentSize size = grid.first_size; size <= grid.last_size; size++) {
    system_theme_set_content_size(size);
    prv_create_and_render_short_titles(false /* past */);
    screen_grid_add(&grid, fake_graphics_context_get_context(), size, 0);
    prv_create_and_render_short_titles(true /* past */);
    screen_grid_add(&grid, fake_graphics_context_get_context(), size, 1);
  }
  screen_grid_check(&grid, TEST_PBI_FILE);
}

void test_timeline_list_view__content_sizes_all_day(void) {
  ScreenGrid grid;
  screen_grid_init(&grid, 2);
  for (PreferredContentSize size = grid.first_size; size <= grid.last_size; size++) {
    system_theme_set_content_size(size);
    prv_create_and_render_all_day(false /* past */);
    screen_grid_add(&grid, fake_graphics_context_get_context(), size, 0);
    prv_create_and_render_all_day(true /* past */);
    screen_grid_add(&grid, fake_graphics_context_get_context(), size, 1);
  }
  screen_grid_check(&grid, TEST_PBI_FILE);
}

#if PBL_RECT
static uint8_t prv_pixel(const GBitmap *bitmap, int x, int y) {
  GBitmapDataRowInfo row = gbitmap_get_data_row_info(bitmap, y);
  if (x < row.min_x || x > row.max_x) {
    return 0;
  }
  return prv_raw_image_get_value_for_format(row.data, x, 0, bitmap->row_size_bytes,
                                            gbitmap_get_bits_per_pixel(gbitmap_get_format(bitmap)),
                                            gbitmap_get_format(bitmap));
}

#endif

void test_timeline_list_view__left_hand_overlap_bars_mirror_right_hand(void) {
#if PBL_RECT
  s_relationship_only = true;
  prv_create_and_render_title_and_subtitle(false, PBL_MIN_PER_HOUR);
  GBitmap *bitmap = &fake_graphics_context_get_context()->dest_bitmap;
  uint8_t *right = malloc(PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT);
  cl_assert(right != nullptr);
  for (int y = 0; y < PBL_DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < PBL_DISPLAY_WIDTH; x++) {
      right[y * PBL_DISPLAY_WIDTH + x] = prv_pixel(bitmap, x, y);
    }
  }
  s_action_bar_on_right = false;
  prv_create_and_render_title_and_subtitle(false, PBL_MIN_PER_HOUR);
  int changed_pixels = 0;
  for (int y = 0; y < PBL_DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < PBL_DISPLAY_WIDTH; x++) {
      const uint8_t expected = right[y * PBL_DISPLAY_WIDTH + PBL_DISPLAY_WIDTH - 1 - x];
      cl_assert_equal_i(prv_pixel(bitmap, x, y), expected);
      changed_pixels += expected != right[0];
    }
  }
  cl_assert(changed_pixels > 0);
  free(right);
#endif
}

void test_timeline_list_view__left_hand_title_and_subtitle_overlap_future(void) {
  s_action_bar_on_right = false;
  prv_create_and_render_title_and_subtitle(false /* past */, PBL_MIN_PER_HOUR);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__left_hand_title_and_subtitle_free_time_past(void) {
  s_action_bar_on_right = false;
  prv_create_and_render_title_and_subtitle(true /* past */, 5 /* first_duration_m */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__left_hand_pin_and_dot_future(void) {
  s_action_bar_on_right = false;
  prv_create_and_render_pin_and_dot(false /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__left_hand_day_sep_tomorrow_future(void) {
  s_action_bar_on_right = false;
  prv_create_and_render_day_sep_tomorrow(false /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__left_hand_pin_and_fin_past(void) {
  s_action_bar_on_right = false;
  prv_create_and_render_pin_and_fin(true /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__left_hand_all_day_future(void) {
  s_action_bar_on_right = false;
  prv_create_and_render_all_day(false /* past */);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

// Compact list
//////////////////////

static TimelineItemConfig s_one_liners[] = {
  {.relative_timestamp = 1 * PBL_SEC_PER_HOUR, .duration = 30, .title = "Standup"},
  {.relative_timestamp = 2 * PBL_SEC_PER_HOUR, .duration = 30, .title = "Lunch"},
  {.relative_timestamp = 3 * PBL_SEC_PER_HOUR, .duration = 30, .title = "Code review"},
  {.relative_timestamp = 4 * PBL_SEC_PER_HOUR, .duration = 30, .title = "Gym"},
  {.relative_timestamp = 5 * PBL_SEC_PER_HOUR, .duration = 30, .title = "Call Mom"},
  {.relative_timestamp = 6 * PBL_SEC_PER_HOUR, .duration = 30, .title = "Dinner"},
};

static const char *const s_long_body =
    "Bring both passports and the printed hotel bookings. Ask about the train from the airport "
    "to the city centre, the museum pass for three days, and whether the hotel can store the "
    "luggage after checkout. Confirm the dinner reservation for Saturday and the time of the "
    "boat tour.";

static void prv_render_compact_config(TimelineItemConfig *pins, ListViewConfig *config) {
  for (int i = 0; i < (int)PBL_ARRAY_SIZE(config->pins); i++) {
    pins[i].icon = TIMELINE_RESOURCE_TIMELINE_CALENDAR;
    config->pins[i] = &pins[i];
  }
  prv_create_list_view_and_render(config);
}

static void prv_render_compact(TimelineItemConfig *pins, bool past, int num_scrolls,
                               AnimationProgress scroll_progress) {
  ListViewConfig config = {
    .past = past,
    .num_scrolls = num_scrolls,
    .scroll_progress = scroll_progress,
  };
  prv_render_compact_config(pins, &config);
}

static void prv_check_compact_pages(TimelineItemConfig *pins, int num_scrolls, int num_page_scrolls,
                                    const char *pbi_file) {
  ScreenGrid grid;
  screen_grid_init(&grid, 2);
  for (PreferredContentSize size = grid.first_size; size <= grid.last_size; size++) {
    system_theme_set_content_size(size);
    for (int past = 0; past < 2; past++) {
      ListViewConfig config = {
        .past = past,
        .num_scrolls = num_scrolls,
        // The larger text sizes keep the fixed slots, which do not scroll through a pin
        .num_page_scrolls = (size <= PreferredContentSizeDefault) ? num_page_scrolls : 0,
      };
      prv_render_compact_config(pins, &config);
      screen_grid_add(&grid, fake_graphics_context_get_context(), size, past);
    }
  }
  screen_grid_check(&grid, pbi_file);
}

static void prv_check_compact(TimelineItemConfig *pins, int num_scrolls, const char *pbi_file) {
  prv_check_compact_pages(pins, num_scrolls, 0 /* num_page_scrolls */, pbi_file);
}

void test_timeline_list_view__content_sizes_compact_one_liners(void) {
  // Up to the default text size, the selected pin shows between the pins before and after it
  prv_check_compact(s_one_liners, 2 /* num_scrolls */, TEST_PBI_FILE);
}

void test_timeline_list_view__content_sizes_compact_tall_selected(void) {
  TimelineItemConfig pins[PBL_ARRAY_SIZE(s_one_liners)];
  memcpy(pins, s_one_liners, sizeof(pins));
  pins[2].title = "Quarterly planning review with everybody";
  pins[2].subtitle = "Building 4, Conference room Batavia";
  // The selected pin shows all its text and pushes its neighbours away
  prv_check_compact(pins, 2 /* num_scrolls */, TEST_PBI_FILE);
}

void test_timeline_list_view__content_sizes_compact_description(void) {
  TimelineItemConfig pins[PBL_ARRAY_SIZE(s_one_liners)];
  memcpy(pins, s_one_liners, sizeof(pins));
  pins[2].title = "Trip planning";
  pins[2].subtitle = "Agency office";
  pins[2].body = s_long_body;
  // The selected pin shows the first page of its description above the next pin
  prv_check_compact(pins, 2 /* num_scrolls */, TEST_PBI_FILE);
}

void test_timeline_list_view__content_sizes_compact_description_next_page(void) {
  TimelineItemConfig pins[PBL_ARRAY_SIZE(s_one_liners)];
  memcpy(pins, s_one_liners, sizeof(pins));
  pins[2].title = "Trip planning";
  pins[2].subtitle = "Agency office";
  pins[2].body = s_long_body;
  // Scrolling shows the next page of the description in its place, the other pins stay
  prv_check_compact_pages(pins, 2 /* num_scrolls */, 1 /* num_page_scrolls */, TEST_PBI_FILE);
}

void test_timeline_list_view__compact_fold_scrolled(void) {
  system_theme_set_content_size(PreferredContentSizeDefault);
  TimelineItemConfig pins[PBL_ARRAY_SIZE(s_one_liners)];
  memcpy(pins, s_one_liners, sizeof(pins));
  pins[2].title = "Trip planning";
  pins[2].subtitle = "Agency office";
  pins[2].body = s_long_body;
  // Moving on from the second page of the description, the pin keeps showing it as it folds
  ListViewConfig config = {
    .num_scrolls = 2,
    .num_page_scrolls = 1,
    .then_scroll_progress = ANIMATION_NORMALIZED_MAX / 2,
  };
  prv_render_compact_config(pins, &config);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}

void test_timeline_list_view__content_sizes_compact_first(void) {
  // Nothing comes before the first pin, so the list starts with the selected pin
  prv_check_compact(s_one_liners, 0 /* num_scrolls */, TEST_PBI_FILE);
}

void test_timeline_list_view__compact_mid_scroll(void) {
  system_theme_set_content_size(PreferredContentSizeDefault);
  TimelineItemConfig pins[PBL_ARRAY_SIZE(s_one_liners)];
  memcpy(pins, s_one_liners, sizeof(pins));
  pins[1].title = "Physics 7A-201 lecture and lab";
  pins[1].subtitle = "Room 2B";
  pins[2].title = "Dentist";
  pins[2].subtitle = "Dr. Vidal, Carrer de Mallorca 123, Barcelona";
  // Halfway from the lecture to the dentist, the lecture folds over its room and the dentist
  // grows over its address, both laid out as they rest, while the pins stay stacked
  prv_render_compact(pins, false /* past */, 2 /* num_scrolls */, ANIMATION_NORMALIZED_MAX / 2);
  FAKE_GRAPHICS_CONTEXT_CHECK_DEST_BITMAP_FILE();
}
