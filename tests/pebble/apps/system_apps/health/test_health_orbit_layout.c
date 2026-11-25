/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"
#include "pebble_asserts.h"

#include <pbl/util/math.h>
#include <pbl/util/trig.h>

#include <apps/system/health/orbit_layout.h>

#define DISPLAY GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT)
#define MID_Y   (PBL_DISPLAY_HEIGHT / 2)

#define QUARTER (TRIG_MAX_ANGLE / 4)

static int32_t prv_distance(GPoint a, GPoint b) {
  const int32_t dx = a.x - b.x;
  const int32_t dy = a.y - b.y;
  return integer_sqrt(dx * dx + dy * dy);
}

//! Point \a radius from the middle of the display at \a angle, clockwise from the top
static GPoint prv_polar(int32_t radius, int32_t angle) {
  return GPoint(PBL_DISPLAY_WIDTH / 2 + (radius * sin_lookup(angle)) / TRIG_MAX_RATIO,
                MID_Y - (radius * cos_lookup(angle)) / TRIG_MAX_RATIO);
}

void test_health_orbit_layout__sidebar_is_the_timeline_sidebar(void) {
  // Unnotched, it reaches as deep as the round banner does, notched its notch cuts into it
  const TimelineListSidebar plain = health_orbit_sidebar(DISPLAY, 0);
  const int16_t depth = HEALTH_ORBIT_CENTER_INSET + HEALTH_ORBIT_RADIUS + 1;
  cl_assert_equal_i(timeline_list_sidebar_depth(&plain, MID_Y), depth);
  cl_assert_equal_i(depth, LAYOUT_TOP_BANNER_HEIGHT_ROUND);
  const TimelineListSidebar notched = health_orbit_sidebar(DISPLAY, HEALTH_ORBIT_NOTCH_WIDTH);
  cl_assert_equal_i(timeline_list_sidebar_depth(&notched, MID_Y), depth - HEALTH_ORBIT_NOTCH_WIDTH);
  cl_assert_equal_i(timeline_list_sidebar_depth(&notched, MID_Y + HEALTH_ORBIT_NOTCH_HEIGHT),
                    timeline_list_sidebar_depth(&plain, MID_Y + HEALTH_ORBIT_NOTCH_HEIGHT));
}

void test_health_orbit_layout__quarter_turn_maps_the_display_onto_itself(void) {
  cl_assert_equal_gpoint(health_orbit_turn(DISPLAY, GPoint(0, 0), QUARTER),
                         GPoint(PBL_DISPLAY_WIDTH - 1, 0));
  cl_assert_equal_gpoint(health_orbit_turn(DISPLAY, GPoint(0, 0), -QUARTER),
                         GPoint(0, PBL_DISPLAY_HEIGHT - 1));
  cl_assert_equal_gpoint(health_orbit_turn(DISPLAY, GPoint(12, 34), 0), GPoint(12, 34));
  cl_assert_equal_gpoint(health_orbit_turn(DISPLAY, GPoint(12, 34), 4 * QUARTER), GPoint(12, 34));
}

void test_health_orbit_layout__sidebar_turns_into_the_banner(void) {
  // On the right of the display at rest, on the middle row
  const GPoint rest = health_orbit_center(DISPLAY, 0);
  cl_assert_equal_i(rest.y, MID_Y);
  cl_assert_equal_i(PBL_DISPLAY_WIDTH - 1 - rest.x, HEALTH_ORBIT_CENTER_INSET);
  // A quarter turn back, it is the round notification banner at the top
  const GPoint top = health_orbit_center(DISPLAY, -QUARTER);
  cl_assert_equal_i(top.y, LAYOUT_TOP_BANNER_CENTER_Y_ROUND);
  cl_assert_equal_i(top.x, PBL_DISPLAY_WIDTH / 2);
  // On the way it stays as far from the middle of the display
  const int32_t distance = prv_distance(rest, GPoint(PBL_DISPLAY_WIDTH / 2, MID_Y));
  for (int32_t turn = 0; turn >= -QUARTER; turn -= QUARTER / 9) {
    const GPoint center = health_orbit_center(DISPLAY, turn);
    cl_assert(PBL_ABS(prv_distance(center, GPoint(PBL_DISPLAY_WIDTH / 2, MID_Y)) - distance) <= 1);
  }
}

void test_health_orbit_layout__notch_points_at_the_middle_row(void) {
  const TimelineListSidebar notched = health_orbit_sidebar(DISPLAY, HEALTH_ORBIT_NOTCH_WIDTH);
  const GPoint tip = health_orbit_notch_tip(DISPLAY, 0);
  cl_assert_equal_i(tip.y, MID_Y);
  cl_assert_equal_i(tip.x, PBL_DISPLAY_WIDTH - timeline_list_sidebar_depth(&notched, MID_Y));
  // Turned to the top, it points up at the middle column from the banner's edge, closed
  const GPoint top = health_orbit_notch_tip(DISPLAY, -QUARTER);
  cl_assert_equal_i(top.x, PBL_DISPLAY_WIDTH / 2);
  cl_assert_equal_i(top.y, LAYOUT_TOP_BANNER_HEIGHT_ROUND - 1);
}

void test_health_orbit_layout__notch_closes_as_the_sidebar_turns_up(void) {
  cl_assert_equal_i(health_orbit_notch_width(0), HEALTH_ORBIT_NOTCH_WIDTH);
  cl_assert_equal_i(health_orbit_notch_width(-QUARTER / 2), HEALTH_ORBIT_NOTCH_WIDTH / 2);
  cl_assert_equal_i(health_orbit_notch_width(-QUARTER), 0);
  // Past the top, as the turn overshoots, it stays closed
  cl_assert_equal_i(health_orbit_notch_width(-QUARTER - QUARTER / 10), 0);
  int16_t last = HEALTH_ORBIT_NOTCH_WIDTH;
  for (int32_t turn = 0; turn >= -QUARTER; turn -= QUARTER / 16) {
    const int16_t width = health_orbit_notch_width(turn);
    cl_assert(width <= last);
    last = width;
  }
}

void test_health_orbit_layout__bubbles_orbit_the_sidebar(void) {
  const GPoint center = health_orbit_center(DISPLAY, 0);
  const GPoint selected = health_orbit_bubble_center(DISPLAY, 0, 0);
  // At the notch, inside the sidebar, the gap clear of the notch's tip
  cl_assert_equal_i(selected.y, MID_Y);
  cl_assert_equal_i(center.x - selected.x, HEALTH_ORBIT_BUBBLE_DISTANCE);
  cl_assert_equal_i(
      selected.x - HEALTH_ORBIT_SELECTED_RADIUS - health_orbit_notch_tip(DISPLAY, 0).x,
      HEALTH_ORBIT_NOTCH_GAP);
  // Neighbours a step along above and below, on the same circle, further right as it curves
  const GPoint above = health_orbit_bubble_center(DISPLAY, -HEALTH_ORBIT_STEP, 0);
  const GPoint below = health_orbit_bubble_center(DISPLAY, HEALTH_ORBIT_STEP, 0);
  cl_assert_equal_i(above.y, MID_Y - 42);
  cl_assert_equal_i(below.y - MID_Y, MID_Y - above.y);
  cl_assert_equal_i(above.x, below.x);
  cl_assert(below.x > selected.x);
  for (int16_t offset = -2 * HEALTH_ORBIT_STEP; offset <= 2 * HEALTH_ORBIT_STEP; offset += 7) {
    const GPoint bubble = health_orbit_bubble_center(DISPLAY, offset, 0);
    cl_assert(PBL_ABS(prv_distance(bubble, center) - HEALTH_ORBIT_BUBBLE_DISTANCE) <= 1);
  }
  // The neighbours stay inside the bezel
  const int32_t neighbour_radius = 12;
  const GPoint display_center = GPoint(PBL_DISPLAY_WIDTH / 2, MID_Y);
  cl_assert(prv_distance(below, display_center) + neighbour_radius <= PBL_DISPLAY_WIDTH / 2);
  // They turn with the sidebar
  const GPoint top = health_orbit_bubble_center(DISPLAY, 0, -QUARTER);
  cl_assert_equal_i(top.x, PBL_DISPLAY_WIDTH / 2);
  cl_assert_equal_i(top.y, PBL_DISPLAY_WIDTH - 1 - selected.x);
}

void test_health_orbit_layout__orbit_goes_all_the_way_round(void) {
  // A full turn round the sidebar's center is the orbit's length
  cl_assert(PBL_ABS(prv_distance(health_orbit_bubble_center(DISPLAY, 0, 0),
                                 health_orbit_bubble_center(DISPLAY, HEALTH_ORBIT_LENGTH / 2, 0)) -
                    2 * HEALTH_ORBIT_BUBBLE_DISTANCE) <= 1);
  for (int16_t offset = -HEALTH_ORBIT_STEP; offset <= HEALTH_ORBIT_STEP; offset += 5) {
    cl_assert_equal_gpoint(health_orbit_bubble_center(DISPLAY, offset + HEALTH_ORBIT_LENGTH, 0),
                           health_orbit_bubble_center(DISPLAY, offset, 0));
    cl_assert_equal_gpoint(health_orbit_bubble_center(DISPLAY, offset - HEALTH_ORBIT_LENGTH, 0),
                           health_orbit_bubble_center(DISPLAY, offset, 0));
  }
  // Half way round, behind the sidebar, off the display
  cl_assert(health_orbit_bubble_center(DISPLAY, HEALTH_ORBIT_LENGTH / 2, 0).x >= PBL_DISPLAY_WIDTH);
  // Offsets the shorter way round
  cl_assert_equal_i(health_orbit_wrap_offset(HEALTH_ORBIT_STEP), HEALTH_ORBIT_STEP);
  cl_assert_equal_i(health_orbit_wrap_offset(-HEALTH_ORBIT_STEP), -HEALTH_ORBIT_STEP);
  cl_assert_equal_i(health_orbit_wrap_offset(HEALTH_ORBIT_LENGTH - HEALTH_ORBIT_STEP),
                    -HEALTH_ORBIT_STEP);
  cl_assert_equal_i(health_orbit_wrap_offset(2 * HEALTH_ORBIT_STEP - HEALTH_ORBIT_LENGTH),
                    2 * HEALTH_ORBIT_STEP);
}

void test_health_orbit_layout__banner_bubble_is_centered_in_the_banner(void) {
  // Turned up into the banner, the selected bubble is as far from the top of the display as from
  // the banner's edge, across its width
  const GPoint top = health_orbit_bubble_center(DISPLAY, 0, -QUARTER);
  const TimelineListSidebar banner = health_orbit_sidebar(DISPLAY, 0);
  const int16_t r = HEALTH_ORBIT_SELECTED_RADIUS;
  for (int16_t dx = -r; dx <= r; dx += r) {
    const int16_t x = top.x + dx;
    const int16_t bezel_y =
        MID_Y - integer_sqrt((PBL_DISPLAY_WIDTH / 2) * (PBL_DISPLAY_WIDTH / 2) -
                             (x - PBL_DISPLAY_WIDTH / 2) * (x - PBL_DISPLAY_WIDTH / 2));
    const int16_t edge_y = timeline_list_sidebar_depth(&banner, x);
    const int32_t above = top.y - bezel_y;
    const int32_t below = edge_y - top.y;
    cl_assert(PBL_ABS(above - below) <= ((dx == 0) ? 2 : 3));
  }
}

void test_health_orbit_layout__text_runs_inside_the_ring_and_clear_of_the_sidebar(void) {
  const int16_t inner = 113;
  const GRangeHorizontal middle = health_orbit_text_range(DISPLAY, inner, MID_Y, MID_Y);
  cl_assert_equal_i(middle.origin_x, PBL_DISPLAY_WIDTH / 2 - inner);
  const TimelineListSidebar plain = health_orbit_sidebar(DISPLAY, 0);
  cl_assert_equal_i(middle.origin_x + middle.size_w,
                    PBL_DISPLAY_WIDTH - timeline_list_sidebar_depth(&plain, MID_Y));
  // Higher up, the ring narrows it on the left and the sidebar less so on the right
  const GRangeHorizontal high = health_orbit_text_range(DISPLAY, inner, 46, 58);
  cl_assert(high.origin_x > middle.origin_x);
  cl_assert(high.size_w < middle.size_w);
  // Rows outside the ring have no room
  cl_assert_equal_i(health_orbit_text_range(DISPLAY, inner, 0, 10).size_w, 0);
}

//! Column of the sidebar's edge on \a row, or of the display's on its other side
static int32_t prv_sidebar_edge_x(int16_t row) {
  const TimelineListSidebar plain = health_orbit_sidebar(DISPLAY, 0);
  return PBL_DISPLAY_WIDTH - timeline_list_sidebar_depth(&plain, row);
}

void test_health_orbit_layout__middle_arc_is_midway_between_ring_and_sidebar(void) {
  const int16_t inners[] = {100, 113};
  for (size_t i = 0; i < PBL_ARRAY_SIZE(inners); i++) {
    const int16_t inner = inners[i];
    // On the middle row, halfway from the ring's inside to the sidebar's edge
    const int32_t ring_x = PBL_DISPLAY_WIDTH / 2 - inner;
    cl_assert(PBL_ABS(2 * health_orbit_middle_x(DISPLAY, inner, MID_Y) -
                      (ring_x + prv_sidebar_edge_x(MID_Y))) <= 2);
    // And close to halfway on the rows the text is on above and below, where both curve away
    for (int16_t dy = -75; dy <= 75; dy += 15) {
      const int16_t row = MID_Y + dy;
      const int32_t ring_left =
          PBL_DISPLAY_WIDTH / 2 - integer_sqrt((int32_t)inner * inner - (int32_t)dy * dy);
      const int32_t halfway = (ring_left + prv_sidebar_edge_x(row)) / 2;
      cl_assert(PBL_ABS(health_orbit_middle_x(DISPLAY, inner, row) - halfway) <= 2);
    }
    // Mirrored about the sidebar's middle row
    cl_assert_equal_i(health_orbit_middle_x(DISPLAY, inner, MID_Y - 40),
                      health_orbit_middle_x(DISPLAY, inner, MID_Y + 40));
  }
  // Higher up, where the sidebar curves away, it is right of the display's middle
  cl_assert(health_orbit_middle_x(DISPLAY, 113, MID_Y - 78) > PBL_DISPLAY_WIDTH / 2);
}

//! A ring's round end at \a angle keeps the gap from the sidebar, and not much more
static void prv_check_ring_end(int16_t outer, int16_t thickness, int32_t angle) {
  const GPoint cap = prv_polar(outer - thickness / 2, angle);
  const int32_t clearance =
      prv_distance(cap, health_orbit_center(DISPLAY, 0)) - thickness / 2 - HEALTH_ORBIT_RADIUS;
  cl_assert(clearance >= HEALTH_RING_GAP - 1);
  cl_assert(clearance <= HEALTH_RING_GAP + 2);
}

void test_health_orbit_layout__rings_stop_short_of_the_sidebar(void) {
  const int16_t rings[][2] = {{127, 14}, {127, 12}, {112, 12}};
  for (size_t i = 0; i < PBL_ARRAY_SIZE(rings); i++) {
    int32_t start;
    int32_t end;
    health_orbit_ring_angles(DISPLAY, rings[i][0], rings[i][1], &start, &end);
    // Below the sidebar round the left to above it, mirrored about the middle row
    cl_assert(start > QUARTER && start < 2 * QUARTER);
    cl_assert(end > 4 * QUARTER && end < 5 * QUARTER);
    cl_assert(PBL_ABS((start + end) - 6 * QUARTER) <= 2);
    prv_check_ring_end(rings[i][0], rings[i][1], start);
    prv_check_ring_end(rings[i][0], rings[i][1], end);
  }
  // The sidebar curves away from the display's middle, so a ring further in runs further round
  int32_t outer_start, outer_end, inner_start, inner_end;
  health_orbit_ring_angles(DISPLAY, 127, 12, &outer_start, &outer_end);
  health_orbit_ring_angles(DISPLAY, 112, 12, &inner_start, &inner_end);
  cl_assert(inner_start < outer_start);
  cl_assert(inner_end > outer_end);
}

void test_health_orbit_layout__ring_sweep(void) {
  const int32_t start = 100;
  const int32_t end = 1100;
  cl_assert_equal_i(health_orbit_ring_sweep(start, end, 0, 500), 0);
  cl_assert_equal_i(health_orbit_ring_sweep(start, end, 250, 500), 500);
  cl_assert_equal_i(health_orbit_ring_sweep(start, end, 500, 500), 1000);
  cl_assert_equal_i(health_orbit_ring_sweep(start, end, 900, 500), 1000);
  // Nothing to fill toward
  cl_assert_equal_i(health_orbit_ring_sweep(start, end, 900, 0), 0);
}

void test_health_orbit_layout__week_rows_fit_below_the_banner(void) {
  const int16_t banner_bottom = LAYOUT_TOP_BANNER_CENTER_Y_ROUND + HEALTH_ORBIT_RADIUS;
  const int16_t display_radius = PBL_DISPLAY_WIDTH / 2;
  int16_t last_bottom = banner_bottom;
  for (int i = 0; i < 7; i++) {
    MenuRoundShape pill;
    cl_assert(health_orbit_week_row(DISPLAY, i, &pill));
    // Rows are as tall as their pills, one under the other, clear of the banner
    cl_assert_equal_i(pill.box.size.h, HEALTH_WEEK_ROW_HEIGHT);
    cl_assert_equal_i(pill.radius, HEALTH_WEEK_ROW_HEIGHT / 2);
    cl_assert(pill.box.origin.y > last_bottom);
    last_bottom = grect_get_max_y(&pill.box);
    cl_assert_equal_i(pill.box.origin.y + pill.box.size.h / 2, health_orbit_week_row_center_y(i));
    // Centered, their round ends inside the bezel
    cl_assert(PBL_ABS(pill.box.origin.x + grect_get_max_x(&pill.box) - PBL_DISPLAY_WIDTH) <= 1);
    const GPoint end_center =
        GPoint(grect_get_max_x(&pill.box) - pill.radius, pill.box.origin.y + pill.radius);
    const int16_t far_y = PBL_MAX(PBL_ABS(pill.box.origin.y + pill.radius - MID_Y),
                                  PBL_ABS(grect_get_max_y(&pill.box) - pill.radius - MID_Y));
    const int32_t reach =
        prv_distance(GPoint(end_center.x, MID_Y + far_y), GPoint(display_radius, MID_Y));
    cl_assert(reach + pill.radius <= display_radius - MENU_ROUND_PILL_BEZEL_MARGIN + 1);
  }
  cl_assert(last_bottom < PBL_DISPLAY_HEIGHT);
}
