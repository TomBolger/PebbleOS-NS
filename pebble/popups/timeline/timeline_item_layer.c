/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "timeline_item_layer.h"

#include <stdint.h>
#include <time.h>

#include <pbl/kernel/assert.h>
#include <pbl/services/timeline/layout_layer.h>
#include <pbl/services/timeline/timeline_actions.h>
#include <pbl/util/math.h>
#include <pbl/util/testing.h>

#include <applib/graphics/graphics.h>
#include <applib/ui/action_menu_window.h>
#include <applib/ui/window.h>
#include <applib/ui/window_manager.h>
#include <apps/system/timeline/timeline.h>
#include <kernel/pebble_tasks.h>
#include <kernel/ui/kernel_ui.h>
#include <kernel/ui/modals/modal_manager.h>
#include <process_state/app_state/app_state.h>

static void prv_handle_select_click(ClickRecognizerRef recognizer, void *context) {
  TimelineItemLayer *item_layer = context;
  TimelineItem *item = item_layer->item;
  if (!item || timeline_actions_perform_sole_action(item)) {
    return;
  }
  ActionMenuLevel *root_level =
      timeline_actions_create_item_root_level(item, TimelineItemActionSourceTimeline);
  if (!root_level) {
    return;
  }
  const LayoutColors *colors = layout_get_colors((LayoutLayer *)item_layer->timeline_layout);
  ActionMenuConfig config = {
    .root_level = root_level,
    .context = item,
    .colors.background = colors->bg_color,
    .colors.foreground = colors->primary_color,
  };
  WindowStack *window_stack = window_manager_get_window_stack(ModalPriorityNotification);
  timeline_actions_push_action_menu(&config, window_stack);
}

static void prv_handle_back_click(ClickRecognizerRef recognizer, void *context) {
  timeline_animate_back_from_card();
}

//! Select and back, on top of the swap layer's scrolling
static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_handle_select_click);
  window_set_click_context(BUTTON_ID_SELECT, context);

  if (pebble_task_get_current() == PebbleTask_App) {
    // only override the back button when we're in the app
    window_single_click_subscribe(BUTTON_ID_BACK, prv_handle_back_click);
    window_set_click_context(BUTTON_ID_BACK, context);
  }
}

void timeline_item_layer_set_click_config_onto_window(TimelineItemLayer *item_layer,
                                                      struct Window *window) {
  swap_layer_set_click_config_onto_window(&item_layer->swap_layer, window);
}

/////////////////////////////////////////
// Public functions
/////////////////////////////////////////

void timeline_item_layer_update_proc(Layer *layer, GContext *ctx) {
  //! Fill background to hide layers below
  TimelineItemLayer *item_layer = (TimelineItemLayer *)layer;
  if (!item_layer->timeline_layout) {
    return;
  }
  const LayoutColors *colors = layout_get_colors((LayoutLayer *)item_layer->timeline_layout);
  graphics_context_set_fill_color(ctx, colors->bg_color);
  graphics_fill_rect(ctx, &layer->bounds);
}

static LayoutLayer *prv_get_layout(SwapLayer *swap_layer, int8_t rel_position, void *context) {
  TimelineItemLayer *item_layer = context;
  // The card is the only one: there is nothing to swap to
  return (rel_position == 0) ? (LayoutLayer *)item_layer->timeline_layout : nullptr;
}

static void prv_layout_removed(SwapLayer *swap_layer, LayoutLayer *layout, void *context) {
  TimelineItemLayer *item_layer = context;
  if (layout == (LayoutLayer *)item_layer->timeline_layout) {
    item_layer->timeline_layout = nullptr;
  }
  layout_destroy(layout);
}

void timeline_item_layer_init(TimelineItemLayer *item_layer, const GRect *frame) {
  *item_layer = (TimelineItemLayer){};
  layer_init(&item_layer->layer, frame);
  layer_set_update_proc(&item_layer->layer, timeline_item_layer_update_proc);
  layer_set_clips(&item_layer->layer, false);

  swap_layer_init(&item_layer->swap_layer, &(GRect){GPointZero, frame->size});
  layer_add_child(&item_layer->layer, swap_layer_get_layer(&item_layer->swap_layer));
  swap_layer_set_callbacks(&item_layer->swap_layer, item_layer,
                           (SwapLayerCallbacks){
                             .get_layout_handler = prv_get_layout,
                             .layout_removed_handler = prv_layout_removed,
                             .click_config_provider = prv_click_config_provider,
                           });
}

void timeline_item_layer_deinit(TimelineItemLayer *item_layer) {
  // Destroys the layout through the layout removed handler
  swap_layer_deinit(&item_layer->swap_layer);
  layer_deinit(&item_layer->layer);
}

void timeline_item_layer_set_item(TimelineItemLayer *item_layer, TimelineItem *item,
                                  TimelineLayoutInfo *info) {
  item_layer->item = item;
  const LayoutLayerConfig config = {
    .frame = &(GRect){GPointZero, item_layer->layer.frame.size},
    .attributes = &item_layer->item->attr_list,
    .mode = LayoutLayerModeCard,
    .app_id = &item->header.parent_id,
    .context = info,
  };
  // The swap layer lets go of the previous card, destroying it, and takes this one
  item_layer->timeline_layout =
      (TimelineLayout *)layout_create(item_layer->item->header.layout, &config);
  swap_layer_reload_data(&item_layer->swap_layer);
}
