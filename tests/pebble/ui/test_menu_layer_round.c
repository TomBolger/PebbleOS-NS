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

// 10 rows of ROW_H in a 260 px frame
#define CONTENT_H (10 * ROW_H)
#define SPACE     (130 - ROW_H - ROW_H / 2)

static int16_t prv_rest(int16_t row, int16_t space) {
  return menu_round_rest_offset(260, CONTENT_H, row * ROW_H, ROW_H, space, space);
}

void test_menu_layer_round__end_space(void) {
  cl_assert_equal_i(menu_round_end_space(260, ROW_H, ROW_H), SPACE);
  // A header tall enough to put the next row in the middle needs no space
  cl_assert_equal_i(menu_round_end_space(260, 130 - ROW_H / 2, ROW_H), 0);
  cl_assert_equal_i(menu_round_end_space(260, 200, ROW_H), 0);
}

void test_menu_layer_round__rest_offset_centres_the_middle(void) {
  cl_assert_equal_i(prv_rest(4, SPACE), 130 - 4 * ROW_H - ROW_H / 2);
}

void test_menu_layer_round__rest_offset_stops_at_the_ends(void) {
  cl_assert_equal_i(prv_rest(0, 0), 0);
  cl_assert_equal_i(prv_rest(1, 0), 0);
  cl_assert_equal_i(prv_rest(9, 0), 260 - CONTENT_H);
}

void test_menu_layer_round__rest_offset_near_the_middle(void) {
  // The first row rests with the second one in the middle...
  const int16_t first = prv_rest(0, SPACE);
  cl_assert_equal_i(first + ROW_H + ROW_H / 2, 130);
  // ... where the second one rests too, so moving between them doesn't move the list
  cl_assert_equal_i(prv_rest(1, SPACE), first);
  // Likewise at the bottom
  const int16_t last = prv_rest(9, SPACE);
  cl_assert_equal_i(last + 8 * ROW_H + ROW_H / 2, 130);
  cl_assert_equal_i(prv_rest(8, SPACE), last);
  // Rows in between scroll a row at a time
  for (int16_t row = 2; row < 8; row++) {
    cl_assert_equal_i(prv_rest(row, SPACE), first - (row - 1) * ROW_H);
  }
}

void test_menu_layer_round__rest_offset_under_a_header(void) {
  const int16_t header_h = 130 - ROW_H / 2;
  const int16_t content_h = header_h + 9 * ROW_H;
  const int16_t top = menu_round_end_space(260, header_h, ROW_H);
  // The header at the top, and the row under it in the middle, rest without moving the list
  cl_assert_equal_i(menu_round_rest_offset(260, content_h, 0, header_h, top, SPACE), 0);
  cl_assert_equal_i(menu_round_rest_offset(260, content_h, header_h, ROW_H, top, SPACE), 0);
}

void test_menu_layer_round__rest_offset_centres_a_list_that_fits(void) {
  cl_assert_equal_i(menu_round_rest_offset(260, 3 * ROW_H, ROW_H, ROW_H, 0, 0),
                    (260 - 3 * ROW_H) / 2);
  cl_assert_equal_i(menu_round_rest_offset(260, 2 * ROW_H, 0, ROW_H, 40, 40),
                    (260 - 2 * ROW_H) / 2);
}

void test_menu_layer_round__rest_offset_measures_from_the_selection(void) {
  // Content measured before the last row grew still ends the list at that row's bottom
  cl_assert_equal_i(menu_round_rest_offset(260, CONTENT_H - 10, 9 * ROW_H, ROW_H, 0, 0),
                    260 - CONTENT_H);
}
