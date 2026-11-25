/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <applib/graphics/graphics.h>
#include <applib/ui/click.h>
#include <applib/ui/list_wrap.h>
#include <applib/ui/menu_layer.h>
#include <applib/ui/menu_layer_private.h>
#include <applib/ui/window.h>
#include <clar.h>
#include <stubs_compiled_with_legacy2_sdk.h>

// Stubs for what these tests never draw or click
/////////////////////

GDrawState graphics_context_get_drawing_state(GContext *ctx) {
  return ctx->draw_state;
}
void graphics_context_set_drawing_state(GContext *ctx, GDrawState draw_state) {
  ctx->draw_state = draw_state;
}
void graphics_fill_radial_capped_internal(GContext *ctx, GPoint center, uint16_t radius_inner,
                                          uint16_t radius_outer, int32_t angle_start,
                                          int32_t angle_end) {
}
int16_t menu_cell_basic_cell_height(void) {
  return 44;
}
void menu_text_scroll_layer_deinit(const Layer *content_layer) {
}
GBitmap *shadow_get_top(void) {
  return nullptr;
}
GBitmap *shadow_get_bottom(void) {
  return nullptr;
}
void graphics_draw_bitmap_in_rect(GContext *ctx, const GBitmap *bitmap, const GRect *rect) {
}
void window_set_click_context(ButtonId button_id, void *context) {
}
void window_set_click_config_provider_with_context(Window *window, ClickConfigProvider provider,
                                                   void *context) {
}
void window_long_click_subscribe(ButtonId button_id, uint16_t delay_ms, ClickHandler down_handler,
                                 ClickHandler up_handler) {
}
void window_single_click_subscribe(ButtonId button_id, ClickHandler handler) {
}
void window_single_repeating_click_subscribe(ButtonId button_id, uint16_t repeat_interval_ms,
                                             ClickHandler handler) {
}

// Not declared in menu_layer.h; normally reached via the window click config
extern void menu_up_click_handler(ClickRecognizerRef recognizer, void *context);
extern void menu_down_click_handler(ClickRecognizerRef recognizer, void *context);

// The private part of the wrap state the tests look at, see menu_layer.c
typedef struct MenuLayerWrap {
  ListWrap list_wrap;
} MenuLayerWrap;

#define NUM_ROWS 10

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return NUM_ROWS;
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                         void *context) {
}

static MenuLayer s_menu_layer;

void test_menu_layer_wrap__initialize(void) {
  menu_layer_init(&s_menu_layer, &GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
  menu_layer_set_callbacks(&s_menu_layer, nullptr,
                           &(MenuLayerCallbacks){
                             .draw_row = prv_draw_row,
                             .get_num_rows = prv_get_num_rows,
                           });
  menu_layer_set_scroll_wrap_around(&s_menu_layer, true);
}

void test_menu_layer_wrap__cleanup(void) {
  menu_layer_deinit(&s_menu_layer);
}

static int16_t prv_offset(void) {
  return scroll_layer_get_content_offset(&s_menu_layer.scroll_layer).y;
}

//! Where the highlight is on screen
static int16_t prv_highlight_y(void) {
  return s_menu_layer.inverter.layer.frame.origin.y + prv_offset();
}

void test_menu_layer_wrap__down_past_the_last_row_carries_on_round(void) {
  menu_layer_set_selected_index(&s_menu_layer, MenuIndex(0, NUM_ROWS - 1), MenuRowAlignCenter,
                                false);
  const int16_t from_offset = prv_offset();
  const int16_t from_highlight_y = prv_highlight_y();

  menu_down_click_handler(nullptr, &s_menu_layer);

  // The first row is selected and the list rests where it does with it, right away
  cl_assert_equal_i(menu_layer_get_selected_index(&s_menu_layer).row, 0);
  cl_assert(prv_offset() > from_offset);
  // The wrap draws it from where it was, moving it up round the loop
  cl_assert(s_menu_layer.wrap != nullptr);
  const ListWrapLaps *laps = &s_menu_layer.wrap->list_wrap.laps;
  cl_assert(laps->from_shift > 0);
  cl_assert_equal_i(prv_offset() + laps->from_shift + laps->loop, from_offset);
  // The highlight starts on the row left, as it was on screen
  cl_assert_equal_i(prv_highlight_y(), from_highlight_y);

  // Any other move lands it first
  menu_layer_set_selected_next(&s_menu_layer, false, MenuRowAlignCenter, false);
  cl_assert(s_menu_layer.wrap == nullptr);
  cl_assert_equal_i(menu_layer_get_selected_index(&s_menu_layer).row, 1);
}

void test_menu_layer_wrap__up_past_the_first_row_is_the_mirror(void) {
  const int16_t from_offset = prv_offset();
  const int16_t from_highlight_y = prv_highlight_y();

  menu_up_click_handler(nullptr, &s_menu_layer);

  cl_assert_equal_i(menu_layer_get_selected_index(&s_menu_layer).row, NUM_ROWS - 1);
  cl_assert(prv_offset() < from_offset);
  cl_assert(s_menu_layer.wrap != nullptr);
  const ListWrapLaps *laps = &s_menu_layer.wrap->list_wrap.laps;
  cl_assert(laps->from_shift < 0);
  cl_assert_equal_i(prv_offset() + laps->from_shift + laps->loop, from_offset);
  cl_assert_equal_i(prv_highlight_y(), from_highlight_y);

  // Deinit lands it too
  menu_layer_deinit(&s_menu_layer);
  cl_assert(s_menu_layer.wrap == nullptr);
  test_menu_layer_wrap__initialize();
}

void test_menu_layer_wrap__ends_stop_without_wrap_around(void) {
  menu_layer_set_scroll_wrap_around(&s_menu_layer, false);
  menu_up_click_handler(nullptr, &s_menu_layer);
  cl_assert_equal_i(menu_layer_get_selected_index(&s_menu_layer).row, 0);
  cl_assert(s_menu_layer.wrap == nullptr);
}
