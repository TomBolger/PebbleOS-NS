/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! Vertical placement of the pins of the compact timeline list, where each pin is as tall as its
//! content and the selected pin is shown between the pins before and after it.

//! A pin of the list, in on-screen order from the top.
typedef struct TimelineListPin {
  //! Height of the pin, from the top of its content to the top of the next pin.
  int16_t height;
  //! Offset from the pin's top of the point that rests on the geometry's anchor when selected.
  int16_t anchor_y;
} TimelineListPin;

//! Where the list rests.
typedef enum TimelineListAlign {
  //! The selected pin's anchor rests on the geometry's anchor.
  TimelineListAlignAnchor,
  //! The top pin rests below the top margin and the geometry's top space.
  TimelineListAlignTop,
} TimelineListAlign;

//! The viewport the list is laid out in.
typedef struct TimelineListGeometry {
  //! Height of the viewport.
  int16_t height;
  //! Where the selected pin's anchor rests, with TimelineListAlignAnchor.
  int16_t anchor_y;
  //! Space kept free at the top and bottom of the viewport.
  int16_t margin;
  //! Space kept above the top pin with TimelineListAlignTop, for what shows before it.
  int16_t top_space;
  TimelineListAlign align;
} TimelineListGeometry;

//! Computes the top of each pin.
//!
//! The pins stack without gaps and rest as the geometry aligns them, except that the selected
//! pin's top never rests above the margin: a selected pin taller than the screen shows from its
//! top and pushes its neighbours off screen.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param[out] tops_out Top of each pin.
void timeline_list_layout(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                          int num_pins, int selected, int16_t *tops_out);

//! Bottom of the page the selected pin shows its text in: above the pin after it on screen, so
//! that pin stays whole on screen, unless that leaves the page too short. A page is never
//! shorter than the shortest page, even if it goes on below the screen.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param page_top Top of the page.
//! @param min_height Shortest page.
//! @return Bottom of the page.
int16_t timeline_list_page_bottom(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                                  int num_pins, int selected, int16_t page_top, int16_t min_height);

//! How far text scrolls through its pages to show its end, in whole pages.
//! @param content_height Height of the text, laid out in pages.
//! @param page_height Height of a page.
//! @return The largest scroll, a multiple of the page height, or 0 if the text fits on a page.
int16_t timeline_list_max_scroll(int16_t content_height, int16_t page_height);

//! Places the pins outside a run of placed pins against their neighbours, without gaps.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins.
//! @param first_placed Index of the first pin of the run whose top is already in tops.
//! @param last_placed Index of the last pin of the run.
//! @param[in,out] tops Top of each pin; only the run is read.
void timeline_list_stack(const TimelineListPin *pins, int num_pins, int first_placed,
                         int last_placed, int16_t *tops);

//! How far in from the side of a round display its edge is, at a distance from its middle row.
//! @param radius Radius of the display.
//! @param distance_y Distance from the middle row; beyond the radius counts as the radius.
//! @return Distance from the side of the display's bounds to its edge.
int16_t timeline_list_round_edge_inset(int16_t radius, int16_t distance_y);
