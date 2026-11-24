/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include <pbl/util/math.h>
#include <pbl/util/trig.h>

#include <apps/system/timeline/list_layout.h>

#define HEIGHT 228
#define MARGIN 4
#define EDGE   2
#define GAP    10

static const TimelineListGeometry s_centered = {
  .height = HEIGHT,
  .anchor_y = HEIGHT / 2,
  .margin = MARGIN,
  .gap = GAP,
  .align = TimelineListAlignAnchor,
  .next_is_down = true,
};

static const TimelineListGeometry s_top = {
  .height = HEIGHT,
  .margin = MARGIN,
  .gap = GAP,
  .align = TimelineListAlignTop,
  .next_is_down = true,
};

//! Pins anchored at their middle that show whole, as on rect
static TimelineListPin prv_pin(int16_t height) {
  return (TimelineListPin){
    .height = height,
    .anchor_y = height / 2,
    .edge_top = EDGE,
    .edge_bottom = EDGE,
    .reach_top = EDGE,
    .reach_bottom = HEIGHT - EDGE,
  };
}

//! Pins anchored at their middle that show as long as their anchor is on screen, as on round
static TimelineListPin prv_cut_pin(int16_t height) {
  TimelineListPin pin = prv_pin(height);
  pin.reach_top = -pin.anchor_y;
  pin.reach_bottom = HEIGHT - 1 - pin.anchor_y + height;
  return pin;
}

static void prv_assert_stacked(const TimelineListPin *pins, int num_pins, const int16_t *tops) {
  for (int i = 1; i < num_pins; i++) {
    cl_assert_equal_i(tops[i], tops[i - 1] + pins[i - 1].height);
  }
}

//! The pins around the selected one rest out at the edges, at least the gap from it
static void prv_assert_out_at_edges(const TimelineListPin *pins, const int16_t *tops) {
  cl_assert_equal_i(tops[0], EDGE);
  cl_assert(tops[0] + pins[0].height + GAP <= tops[1]);
  cl_assert_equal_i(tops[2] + pins[2].height, HEIGHT - EDGE);
  cl_assert(tops[1] + pins[1].height + GAP <= tops[2]);
}

static void prv_arrange(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                        int num_pins, int selected, int16_t *tops, bool *shown) {
  timeline_list_arrange(geometry, pins, num_pins, selected, 0 /* scroll */, tops, shown);
}

void test_timeline_list_layout__selected_rests_on_anchor(void) {
  const TimelineListPin pins[] = {prv_cut_pin(30), prv_pin(60), prv_cut_pin(30)};
  int16_t tops[3];
  bool shown[3];
  prv_arrange(&s_centered, pins, 3, 1, tops, shown);
  cl_assert_equal_i(tops[1], HEIGHT / 2 - 30);
  prv_assert_out_at_edges(pins, tops);
  cl_assert(shown[0] && shown[1] && shown[2]);
}

void test_timeline_list_layout__anchor_inside_pin(void) {
  TimelineListPin pin = prv_pin(70);
  pin.anchor_y = 25;
  const TimelineListPin pins[] = {prv_cut_pin(40), pin, prv_cut_pin(40)};
  int16_t tops[3];
  bool shown[3];
  prv_arrange(&s_centered, pins, 3, 1, tops, shown);
  cl_assert_equal_i(tops[1] + 25, HEIGHT / 2);
  prv_assert_out_at_edges(pins, tops);
}

void test_timeline_list_layout__top(void) {
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(60), prv_pin(30)};
  int16_t tops[3];
  bool shown[3];
  // The top pin rests at the edge, the selected pin the gap below it
  prv_arrange(&s_top, pins, 3, 1, tops, shown);
  cl_assert_equal_i(tops[1], EDGE + 30 + GAP);
  prv_assert_out_at_edges(pins, tops);
  // With nothing above, the selected pin rests on the margin
  prv_arrange(&s_top, pins + 1, 2, 0, tops, shown);
  cl_assert_equal_i(tops[0], MARGIN);
  // Space can be kept above it
  TimelineListGeometry geometry = s_top;
  geometry.top_space = 20;
  prv_arrange(&geometry, pins + 1, 2, 0, tops, shown);
  cl_assert_equal_i(tops[0], MARGIN + 20);
}

void test_timeline_list_layout__neighbours_cut_by_the_display(void) {
  // Pins around the selected one show as long as their anchor is on screen
  const TimelineListPin pins[] = {prv_cut_pin(60), prv_pin(100), prv_cut_pin(60)};
  int16_t tops[3];
  bool shown[3];
  prv_arrange(&s_centered, pins, 3, 1, tops, shown);
  cl_assert(shown[0] && shown[2]);
  cl_assert(tops[0] < 0);
  cl_assert(tops[0] + 30 >= 0);
  cl_assert(tops[2] + 60 > HEIGHT);
  cl_assert(tops[2] + 30 < HEIGHT);
}

void test_timeline_list_layout__tall_details_push_neighbours_off(void) {
  // The selected pin stays on the anchor; the pin its details reach over is pushed off
  TimelineListPin pin = prv_pin(110);
  pin.anchor_y = 20;
  const TimelineListPin pins[] = {prv_cut_pin(50), pin, prv_cut_pin(50)};
  int16_t tops[3];
  bool shown[3];
  prv_arrange(&s_centered, pins, 3, 1, tops, shown);
  cl_assert_equal_i(tops[1] + 20, HEIGHT / 2);
  cl_assert(shown[0]);
  cl_assert(!shown[2]);
  cl_assert(tops[2] >= HEIGHT);
  // Alike in the past, where the pin below is the one after it
  TimelineListGeometry past = s_centered;
  past.next_is_down = false;
  prv_arrange(&past, pins, 3, 1, tops, shown);
  cl_assert_equal_i(tops[1] + 20, HEIGHT / 2);
  cl_assert(shown[0] && !shown[2]);
}

void test_timeline_list_layout__top_room_pushes_previous_off(void) {
  // Aligned to the top, the selected pin and the next fit, but not with the previous one
  const TimelineListPin pins[] = {prv_pin(50), prv_pin(140), prv_pin(50)};
  int16_t tops[3];
  bool shown[3];
  prv_arrange(&s_top, pins, 3, 1, tops, shown);
  cl_assert(!shown[0]);
  cl_assert(tops[0] + 50 <= 0);
  cl_assert_equal_i(tops[1], MARGIN);
  cl_assert(shown[2]);
  cl_assert_equal_i(tops[2] + 50, HEIGHT - EDGE);
}

void test_timeline_list_layout__pushed_off_with_those_past_it(void) {
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(30), prv_pin(200), prv_pin(30)};
  int16_t tops[4];
  bool shown[4];
  prv_arrange(&s_top, pins, 4, 2, tops, shown);
  cl_assert(!shown[0] && !shown[1]);
  cl_assert(tops[1] + 30 <= 0);
  cl_assert_equal_i(tops[0] + 30, tops[1]);
}

void test_timeline_list_layout__neighbours_stay_together(void) {
  // Pins past a neighbour that fit stack against it, the run resting out at the edge
  const TimelineListPin pins[] = {
    prv_cut_pin(20), prv_cut_pin(20), prv_pin(40), prv_cut_pin(20), prv_cut_pin(20)
  };
  int16_t tops[5];
  bool shown[5];
  prv_arrange(&s_centered, pins, 5, 2, tops, shown);
  cl_assert(shown[0] && shown[1] && shown[3] && shown[4]);
  cl_assert_equal_i(tops[0], EDGE);
  prv_assert_stacked(pins, 2, tops);
  cl_assert_equal_i(tops[4] + 20, HEIGHT - EDGE);
  prv_assert_stacked(pins + 3, 2, tops + 3);
}

void test_timeline_list_layout__whole_screen_anchored(void) {
  // A selected pin that does not fit below its anchor takes the screen from there, page after page
  TimelineListPin pin = prv_pin(200);
  pin.anchor_y = 20;
  const TimelineListPin pins[] = {prv_cut_pin(30), pin, prv_cut_pin(30)};
  bool whole;
  const int16_t top = timeline_list_selected_top(&s_centered, pins, 3, 1, &whole);
  cl_assert(whole);
  cl_assert_equal_i(top + 20, HEIGHT / 2);
  const int16_t page = timeline_list_page_height(&s_centered);
  cl_assert_equal_i(page, HEIGHT);
  const int16_t max_scroll = timeline_list_max_scroll(top + 200, page);
  cl_assert_equal_i(max_scroll, HEIGHT);
  // The next pin comes in at its end
  int16_t tops[3];
  bool shown[3];
  timeline_list_arrange(&s_centered, pins, 3, 1, max_scroll, tops, shown);
  cl_assert_equal_i(tops[1], top - max_scroll);
  cl_assert(shown[2]);
  cl_assert(!shown[0]);
}

void test_timeline_list_layout__whole_screen_top(void) {
  TimelineListGeometry geometry = s_top;
  geometry.page_top = -4;
  const TimelineListPin pins[] = {prv_pin(30), prv_pin(2 * HEIGHT - 100), prv_pin(30)};
  bool whole;
  cl_assert_equal_i(timeline_list_selected_top(&geometry, pins, 3, 1, &whole), -4);
  cl_assert(whole);
  int16_t tops[3];
  bool shown[3];
  prv_arrange(&geometry, pins, 3, 1, tops, shown);
  cl_assert(!shown[0] && !shown[2]);
  const int16_t max_scroll = timeline_list_max_scroll(-4 + pins[1].height, HEIGHT);
  timeline_list_arrange(&geometry, pins, 3, 1, max_scroll, tops, shown);
  cl_assert_equal_i(tops[1], -4 - max_scroll);
  cl_assert(shown[2]);
  // A pin that fits on the screen but not between the margins takes the whole screen too
  const TimelineListPin fits[] = {prv_pin(30), prv_pin(HEIGHT - MARGIN), prv_pin(30)};
  cl_assert_equal_i(timeline_list_selected_top(&geometry, fits, 3, 1, &whole), -4);
  cl_assert(whole);
}

void test_timeline_list_layout__max_scroll(void) {
  // Text that fits on a page does not scroll
  cl_assert_equal_i(timeline_list_max_scroll(100, 120), 0);
  cl_assert_equal_i(timeline_list_max_scroll(120, 120), 0);
  // One line past the page is a whole page more, so that pages line up with the page frame
  cl_assert_equal_i(timeline_list_max_scroll(121, 120), 120);
  cl_assert_equal_i(timeline_list_max_scroll(240, 120), 120);
  cl_assert_equal_i(timeline_list_max_scroll(241, 120), 240);
}

void test_timeline_list_layout__stack_around_run(void) {
  const TimelineListPin pins[] = {prv_pin(10), prv_pin(20), prv_pin(30), prv_pin(40), prv_pin(50)};
  int16_t tops[5] = {0, 0, 100, 130, 0};
  timeline_list_stack(pins, 5, 2, 3, tops);
  cl_assert_equal_i(tops[1], 80);
  cl_assert_equal_i(tops[0], 70);
  cl_assert_equal_i(tops[4], 170);
}

//! A round display 260 wide with a circle of radius 140 reaching 60 into it from the right, as
//! the notification banner hangs off the top
static const TimelineListArc s_arc = {
  .display_radius = 130,
  .text_inset = 8,
  .min_inset = 13,
  .center_inset = -81,
  .radius = 140,
  .gap = 13,
};

void test_timeline_list_layout__arc_depth(void) {
  // Deepest on rows spanning the middle, or touching it
  cl_assert_equal_i(timeline_list_arc_depth(&s_arc, 100, 160), 60);
  cl_assert_equal_i(timeline_list_arc_depth(&s_arc, 130, 130), 60);
  cl_assert_equal_i(timeline_list_arc_depth(&s_arc, 0, 130), 60);
  // Shallower away from the middle, alike above and below: 60 rows away, the circle is
  // sqrt(140^2 - 60^2) = 126 wide
  cl_assert_equal_i(timeline_list_arc_depth(&s_arc, 10, 70), 46);
  cl_assert_equal_i(timeline_list_arc_depth(&s_arc, 190, 250), 46);
  // Rows the circle misses are not reached
  cl_assert(timeline_list_arc_depth(&s_arc, -40, -20) <= 0);
  cl_assert(timeline_list_arc_depth(&s_arc, 300, 320) <= 0);
}

void test_timeline_list_layout__arc_text_range(void) {
  int16_t end;
  // Beside the circle's middle, text runs from the minimum inset to the gap before the circle
  cl_assert_equal_i(timeline_list_arc_text_range(&s_arc, 110, 140, &end), 13);
  cl_assert_equal_i(end, 260 - 60 - 13);
  // Away from the middle, it starts inside the display where the rows are narrowest: 90 rows up,
  // the display inset by 8 is sqrt(122^2 - 90^2) = 82 wide, so its edge is 8 + 122 - 82 in
  cl_assert_equal_i(timeline_list_arc_text_range(&s_arc, 40, 60, &end), 48);
  cl_assert_equal_i(timeline_list_arc_text_range(&s_arc, 200, 220, &end), 48);
  // Above the circle it ends inside the display's edge on the other side
  cl_assert_equal_i(timeline_list_arc_text_range(&s_arc, 20, 40, &end), 78);
  cl_assert_equal_i(end, 260 - 78);
  // Where the circle reaches, text ends the gap before it: 60 rows away, the circle is 46 deep
  timeline_list_arc_text_range(&s_arc, 60, 70, &end);
  cl_assert_equal_i(end, 260 - 46 - 13);
}

void test_timeline_list_layout__arc_drawn_range(void) {
  int16_t end;
  int16_t rest_end;
  // Drawn where it is laid out, text runs as it is laid out
  const int16_t rest_start = timeline_list_arc_text_range(&s_arc, 60, 70, &rest_end);
  cl_assert_equal_i(timeline_list_arc_drawn_range(&s_arc, 60, 70, 0, &end), rest_start);
  cl_assert_equal_i(end, rest_end);
  // Drawn anywhere else, it keeps its width and its distance from the circle, past the display's
  // edge too, so it never drifts toward the middle as it leaves
  const int16_t rest_depth = timeline_list_arc_depth(&s_arc, 60, 70);
  for (int16_t dy = -120; dy <= 120; dy += 3) {
    const int16_t start = timeline_list_arc_drawn_range(&s_arc, 60, 70, dy, &end);
    cl_assert_equal_i(end - start, rest_end - rest_start);
    cl_assert_equal_i(end + timeline_list_arc_depth(&s_arc, 60 + dy, 70 + dy),
                      rest_end + rest_depth);
  }
  // Text resting on the middle rows leaves the top further out than the display's edge there,
  // moving out as the circle does
  int16_t middle_end;
  const int16_t middle_start = timeline_list_arc_text_range(&s_arc, 120, 140, &middle_end);
  const int16_t start = timeline_list_arc_drawn_range(&s_arc, 120, 140, -125, &end);
  int16_t edge_end;
  timeline_list_arc_text_range(&s_arc, 0, 15, &edge_end);
  cl_assert(end > edge_end);
  cl_assert(end > middle_end);
  cl_assert_equal_i(end - start, middle_end - middle_start);
}

void test_timeline_list_layout__arc_angle(void) {
  // A quarter of a circle of radius 100 is 157 long
  cl_assert(PBL_ABS(timeline_list_arc_angle(100, 157) - TRIG_MAX_ANGLE / 4) < 20);
  cl_assert_equal_i(timeline_list_arc_angle(100, 0), 0);
  cl_assert(timeline_list_arc_angle(100, -50) == -timeline_list_arc_angle(100, 50));
}

void test_timeline_list_layout__arc_icon(void) {
  int16_t y;
  // On the circle's middle row, the icon is its distance in from the circle's center
  cl_assert_equal_i(timeline_list_arc_icon_inset(&s_arc, 119, 0, &y), 38);
  cl_assert_equal_i(y, 130);
  // It turns about the center, down for a positive angle, alike both ways
  int16_t y_up;
  const int16_t up = timeline_list_arc_icon_inset(&s_arc, 119, -TRIG_MAX_ANGLE / 12, &y_up);
  const int16_t down = timeline_list_arc_icon_inset(&s_arc, 119, TRIG_MAX_ANGLE / 12, &y);
  cl_assert_equal_i(up, down);
  cl_assert_equal_i(y - 130, 130 - y_up);
  // 30 degrees: half the distance down, cos 30 of it in
  cl_assert(PBL_ABS(y - (130 + 119 / 2)) <= 1);
  cl_assert(PBL_ABS(up - (-81 + 103)) <= 1);
  // A quarter turn puts it the distance below the center, behind the side of the display
  cl_assert_equal_i(timeline_list_arc_icon_inset(&s_arc, 119, TRIG_MAX_ANGLE / 4, &y), -81);
  cl_assert_equal_i(y, 130 + 119);
}

void test_timeline_list_layout__arc_row_angle(void) {
  int16_t y;
  // The angle turns the icon onto the row
  for (int16_t row = 40; row <= 220; row += 15) {
    const int32_t angle = timeline_list_arc_row_angle(&s_arc, 119, row);
    timeline_list_arc_icon_inset(&s_arc, 119, angle, &y);
    cl_assert(PBL_ABS(y - row) <= 1);
  }
  cl_assert_equal_i(timeline_list_arc_row_angle(&s_arc, 119, 130), 0);
  cl_assert(timeline_list_arc_row_angle(&s_arc, 119, 100) < 0);
  // Rows out of reach are a quarter turn away
  cl_assert_equal_i(timeline_list_arc_row_angle(&s_arc, 119, 400), TRIG_MAX_ANGLE / 4);
}

void test_timeline_list_layout__arc_max_angle(void) {
  const int32_t max = timeline_list_arc_max_angle(&s_arc, 119, 4);
  int16_t y;
  // At the largest angle the icon's center is the inset inside the display's edge, and a little
  // further it is not
  for (int sign = -1; sign <= 1; sign += 2) {
    int16_t inset = timeline_list_arc_icon_inset(&s_arc, 119, sign * max, &y);
    int32_t dx = 130 - inset;
    int32_t dy = y - 130;
    cl_assert(dx * dx + dy * dy <= 126 * 126);
    inset = timeline_list_arc_icon_inset(&s_arc, 119, sign * (max + TRIG_MAX_ANGLE / 90), &y);
    dx = 130 - inset;
    dy = y - 130;
    cl_assert(dx * dx + dy * dy > 126 * 126);
  }
  // A negative inset turns the icon's center past the display's edge, wholly out of view
  const int32_t out = timeline_list_arc_max_angle(&s_arc, 119, -19);
  cl_assert(out > max && out < TRIG_MAX_ANGLE / 4);
  const int16_t inset = timeline_list_arc_icon_inset(&s_arc, 119, out, &y);
  const int32_t dx = 130 - inset;
  const int32_t dy = y - 130;
  cl_assert(dx * dx + dy * dy <= 149 * 149);
  cl_assert(dx * dx + dy * dy > 148 * 148);
}

void test_timeline_list_layout__arc_band(void) {
  int16_t bottom;
  const int16_t top = timeline_list_arc_band(&s_arc, 110, &bottom);
  // Text of the width fits from the first row of the band to the last, and not past them
  int16_t end;
  int16_t start = timeline_list_arc_text_range(&s_arc, top, top, &end);
  cl_assert(end - start >= 110);
  start = timeline_list_arc_text_range(&s_arc, top - 1, top - 1, &end);
  cl_assert(end - start < 110);
  start = timeline_list_arc_text_range(&s_arc, 259 - bottom, 259 - bottom, &end);
  cl_assert(end - start >= 110);
  start = timeline_list_arc_text_range(&s_arc, 260 - bottom, 260 - bottom, &end);
  cl_assert(end - start < 110);
  // Wider text needs rows nearer the middle
  int16_t wide_bottom;
  cl_assert(timeline_list_arc_band(&s_arc, 150, &wide_bottom) > top);
  cl_assert(wide_bottom > bottom);
}

void test_timeline_list_layout__sidebar_depth(void) {
  // The circle of the arc above, its notch 7 deep and 28 tall in the middle row
  const TimelineListSidebar sidebar = {
    .height = 260,
    .center_inset = -81,
    .radius = 140,
    .notch_width = 7,
    .notch_height = 28,
  };
  TimelineListSidebar plain = sidebar;
  plain.notch_width = 0;
  // Without a notch, it reaches as deep as the circle on every row
  for (int16_t row = 0; row < 260; row++) {
    TimelineListArc arc = s_arc;
    cl_assert_equal_i(timeline_list_sidebar_depth(&plain, row),
                      timeline_list_arc_depth(&arc, row, row));
  }
  // The notch cuts its full depth at its tip, less toward its base, and nothing beyond it
  cl_assert_equal_i(timeline_list_sidebar_depth(&sidebar, 130),
                    timeline_list_sidebar_depth(&plain, 130) - 7);
  int16_t last_cut = 7;
  for (int16_t d = 1; d < 14; d++) {
    const int16_t cut = timeline_list_sidebar_depth(&plain, 130 + d) -
                        timeline_list_sidebar_depth(&sidebar, 130 + d);
    cl_assert(cut >= 0 && cut <= last_cut);
    last_cut = cut;
    // Its sides open steadily from the tip, a row never reaching less deep than the one before
    cl_assert(timeline_list_sidebar_depth(&sidebar, 130 + d) >=
              timeline_list_sidebar_depth(&sidebar, 130 + d - 1));
    // Alike above and below, so the notch points straight at the middle row
    cl_assert_equal_i(timeline_list_sidebar_depth(&sidebar, 130 - d),
                      timeline_list_sidebar_depth(&sidebar, 130 + d));
  }
  cl_assert_equal_i(timeline_list_sidebar_depth(&sidebar, 130 + 14),
                    timeline_list_sidebar_depth(&plain, 130 + 14));
}
