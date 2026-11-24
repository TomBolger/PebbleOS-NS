/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/util/math.h>

#include <applib/ui/menu_layer_round.h>
#include <clar.h>

#define DISPLAY GSize(260, 260)
#define BOUNDS  GRect(0, 0, 260, 260)
#define ROW_H   52

static int32_t prv_distance(int32_t dx, int32_t dy) {
  return integer_sqrt((int64_t)dx * dx + (int64_t)dy * dy);
}

//! Both end circles of \a pill stay \a margin clear of the bezel
static void prv_assert_ends_inside(const MenuRoundShape *pill, int32_t left_margin) {
  const int32_t r = pill->radius;
  const int32_t top_y = pill->box.origin.y + r;
  const int32_t bottom_y = grect_get_max_y(&pill->box) - r;
  const int32_t left_x = pill->box.origin.x + r;
  const int32_t right_x = grect_get_max_x(&pill->box) - r;
  for (int32_t y = top_y; y <= bottom_y; y++) {
    cl_assert(prv_distance(left_x - 130, y - 130) + r <=
              130 - MENU_ROUND_PILL_BEZEL_MARGIN - left_margin);
    cl_assert(prv_distance(right_x - 130, y - 130) + r <= 130 - MENU_ROUND_PILL_BEZEL_MARGIN);
  }
}

void test_menu_layer_round__pill_in_the_middle_spans_the_display(void) {
  MenuRoundShape pill;
  cl_assert(menu_round_pill(DISPLAY, BOUNDS, GRect(0, 104, 260, ROW_H), ROW_H, 0, &pill));
  cl_assert_equal_i(pill.box.origin.y, 104 + MENU_ROUND_PILL_INSET_Y);
  cl_assert_equal_i(pill.box.size.h, ROW_H - 2 * MENU_ROUND_PILL_INSET_Y);
  cl_assert_equal_i(pill.radius, pill.box.size.h / 2);
  cl_assert_equal_i(pill.box.origin.x, MENU_ROUND_PILL_BEZEL_MARGIN);
  cl_assert_equal_i(grect_get_max_x(&pill.box), 260 - MENU_ROUND_PILL_BEZEL_MARGIN);
}

void test_menu_layer_round__pill_follows_the_bezel(void) {
  for (int16_t y = 10; y < 200; y += 7) {
    MenuRoundShape pill;
    cl_assert(menu_round_pill(DISPLAY, BOUNDS, GRect(0, y, 260, ROW_H), ROW_H, 0, &pill));
    prv_assert_ends_inside(&pill, 0);
    // Symmetric about the middle
    cl_assert_equal_i(pill.box.origin.x, 260 - grect_get_max_x(&pill.box));
  }
}

void test_menu_layer_round__pill_left_margin(void) {
  MenuRoundShape plain;
  MenuRoundShape inset;
  const GRect frame = GRect(0, 60, 260, ROW_H);
  cl_assert(menu_round_pill(DISPLAY, BOUNDS, frame, ROW_H, 0, &plain));
  cl_assert(menu_round_pill(DISPLAY, BOUNDS, frame, ROW_H, 7, &inset));
  cl_assert(inset.box.origin.x > plain.box.origin.x);
  cl_assert_equal_i(grect_get_max_x(&inset.box), grect_get_max_x(&plain.box));
  prv_assert_ends_inside(&inset, 7);
}

void test_menu_layer_round__stretched_pill_keeps_its_radius(void) {
  MenuRoundShape pill;
  cl_assert(menu_round_pill(DISPLAY, BOUNDS, GRect(0, 90, 260, 2 * ROW_H), ROW_H, 0, &pill));
  cl_assert_equal_i(pill.radius, (ROW_H - 2 * MENU_ROUND_PILL_INSET_Y) / 2);
  cl_assert_equal_i(pill.box.size.h, 2 * ROW_H - 2 * MENU_ROUND_PILL_INSET_Y);
  prv_assert_ends_inside(&pill, 0);
}

void test_menu_layer_round__pill_stays_in_bounds(void) {
  MenuRoundShape pill;
  cl_assert(
      menu_round_pill(DISPLAY, GRect(40, 0, 180, 260), GRect(0, 104, 260, ROW_H), ROW_H, 0, &pill));
  cl_assert_equal_i(pill.box.origin.x, 40);
  cl_assert_equal_i(grect_get_max_x(&pill.box), 220);
}

void test_menu_layer_round__no_pill_without_room(void) {
  MenuRoundShape pill;
  cl_assert(!menu_round_pill(DISPLAY, BOUNDS, GRect(0, 0, 260, 6), 6, 0, &pill));
}
