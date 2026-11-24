/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! Vertical placement of the pins of the compact timeline list, where each pin is as tall as its
//! content and the selected pin rests at a fixed point of the screen.

//! A pin of the list, in on-screen order from the top.
typedef struct TimelineListPin {
  //! Height of the pin, from the top of its content to the top of the next pin.
  int16_t height;
  //! Offset from the pin's top of the point that rests on the geometry's anchor when selected.
  int16_t anchor_y;
} TimelineListPin;

//! The viewport the list is laid out in.
typedef struct TimelineListGeometry {
  //! Height of the viewport.
  int16_t height;
  //! Where the selected pin's anchor rests.
  int16_t anchor_y;
  //! Space kept above the selected pin and at the filled edges.
  int16_t margin;
  //! The top pin starts the list: move the list up rather than leave space above that pin.
  bool fill_top;
  //! The bottom pin starts the list: move the list down rather than leave space below that pin.
  bool fill_bottom;
} TimelineListGeometry;

//! Computes the top of each pin.
//!
//! The pins stack without gaps. The selected pin's anchor rests on the geometry's anchor, unless
//! that would put its top above the margin, so a pin taller than the screen shows from its top and
//! pushes its neighbours off screen. A filled edge then pulls the list towards it, without moving
//! the selected pin's top above the margin.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param[out] tops_out Top of each pin.
void timeline_list_layout(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                          int num_pins, int selected, int16_t *tops_out);

//! Places the pins outside a run of placed pins against their neighbours, without gaps.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins.
//! @param first_placed Index of the first pin of the run whose top is already in tops.
//! @param last_placed Index of the last pin of the run.
//! @param[in,out] tops Top of each pin; only the run is read.
void timeline_list_stack(const TimelineListPin *pins, int num_pins, int first_placed,
                         int last_placed, int16_t *tops);
