/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include <pbl/util/math.h>

#include <apps/system/timeline/sidebar_curve.h>

static const TimelineSidebarCurve s_curve = {
  .height = 260,
  .wide_x = 174,
  .narrow_x = 224,
  .bend = 80,
};

void test_timeline_sidebar_curve__straight_at_the_ends(void) {
  for (int16_t y = 0; y <= 130 - 80; y++) {
    cl_assert_equal_i(timeline_sidebar_curve_x(&s_curve, y), 174);
    cl_assert_equal_i(timeline_sidebar_curve_x(&s_curve, 259 - y + 1), 174);
  }
}

void test_timeline_sidebar_curve__narrowest_in_the_middle(void) {
  cl_assert_equal_i(timeline_sidebar_curve_x(&s_curve, 130), 224);
  // Halfway through the bend, the edge is halfway out
  cl_assert_equal_i(timeline_sidebar_curve_x(&s_curve, 130 - 40), 199);
  cl_assert_equal_i(timeline_sidebar_curve_x(&s_curve, 130 + 40), 199);
}

void test_timeline_sidebar_curve__bends_smoothly(void) {
  int16_t previous = timeline_sidebar_curve_x(&s_curve, 0);
  int16_t previous_step = 0;
  for (int16_t y = 1; y <= 130; y++) {
    const int16_t x = timeline_sidebar_curve_x(&s_curve, y);
    const int16_t step = x - previous;
    // Moves out toward the middle, never by much on one row, and eases in and out of the bend
    cl_assert(step >= 0);
    cl_assert(step <= 2);
    cl_assert(PBL_ABS(step - previous_step) <= 1);
    previous = x;
    previous_step = step;
  }
}

void test_timeline_sidebar_curve__min_x(void) {
  // Over rows around the middle, the ends of the range are farthest left
  cl_assert_equal_i(timeline_sidebar_curve_min_x(&s_curve, 110, 151),
                    timeline_sidebar_curve_x(&s_curve, 110));
  cl_assert_equal_i(timeline_sidebar_curve_min_x(&s_curve, 120, 200),
                    timeline_sidebar_curve_x(&s_curve, 199));
  cl_assert_equal_i(timeline_sidebar_curve_min_x(&s_curve, 0, 260), 174);
}

void test_timeline_sidebar_curve__center_x(void) {
  // On the middle row the display's edge is at its side
  cl_assert_equal_i(timeline_sidebar_curve_center_x(&s_curve, 130), (224 + 260) / 2);
  // At the top the bar narrows with the display
  const int16_t top_center = timeline_sidebar_curve_center_x(&s_curve, 40);
  cl_assert(top_center > 174);
  cl_assert(top_center < (174 + 260) / 2);
}
