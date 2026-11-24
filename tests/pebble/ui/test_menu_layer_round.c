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

static int16_t prv_row_h(uint16_t i, void *context) {
  return ((const int16_t *)context)[i];
}

void test_menu_layer_round__end_rows_reach_the_middle(void) {
  const int16_t rows[] = {68, 68, 68, 68, 68};
  // With the space, the end row and its neighbour, which sits in the middle
  const int16_t space = menu_round_end_space(260, 68, 68);
  cl_assert_equal_i(menu_round_end_rows(260, space, 5, prv_row_h, (void *)rows), 2);
  // Without, every row whose middle isn't past the middle
  const int16_t short_rows[] = {32, 32, 32, 32, 32, 32};
  cl_assert_equal_i(menu_round_end_rows(260, 0, 6, prv_row_h, (void *)short_rows), 4);
  // No more rows than the list has
  cl_assert_equal_i(menu_round_end_rows(260, 0, 3, prv_row_h, (void *)short_rows), 3);
  cl_assert_equal_i(menu_round_end_rows(260, 0, 0, prv_row_h, (void *)short_rows), 0);
}

void test_menu_layer_round__end_rows_keep_the_end_row(void) {
  const int16_t rows[] = {300, 20};
  cl_assert_equal_i(menu_round_end_rows(260, 0, 2, prv_row_h, (void *)rows), 1);
}

#define FOCUSED_H   68
#define UNFOCUSED_H 32
#define MAX_ROWS    12

//! Lays out \a count rows with \a selected selected, as a round list with the pill does: the rows
//! it selects while resting at an end at their focused height, the others focused only when
//! selected. Fills \a screen_y with where each row starts on screen.
static void prv_layout(uint16_t count, uint16_t selected, int16_t *screen_y) {
  int16_t focused[MAX_ROWS];
  for (uint16_t i = 0; i < count; i++) {
    focused[i] = FOCUSED_H;
  }
  const int16_t top = menu_round_end_space(260, FOCUSED_H, (count > 1) ? FOCUSED_H : 0);
  const int16_t bottom = top;
  const uint16_t top_rows = menu_round_end_rows(260, top, count, prv_row_h, focused);
  const uint16_t bottom_rows = menu_round_end_rows(260, bottom, count, prv_row_h, focused);
  int16_t y[MAX_ROWS];
  int16_t h[MAX_ROWS];
  int16_t content_h = 0;
  for (uint16_t i = 0; i < count; i++) {
    const bool pinned = (i < top_rows) || (count - 1 - i < bottom_rows);
    h[i] = (pinned || (i == selected)) ? FOCUSED_H : UNFOCUSED_H;
    y[i] = content_h;
    content_h += h[i];
  }
  const int16_t offset =
      menu_round_rest_offset(260, content_h, y[selected], h[selected], top, bottom);
  for (uint16_t i = 0; i < count; i++) {
    screen_y[i] = offset + y[i];
  }
}

static void prv_assert_still(uint16_t count, uint16_t from, uint16_t to) {
  int16_t before[MAX_ROWS];
  int16_t after[MAX_ROWS];
  prv_layout(count, from, before);
  prv_layout(count, to, after);
  for (uint16_t i = 0; i < count; i++) {
    cl_assert_equal_i(before[i], after[i]);
  }
}

void test_menu_layer_round__rows_stay_still_in_a_list_that_fits(void) {
  for (uint16_t count = 1; count <= 3; count++) {
    for (uint16_t row = 1; row < count; row++) {
      prv_assert_still(count, 0, row);
    }
  }
  // Centred
  int16_t y[MAX_ROWS];
  prv_layout(3, 0, y);
  cl_assert_equal_i(y[0], (260 - 3 * FOCUSED_H) / 2);
}

void test_menu_layer_round__rows_stay_still_at_the_ends(void) {
  prv_assert_still(10, 0, 1);
  prv_assert_still(10, 8, 9);
  // Past them, the selected row scrolls to the middle
  for (uint16_t row = 1; row <= 8; row++) {
    int16_t y[MAX_ROWS];
    prv_layout(10, row, y);
    cl_assert_equal_i(y[row] + FOCUSED_H / 2, 130);
  }
}
