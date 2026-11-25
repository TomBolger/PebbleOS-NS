/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <string.h>

#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/graphics_private.h>
#include <applib/ui/action_menu_hierarchy.h>
#include <applib/ui/action_menu_window.h>
#include <applib/ui/action_menu_window_private.h>
#include <applib/ui/animation_interpolate.h>
#include <applib/ui/click.h>
#include <applib/ui/crumbs_layer.h>
#include <applib/ui/menu_layer.h>
#include <applib/ui/menu_layer_private.h>
#include <applib/ui/menu_text_scroll.h>
#include <applib/ui/pill_action_menu.h>
#include <applib/ui/shadows.h>
#include <applib/ui/window_private.h>
#include <clar.h>
#include <stubs_compiled_with_legacy2_sdk.h>

// Stubs for what these tests never reach: the ActionMenu window, drawing and animation curves
/////////////////////

void action_menu_layer_click_config_provider(ActionMenuLayer *aml) {
}
void action_menu_layer_deinit(ActionMenuLayer *aml) {
}
void action_menu_layer_init(ActionMenuLayer *aml, const GRect *frame) {
}
void action_menu_layer_notify_selection_changed(ActionMenuLayer *aml) {
}
void action_menu_layer_set_align(ActionMenuLayer *aml, ActionMenuAlign align) {
}
void action_menu_layer_set_callbacks(ActionMenuLayer *aml, ActionMenuLayerCallbacks callbacks,
                                     void *context) {
}
void action_menu_layer_set_content_size(ActionMenuLayer *aml, PreferredContentSize size) {
}
void action_menu_layer_set_glyph_grid(ActionMenuLayer *aml, bool glyph_grid) {
}
void action_menu_layer_set_items(ActionMenuLayer *aml, const ActionMenuItem *items, int num_items,
                                 unsigned default_selected_item, unsigned separator_index) {
}
void action_menu_layer_set_short_items(ActionMenuLayer *aml, const ActionMenuItem *items,
                                       int num_items, unsigned default_selected_item) {
}
void crumbs_layer_deinit(CrumbsLayer *crumbs_layer) {
}
Animation *crumbs_layer_get_animation(CrumbsLayer *crumbs_layer) {
  return nullptr;
}
void crumbs_layer_init(CrumbsLayer *crumbs_layer, const GRect *frame, GColor fg_color,
                       GColor bg_color) {
}
void crumbs_layer_set_level(CrumbsLayer *crumbs_layer, int level) {
}
int crumbs_layer_width(void) {
  return 0;
}
GDrawCommandImage *gdraw_command_image_clone(GDrawCommandImage *image) {
  return nullptr;
}
GDrawCommandImage *gdraw_command_image_create_with_resource(uint32_t resource_id) {
  return nullptr;
}
void gdraw_command_image_destroy(GDrawCommandImage *image) {
}
void gdraw_command_image_draw(GContext *ctx, GDrawCommandImage *image, GPoint offset) {
}
GSize gdraw_command_image_get_bounds_size(GDrawCommandImage *image) {
  return GSizeZero;
}
size_t gdraw_command_image_get_data_size(GDrawCommandImage *image) {
  return 0;
}
GDrawState graphics_context_get_drawing_state(GContext *ctx) {
  return ctx->draw_state;
}
void graphics_context_set_drawing_state(GContext *ctx, GDrawState draw_state) {
  ctx->draw_state = draw_state;
}
void graphics_draw_bitmap_in_rect(GContext *ctx, const GBitmap *bitmap, const GRect *rect) {
}
void graphics_fill_radial_internal(GContext *ctx, GPoint center, uint16_t radius_inner,
                                   uint16_t radius_outer, int32_t angle_start, int32_t angle_end) {
}
void graphics_private_draw_horizontal_line_prepared(GContext *ctx, GBitmap *framebuffer,
                                                    GRect *clip_box, int16_t y, Fixed_S16_3 x1,
                                                    Fixed_S16_3 x2, GColor color) {
}
int16_t interpolate_int16(int32_t normalized, int16_t from, int16_t to) {
  return to;
}
int64_t interpolate_moook(int32_t normalized, int64_t from, int64_t to) {
  return to;
}
uint32_t interpolate_moook_duration() {
  return 0;
}
int16_t menu_cell_basic_cell_height(void) {
  return 44;
}
void menu_text_scroll_layer_deinit(const Layer *content_layer) {
}
GBitmap *shadow_get_bottom(void) {
  return nullptr;
}
GBitmap *shadow_get_top(void) {
  return nullptr;
}
void window_init(Window *window, const char *debug_name) {
}
bool window_is_loaded(Window *window) {
  return false;
}
void window_long_click_subscribe(ButtonId button_id, uint16_t delay_ms, ClickHandler down_handler,
                                 ClickHandler up_handler) {
}
void window_set_background_color(Window *window, GColor background_color) {
}
void window_set_click_context(ButtonId button_id, void *context) {
}
void window_set_fullscreen(Window *window, bool enabled) {
}
void window_set_window_handlers(Window *window, const WindowHandlers *handlers) {
}

// Animations never run: the action menu opens and closes at once
bool animation_schedule(Animation *animation) {
  return false;
}

// The window the menu is on, and the clicks its click config provider subscribes
/////////////////////

static Window s_window;
static MenuLayer s_menu_layer;

typedef struct {
  ClickHandler handler;
  uint16_t repeat_interval_ms;
} Subscription;

static Subscription s_clicks[NUM_BUTTONS];

static void prv_load_click_config(void) {
  memset(s_clicks, 0, sizeof(s_clicks));
  if (s_window.click_config_provider) {
    s_window.click_config_provider(s_window.click_config_context);
  }
}

static void prv_click(ButtonId button) {
  prv_load_click_config();
  cl_assert(s_clicks[button].handler);
  s_clicks[button].handler(nullptr, s_window.click_config_context);
}

void window_set_click_config_provider_with_context(Window *window, ClickConfigProvider provider,
                                                   void *context) {
  window->click_config_provider = provider;
  window->click_config_context = context;
}

void window_set_click_config_provider(Window *window, ClickConfigProvider provider) {
  window_set_click_config_provider_with_context(window, provider, nullptr);
}

void window_single_click_subscribe(ButtonId button_id, ClickHandler handler) {
  s_clicks[button_id] = (Subscription){.handler = handler};
  if (button_id == BUTTON_ID_BACK) {
    s_window.overrides_back_button = true;
  }
}

void window_single_repeating_click_subscribe(ButtonId button_id, uint16_t repeat_interval_ms,
                                             ClickHandler handler) {
  s_clicks[button_id] =
      (Subscription){.handler = handler, .repeat_interval_ms = repeat_interval_ms};
}

void window_set_overrides_back_button(Window *window, bool overrides_back_button) {
  window->overrides_back_button = overrides_back_button;
}

struct Layer *window_get_root_layer(const Window *window) {
  return (struct Layer *)&window->layer;
}

void window_set_user_data(Window *window, void *data) {
  window->user_data = data;
}

void *window_get_user_data(const Window *window) {
  return window->user_data;
}

// What the action menu tells its client
/////////////////////

#define MAX_EVENTS 16

static const ActionMenuItem *s_selections[MAX_EVENTS];
static int s_num_selections;
static int s_num_did_close;

static void prv_selection_changed(const ActionMenuItem *item, void *context) {
  cl_assert(s_num_selections < MAX_EVENTS);
  s_selections[s_num_selections++] = item;
}

static void prv_did_close(ActionMenu *action_menu, const ActionMenuItem *item, void *context) {
  s_num_did_close++;
}

static void prv_perform(ActionMenu *action_menu, const ActionMenuItem *item, void *context) {
}

// A root of three actions and a submenu of two whose highlight is followed, as the alarm's sound
// submenu plays the highlighted tone
static ActionMenuLevel *s_root;
static ActionMenuLevel *s_sub;

static ActionMenuLevel *prv_create_level(uint16_t num_items) {
  ActionMenuLevel *level = calloc(1, sizeof(ActionMenuLevel) + num_items * sizeof(ActionMenuItem));
  level->num_items = num_items;
  return level;
}

static ActionMenuConfig s_config;

// MenuLayer callbacks: one row, selected
/////////////////////

static uint16_t prv_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return 1;
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                         void *context) {
}

// Setup and Teardown
/////////////////////

void test_pill_action_menu__initialize(void) {
  s_window = (Window){};
  layer_init(&s_window.layer, &GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
  menu_layer_init(&s_menu_layer, &GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
  menu_layer_set_callbacks(&s_menu_layer, nullptr,
                           &(MenuLayerCallbacks){
                             .get_num_rows = prv_num_rows,
                             .draw_row = prv_draw_row,
                           });
  menu_layer_set_selected_index(&s_menu_layer, MenuIndex(0, 0), MenuRowAlignNone, false);
  // As in system UI
  s_menu_layer.round_pill_highlight = true;
  layer_add_child(&s_window.layer, menu_layer_get_layer(&s_menu_layer));
  menu_layer_set_click_config_onto_window(&s_menu_layer, &s_window);

  s_num_selections = 0;
  s_num_did_close = 0;
  s_root = prv_create_level(4);
  s_sub = prv_create_level(2);
  for (int i = 0; i < 3; i++) {
    s_root->items[i] = (ActionMenuItem){.label = "Action", .perform_action = prv_perform};
  }
  s_root->items[3] = (ActionMenuItem){.label = "Sound", .next_level = s_sub};
  s_sub->parent_level = s_root;
  s_sub->selection_changed = prv_selection_changed;
  for (int i = 0; i < 2; i++) {
    s_sub->items[i] = (ActionMenuItem){.label = "Tone", .perform_action = prv_perform};
  }
  s_config = (ActionMenuConfig){
    .root_level = s_root,
    .did_close = prv_did_close,
  };
}

void test_pill_action_menu__cleanup(void) {
  menu_layer_deinit(&s_menu_layer);
  free(s_root);
  free(s_sub);
}

static ActionMenu *prv_open(void) {
  const PillActionMenuConfig config = {
    .menu_layer = &s_menu_layer,
    .window = &s_window,
  };
  return pill_action_menu_open(&config, &s_config);
}

// Tests
/////////////////////

void test_pill_action_menu__hides_menu_pill_without_touching_colors(void) {
  menu_layer_set_highlight_colors(&s_menu_layer, GColorJaegerGreen, GColorWhite);
  cl_assert(prv_open());
  cl_assert(s_menu_layer.round_pill_hidden);
  // The highlight keeps its colours, which a theme refresh could otherwise overwrite
  cl_assert(
      gcolor_equal(s_menu_layer.highlight_colors[MenuLayerColorBackground], GColorJaegerGreen));
  cl_assert(!s_menu_layer.highlight_colors_follow_theme);

  prv_click(BUTTON_ID_BACK);
  cl_assert_equal_i(s_num_did_close, 1);
  cl_assert(!s_menu_layer.round_pill_hidden);
  MenuRoundShape pill;
  cl_assert(menu_layer_get_round_highlight(&s_menu_layer, &pill));
}

void test_pill_action_menu__back_restores_the_window_clicks(void) {
  const ClickConfigProvider provider = s_window.click_config_provider;
  const bool overrides_back = s_window.overrides_back_button;
  cl_assert(prv_open());
  cl_assert(s_window.click_config_provider != provider);
  prv_click(BUTTON_ID_BACK);
  cl_assert(s_window.click_config_provider == provider);
  cl_assert(s_window.click_config_context == &s_menu_layer);
  cl_assert_equal_b(s_window.overrides_back_button, overrides_back);
}
