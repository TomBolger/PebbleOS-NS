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

//! Rows keep one height whatever is selected, as many fitting as on a stock round menu
void test_menu_layer_round__row_height_fits_five_rows(void) {
  cl_assert_equal_i(menu_round_row_height(260, 68, 32), 52);
  cl_assert(MENU_ROUND_VISIBLE_ROWS * menu_round_row_height(244, 84, 24) <= 244);
  // A row shorter than that keeps its height
  cl_assert_equal_i(menu_round_row_height(260, 40, 30), 40);
  // A card, taller than that even unselected, keeps its height selected
  cl_assert_equal_i(menu_round_row_height(260, 120, 100), 120);
  cl_assert_equal_i(menu_round_row_height(260, 120, 120), 120);
  // So does a page, taller than half the frame selected
  cl_assert_equal_i(menu_round_row_height(260, 260, 0), 260);
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

void test_menu_layer_round__rest_offset_under_a_banner(void) {
  // A header as tall as the space past the top takes its place: the first row rests under it,
  // with the second one in the middle, and selecting the second one doesn't move the list
  const int16_t header_h = SPACE;
  const int16_t content_h = header_h + 10 * ROW_H;
  const int16_t top = menu_round_end_space(260, header_h + ROW_H, ROW_H);
  cl_assert_equal_i(top, 0);
  cl_assert_equal_i(menu_round_rest_offset(260, content_h, header_h, ROW_H, top, SPACE), 0);
  cl_assert_equal_i(menu_round_rest_offset(260, content_h, header_h + ROW_H, ROW_H, top, SPACE), 0);
  // The third one scrolls a row to the middle
  cl_assert_equal_i(menu_round_rest_offset(260, content_h, header_h + 2 * ROW_H, ROW_H, top, SPACE),
                    -ROW_H);
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

#define FOCUSED_H 68
#define MAX_ROWS  12

//! Lays out \a count rows with \a selected selected, as a round list with the pill does: every
//! row at its focused height, whatever is selected. Fills \a screen_y with where each row starts
//! on screen and returns the content offset.
static int16_t prv_layout(uint16_t count, uint16_t selected, int16_t *screen_y) {
  const int16_t space = menu_round_end_space(260, FOCUSED_H, (count > 1) ? FOCUSED_H : 0);
  const int16_t offset =
      menu_round_rest_offset(260, count * FOCUSED_H, selected * FOCUSED_H, FOCUSED_H, space, space);
  for (uint16_t i = 0; i < count; i++) {
    screen_y[i] = offset + i * FOCUSED_H;
  }
  return offset;
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

//! Rows above the middle are spaced as the rows below it, wherever the list rests
void test_menu_layer_round__rows_are_spaced_alike_above_and_below(void) {
  for (uint16_t selected = 0; selected < 10; selected++) {
    int16_t y[MAX_ROWS];
    prv_layout(10, selected, y);
    for (uint16_t i = 1; i < 10; i++) {
      cl_assert_equal_i(y[i] - y[i - 1], FOCUSED_H);
    }
  }
}

//! Every step either leaves the list where it is, moving only the highlight, or scrolls the
//! newly selected row to the middle from a row that was there: never a part of a row
void test_menu_layer_round__every_step_keeps_the_list_or_centres(void) {
  for (uint16_t count = 2; count <= MAX_ROWS; count++) {
    for (uint16_t row = 0; row + 1 < count; row++) {
      int16_t before[MAX_ROWS];
      int16_t after[MAX_ROWS];
      const int16_t from = prv_layout(count, row, before);
      const int16_t to = prv_layout(count, row + 1, after);
      if (from != to) {
        cl_assert_equal_i(before[row] + FOCUSED_H / 2, 130);
        cl_assert_equal_i(after[row + 1] + FOCUSED_H / 2, 130);
        cl_assert_equal_i(from - to, FOCUSED_H);
      }
    }
  }
}

static const MenuRoundShape s_pill = {.box = {{4, 100}, {252, 62}}, .radius = 31};
static const MenuRoundShape s_circle = {.box = {{-221, -10}, {280, 280}}, .radius = 140};

void test_menu_layer_round__morph_starts_and_ends_on_its_shapes(void) {
  const MenuRoundShape start = menu_round_morph(&s_pill, &s_circle, 0);
  cl_assert(grect_equal(&start.box, &s_pill.box));
  cl_assert_equal_i(start.radius, s_pill.radius);
  const MenuRoundShape end = menu_round_morph(&s_pill, &s_circle, 1000);
  cl_assert(grect_equal(&end.box, &s_circle.box));
  cl_assert_equal_i(end.radius, s_circle.radius);
}

//! Every step is a single pill or circle: corners as round as half the shorter side
void test_menu_layer_round__morph_is_one_round_shape_every_step(void) {
  int16_t prev_left = s_pill.box.origin.x;
  for (int32_t progress = 0; progress <= 1000; progress += 50) {
    const MenuRoundShape shape = menu_round_morph(&s_pill, &s_circle, progress);
    cl_assert_equal_i(shape.radius, PBL_MIN(shape.box.size.w, shape.box.size.h) / 2);
    // It moves left all the way
    cl_assert(shape.box.origin.x <= prev_left);
    prev_left = shape.box.origin.x;
  }
}

void test_menu_layer_round__morph_overshoot_keeps_the_size(void) {
  const MenuRoundShape past = menu_round_morph(&s_pill, &s_circle, 1100);
  cl_assert_equal_i(past.box.size.w, s_circle.box.size.w);
  cl_assert_equal_i(past.box.size.h, s_circle.box.size.h);
  cl_assert_equal_i(past.radius, s_circle.radius);
  // ... and carries on left, past the circle
  cl_assert(past.box.origin.x < s_circle.box.origin.x);
}

void test_menu_layer_round__list_scroll(void) {
  // The selection walks down to the middle from two rows above it...
  cl_assert_equal_i(menu_round_list_scroll(0, 8, 38, 2), 0);
  cl_assert_equal_i(menu_round_list_scroll(2, 8, 38, 2), 0);
  // ... then the list scrolls a row at a time...
  cl_assert_equal_i(menu_round_list_scroll(3, 8, 38, 2), 38);
  // ... and stops with the last row two below the middle
  cl_assert_equal_i(menu_round_list_scroll(7, 8, 38, 2), 3 * 38);
  cl_assert_equal_i(menu_round_list_scroll(5, 8, 38, 2), 3 * 38);
  // A short list never scrolls
  cl_assert_equal_i(menu_round_list_scroll(2, 3, 38, 2), 0);
}
