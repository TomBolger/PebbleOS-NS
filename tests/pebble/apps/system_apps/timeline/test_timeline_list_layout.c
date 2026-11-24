/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include <apps/system/timeline/list_layout.h>

#define HEIGHT 228
#define MARGIN 4

static const TimelineListGeometry s_centered = {
  .height = HEIGHT,
  .anchor_y = HEIGHT / 2,
  .margin = MARGIN,
};

//! Pins anchored at their middle, as when the selected pin is centered
static TimelineListPin prv_pin(int16_t height) {
  return (TimelineListPin){.height = height, .anchor_y = height / 2};
}

static void prv_assert_stacked(const TimelineListPin *pins, int num_pins, const int16_t *tops) {
  for (int i = 1; i < num_pins; i++) {
    cl_assert_equal_i(tops[i], tops[i - 1] + pins[i - 1].height);
  }
}

void test_timeline_list_layout__selected_rests_on_anchor(void) {
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(30), prv_pin(60), prv_pin(30), prv_pin(30)};
  int16_t tops[5];
  timeline_list_layout(&s_centered, pins, 5, 2, tops);
  cl_assert_equal_i(tops[2], HEIGHT / 2 - 30);
  prv_assert_stacked(pins, 5, tops);
  // Three pins fit around the selected one, and the next ones still show
  cl_assert(tops[0] > 0);
  cl_assert(tops[4] < HEIGHT);
}

void test_timeline_list_layout__anchor_inside_pin(void) {
  const TimelineListPin pins[] = {prv_pin(40), {.height = 70, .anchor_y = 25}, prv_pin(40)};
  int16_t tops[3];
  timeline_list_layout(&s_centered, pins, 3, 1, tops);
  cl_assert_equal_i(tops[1] + 25, HEIGHT / 2);
  prv_assert_stacked(pins, 3, tops);
}

void test_timeline_list_layout__tall_selected_pushes_neighbours_off(void) {
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(HEIGHT + 40), prv_pin(30)};
  int16_t tops[3];
  timeline_list_layout(&s_centered, pins, 3, 1, tops);
  // The selected pin shows from its top, its neighbours are off screen
  cl_assert_equal_i(tops[1], MARGIN);
  cl_assert(tops[0] + pins[0].height <= MARGIN);
  cl_assert(tops[2] >= HEIGHT);
}

void test_timeline_list_layout__fill_top(void) {
  TimelineListGeometry geometry = s_centered;
  geometry.fill_top = true;
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(60), prv_pin(30), prv_pin(30)};
  int16_t tops[4];
  // Nothing before the first pin: the list starts at the margin
  timeline_list_layout(&geometry, pins, 4, 1, tops);
  cl_assert_equal_i(tops[0], MARGIN);
  prv_assert_stacked(pins, 4, tops);

  // Enough pins above the selected one: it stays on the anchor
  const TimelineListPin many[] = {prv_pin(40), prv_pin(40), prv_pin(40), prv_pin(60)};
  timeline_list_layout(&geometry, many, 4, 3, tops);
  cl_assert_equal_i(tops[3], HEIGHT / 2 - 30);
}

void test_timeline_list_layout__fill_bottom(void) {
  TimelineListGeometry geometry = s_centered;
  geometry.fill_bottom = true;
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(30), prv_pin(60)};
  int16_t tops[3];
  timeline_list_layout(&geometry, pins, 3, 2, tops);
  cl_assert_equal_i(tops[2] + pins[2].height, HEIGHT - MARGIN);
  prv_assert_stacked(pins, 3, tops);
}

void test_timeline_list_layout__fill_top_keeps_tall_selected_visible(void) {
  TimelineListGeometry geometry = s_centered;
  geometry.fill_top = true;
  const TimelineListPin pins[] = {prv_pin(HEIGHT * 2), prv_pin(30)};
  int16_t tops[2];
  timeline_list_layout(&geometry, pins, 2, 0, tops);
  cl_assert_equal_i(tops[0], MARGIN);
}

void test_timeline_list_layout__stack_around_run(void) {
  const TimelineListPin pins[] = {prv_pin(10), prv_pin(20), prv_pin(30), prv_pin(40), prv_pin(50)};
  int16_t tops[5] = {0, 0, 100, 130, 0};
  timeline_list_stack(pins, 5, 2, 3, tops);
  cl_assert_equal_i(tops[1], 80);
  cl_assert_equal_i(tops[0], 70);
  cl_assert_equal_i(tops[4], 170);
}

void test_timeline_list_layout__round_edge_inset(void) {
  // The middle row reaches the sides, the top and bottom rows are a point in the middle
  cl_assert_equal_i(timeline_list_round_edge_inset(130, 0), 0);
  cl_assert_equal_i(timeline_list_round_edge_inset(130, 130), 130);
  cl_assert_equal_i(timeline_list_round_edge_inset(130, -200), 130);
  // 3-4-5 triangle: 50 rows up, the edge is 120 from the middle
  cl_assert_equal_i(timeline_list_round_edge_inset(130, -50), 10);
}
