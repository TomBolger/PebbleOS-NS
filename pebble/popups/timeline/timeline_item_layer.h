/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/services/timeline/item.h>
#include <pbl/services/timeline/swap_layer.h>
#include <pbl/services/timeline/timeline_layout.h>

#include <applib/graphics/text.h>
#include <applib/ui/layer.h>
#include <kernel/events.h>

//! The timeline item layer shows the card of a timeline item. It scrolls the card as a
//! notification's, in a swap layer that holds this one card and never swaps.

typedef struct {
  Layer layer;
  //! Scrolls the card
  SwapLayer swap_layer;
  TimelineItem *item;
  TimelineLayout *timeline_layout;
} TimelineItemLayer;

//! The layer update proc for the TimelineItemLayer
void timeline_item_layer_update_proc(Layer *layer, GContext *ctx);

//! Initialize a timeline item layer
//! @param item_layer a pointer to the TimelineItemLayer to initialize
//! @param frame the frame with which to initialize the layer
void timeline_item_layer_init(TimelineItemLayer *item_layer, const GRect *frame);

//! Deinitialize a timeline item layer. Currently a no-op
void timeline_item_layer_deinit(TimelineItemLayer *item_layer);

//! Set the timeline item displayed by the TimelineItemLayer
//! @param item_layer a pointer to the TimelineItemLayer
//! @param item a pointer to the item to use
//! @param info a pointer to the TimelineLayoutInfo to use for the item
void timeline_item_layer_set_item(TimelineItemLayer *item_layer, TimelineItem *item,
                                  TimelineLayoutInfo *info);

//! Convenience function to set the \ref ClickConfigProvider callback on the
//! given window to the item layer's click config provider: the swap layer's UP & DOWN
//! scrolling, SELECT for the item's actions and, in the timeline app, BACK to its pin
void timeline_item_layer_set_click_config_onto_window(TimelineItemLayer *item_layer,
                                                      struct Window *window);
