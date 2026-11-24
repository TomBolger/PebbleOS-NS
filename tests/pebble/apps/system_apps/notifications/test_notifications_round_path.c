/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <apps/system/notifications_round_path.h>
#include <clar.h>

// Gabbro and Chalk
static const int16_t s_radii[] = {130, 90};

static int16_t prv_rim(int16_t radius, int16_t dy) {
  return radius - integer_sqrt((int64_t)radius * radius - (int64_t)dy * dy);
}

static int16_t prv_x(int16_t radius, int16_t dy) {
  return notifications_round_path_x(radius, dy);
}

void test_notifications_round_path__follows_the_rim_in_the_middle(void) {
  for (unsigned i = 0; i < PBL_ARRAY_SIZE(s_radii); i++) {
    const int16_t r = s_radii[i];
    for (int16_t dy = 0; dy <= r / 2; dy++) {
      cl_assert_equal_i(prv_x(r, dy), prv_rim(r, dy));
    }
  }
}

void test_notifications_round_path__same_above_and_below(void) {
  for (unsigned i = 0; i < PBL_ARRAY_SIZE(s_radii); i++) {
    const int16_t r = s_radii[i];
    for (int16_t dy = 0; dy <= 3 * r; dy++) {
      cl_assert_equal_i(prv_x(r, -dy), prv_x(r, dy));
    }
  }
}

void test_notifications_round_path__leaves_the_rim_gently(void) {
  for (unsigned i = 0; i < PBL_ARRAY_SIZE(s_radii); i++) {
    const int16_t r = s_radii[i];
    for (int16_t dy = 0; dy < r; dy++) {
      // Never outside the rim, give or take rounding, so never thrown sideways where it turns
      // vertical.
      cl_assert(prv_x(r, dy) <= prv_rim(r, dy) + 1);
    }
    // Still on screen where the rim meets the top and bottom, and goes on past them.
    cl_assert(prv_x(r, r) < r * 3 / 4);
    cl_assert(prv_x(r, 2 * r) > prv_x(r, r) + r / 2);
  }
}

void test_notifications_round_path__smooth(void) {
  for (unsigned i = 0; i < PBL_ARRAY_SIZE(s_radii); i++) {
    const int16_t r = s_radii[i];
    const int16_t step = 8;
    for (int16_t dy = step; dy <= 3 * r; dy++) {
      const int16_t before = prv_x(r, dy) - prv_x(r, dy - step);
      const int16_t after = prv_x(r, dy + step) - prv_x(r, dy);
      // Moves outwards, mostly up or down: at most 0.9 px across per px along.
      cl_assert(before >= 0);
      cl_assert(after <= step);
      // No kink anywhere, the join included: the slope changes only a little over a few px, and
      // only up, give or take rounding.
      cl_assert(after - before >= -1);
      cl_assert(after - before <= 3);
    }
  }
}
