/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <applib/ui/animation_interpolate.h>
#include <applib/ui/list_wrap.h>
#include <clar.h>

#define FRAME_H 260
#define ROW_H   52

//! The laps of a wrap show exactly what the list shows at rest at both ends: the lap landed on is
//! off the frame as the wrap starts, where the lap left is the list as it was, and the lap left is
//! off the frame once done. The laps never overlap.
static void prv_assert_laps(bool down, int16_t content_h, int16_t from_offset, int16_t to_offset) {
  const ListWrapLaps laps = list_wrap_laps(down, FRAME_H, content_h, from_offset, to_offset);
  const int16_t landed_start = to_offset + laps.from_shift;
  const int16_t left_start = landed_start + laps.loop;
  const int16_t left_end = to_offset + laps.loop;
  cl_assert_equal_i(left_start, from_offset);
  if (down) {
    cl_assert(laps.from_shift > 0);
    cl_assert(laps.loop < 0);
    cl_assert(landed_start >= FRAME_H);
    cl_assert(left_end + content_h <= 0);
  } else {
    cl_assert(laps.from_shift < 0);
    cl_assert(laps.loop > 0);
    cl_assert(landed_start + content_h <= 0);
    cl_assert(left_end >= FRAME_H);
  }
  cl_assert((laps.loop > 0 ? laps.loop : -laps.loop) >= content_h);
}

// A list resting a row past either end, as round lists with the pill do: the first row selected
// a row above the middle, the last a row below it
#define LONG_CONTENT_H (10 * ROW_H)
#define LONG_TOP       ROW_H
#define LONG_BOTTOM    (FRAME_H - LONG_CONTENT_H - ROW_H)

void test_list_wrap__down_carries_on_round_a_loop(void) {
  const ListWrapLaps laps = list_wrap_laps(true, FRAME_H, LONG_CONTENT_H, LONG_BOTTOM, LONG_TOP);
  // The list moves up four rows, from the last row a row below the middle to the first row a row
  // above it, with one row's gap between the last row and the first
  cl_assert_equal_i(laps.from_shift, 4 * ROW_H);
  cl_assert_equal_i(laps.loop, -(LONG_CONTENT_H + ROW_H));
  prv_assert_laps(true, LONG_CONTENT_H, LONG_BOTTOM, LONG_TOP);
}

void test_list_wrap__up_is_the_mirror_of_down(void) {
  const ListWrapLaps down = list_wrap_laps(true, FRAME_H, LONG_CONTENT_H, LONG_BOTTOM, LONG_TOP);
  const ListWrapLaps up = list_wrap_laps(false, FRAME_H, LONG_CONTENT_H, LONG_TOP, LONG_BOTTOM);
  cl_assert_equal_i(up.from_shift, -down.from_shift);
  cl_assert_equal_i(up.loop, -down.loop);
  prv_assert_laps(false, LONG_CONTENT_H, LONG_TOP, LONG_BOTTOM);
}

void test_list_wrap__a_list_that_fits_goes_all_the_way_round(void) {
  // Three rows centred in the frame: the list rests in one place for every selection, so the wrap
  // carries it out of one edge and back in at the other, a row's gap behind it
  const int16_t content_h = 3 * ROW_H;
  const int16_t offset = (FRAME_H - content_h) / 2;
  const ListWrapLaps laps = list_wrap_laps(true, FRAME_H, content_h, offset, offset);
  cl_assert_equal_i(laps.from_shift, content_h + ROW_H);
  cl_assert_equal_i(laps.loop, -(content_h + ROW_H));
  prv_assert_laps(true, content_h, offset, offset);
  prv_assert_laps(false, content_h, offset, offset);
}

void test_list_wrap__a_list_stopping_at_its_ends_closes_the_loop(void) {
  // A list whose end spaces are content of its own needs no gap between its laps
  const int16_t content_h = 600;
  const ListWrapLaps laps = list_wrap_laps(true, FRAME_H, content_h, FRAME_H - content_h, 0);
  cl_assert_equal_i(laps.loop, -content_h);
  prv_assert_laps(true, content_h, FRAME_H - content_h, 0);
  prv_assert_laps(false, content_h, 0, FRAME_H - content_h);
}

void test_list_wrap__carries_on_from_a_list_still_moving(void) {
  // The wrap starts from wherever a step still under way left the list
  for (int16_t from = LONG_BOTTOM - ROW_H; from <= LONG_BOTTOM + ROW_H; from += 7) {
    prv_assert_laps(true, LONG_CONTENT_H, from, LONG_TOP);
  }
  for (int16_t from = LONG_TOP - ROW_H; from <= LONG_TOP + ROW_H; from += 7) {
    prv_assert_laps(false, LONG_CONTENT_H, from, LONG_BOTTOM);
  }
}

void test_list_wrap__curve_is_a_round_list_step(void) {
  cl_assert_equal_i(list_wrap_duration(), interpolate_moook_soft_duration(3));
  cl_assert_equal_i(list_wrap_curve(0, 208, 0), 208);
  cl_assert_equal_i(list_wrap_curve(ANIMATION_NORMALIZED_MAX, 208, 0), 0);
  // Moook: it eases in the way it goes and settles back from a little past the end
  cl_assert(list_wrap_curve(ANIMATION_NORMALIZED_MAX / 4, 208, 0) < 208);
  cl_assert(list_wrap_curve(ANIMATION_NORMALIZED_MAX * 65 / 100, 208, 0) < 0);
}

static int s_updates;
static int s_stops;
static int16_t s_last_shift;

static void prv_update(ListWrap *wrap, void *context) {
  s_updates++;
  s_last_shift = list_wrap_shift(wrap);
}

static void prv_stopped(ListWrap *wrap, void *context) {
  s_stops++;
  cl_assert_equal_p(wrap->animation, nullptr);
}

void test_list_wrap__stop_lands_it_once(void) {
  s_updates = 0;
  s_stops = 0;
  ListWrap wrap;
  const ListWrapLaps laps = list_wrap_laps(true, FRAME_H, LONG_CONTENT_H, LONG_BOTTOM, LONG_TOP);
  cl_assert(list_wrap_start(&wrap, laps, prv_update, prv_stopped, nullptr));
  cl_assert(wrap.animation != nullptr);
  // Before its first frame it shows the list as it was
  cl_assert_equal_i(list_wrap_shift(&wrap), laps.from_shift);
  cl_assert_equal_i(list_wrap_interpolate(&wrap, 10, 90), 10);

  list_wrap_stop(&wrap);
  cl_assert_equal_i(s_updates, 1);
  cl_assert_equal_i(s_stops, 1);
  cl_assert_equal_i(s_last_shift, 0);
  cl_assert_equal_i(list_wrap_interpolate(&wrap, 10, 90), 90);

  list_wrap_stop(&wrap);
  cl_assert_equal_i(s_updates, 1);
  cl_assert_equal_i(s_stops, 1);
}
