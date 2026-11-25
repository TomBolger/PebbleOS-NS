/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_layout.h"

#include <pbl/util/math.h>
#include <pbl/util/trig.h>

//! The sidebar's circle as a TimelineListArc, with text keeping inside a ring whose inside is
//! \a inner_radius from the middle of the display
static TimelineListArc prv_arc(GSize display, int16_t inner_radius) {
  return (TimelineListArc){
    .display_radius = display.w / 2,
    .text_inset = display.w / 2 - inner_radius,
    .center_inset = HEALTH_ORBIT_CENTER_INSET,
    .radius = HEALTH_ORBIT_RADIUS,
  };
}

//! \a num / \a den rounded to the nearest integer
static int32_t prv_round_div(int64_t num, int64_t den) {
  return (int32_t)((num >= 0) ? ((num + den / 2) / den) : -((-num + den / 2) / den));
}

TimelineListSidebar health_orbit_sidebar(GSize display, bool notched) {
  return (TimelineListSidebar){
    .height = display.h,
    .center_inset = HEALTH_ORBIT_CENTER_INSET,
    .radius = HEALTH_ORBIT_RADIUS,
    .notch_width = notched ? HEALTH_ORBIT_NOTCH_WIDTH : 0,
    .notch_height = HEALTH_ORBIT_NOTCH_HEIGHT,
  };
}

GPoint health_orbit_turn(GSize display, GPoint point, int32_t angle) {
  // In half pixels, about the middle of the display
  const int32_t mid_x = display.w - 1;
  const int32_t mid_y = display.h - 1;
  const int64_t dx = 2 * point.x - mid_x;
  const int64_t dy = 2 * point.y - mid_y;
  const int64_t cos = cos_lookup(angle);
  const int64_t sin = sin_lookup(angle);
  const int32_t x = prv_round_div(dx * cos - dy * sin, TRIG_MAX_RATIO) + mid_x;
  const int32_t y = prv_round_div(dx * sin + dy * cos, TRIG_MAX_RATIO) + mid_y;
  return GPoint(prv_round_div(x, 2), prv_round_div(y, 2));
}

GPoint health_orbit_center(GSize display, int32_t turn) {
  // As the timeline's sidebar on the right, its edge in the column the circle covers last
  const GPoint center = GPoint(display.w - 1 - HEALTH_ORBIT_CENTER_INSET, display.h / 2);
  return health_orbit_turn(display, center, turn);
}

GPoint health_orbit_notch_tip(GSize display, int32_t turn) {
  const TimelineListSidebar sidebar = health_orbit_sidebar(display, true);
  const int16_t depth = timeline_list_sidebar_depth(&sidebar, display.h / 2);
  return health_orbit_turn(display, GPoint(display.w - depth, display.h / 2), turn);
}

GPoint health_orbit_bubble_center(GSize display, int16_t offset, int32_t turn) {
  const TimelineListArc arc = prv_arc(display, 0);
  const int16_t distance = HEALTH_ORBIT_BUBBLE_DISTANCE;
  const int32_t angle = timeline_list_arc_row_angle(&arc, distance, display.h / 2 + offset);
  int16_t y;
  const int16_t inset = timeline_list_arc_icon_inset(&arc, distance, angle, &y);
  return health_orbit_turn(display, GPoint(display.w - 1 - inset, y), turn);
}

GRangeHorizontal health_orbit_text_range(GSize display, int16_t inner_radius, int16_t top,
                                         int16_t bottom) {
  const TimelineListArc arc = prv_arc(display, inner_radius);
  int16_t end;
  const int16_t start = timeline_list_arc_text_range(&arc, top, bottom, &end);
  const int16_t middle = display.h / 2;
  const bool inside = (PBL_MAX(middle - top, bottom - middle) < inner_radius);
  if (!inside || end <= start) {
    return (GRangeHorizontal){.origin_x = start};
  }
  return (GRangeHorizontal){.origin_x = start, .size_w = end - start};
}

int16_t health_orbit_text_center_x(GSize display, int16_t inner_radius, int16_t top,
                                   int16_t bottom) {
  const GRangeHorizontal range = health_orbit_text_range(display, inner_radius, top, bottom);
  return range.origin_x + range.size_w / 2;
}

void health_orbit_ring_angles(GSize display, int16_t outer_radius, int16_t thickness,
                              int32_t *start_out, int32_t *end_out) {
  // The middle of the band meets a circle about the sidebar's center that keeps the gap, half
  // the band out from the sidebar. In sixteenths of a pixel, with the distance between the
  // centers doubled as the middle of an even display is between pixels.
  const int64_t r = outer_radius - thickness / 2;
  const int64_t clear = HEALTH_ORBIT_RADIUS + thickness / 2 + HEALTH_RING_GAP;
  const int64_t distance2 = display.w - 1 - 2 * HEALTH_ORBIT_CENTER_INSET;
  // Distance from the middle of the display toward the sidebar, and up, to where they meet
  const int64_t across = 4 * (4 * r * r - 4 * clear * clear + distance2 * distance2) / distance2;
  const int64_t r16 = 16 * r;
  const int64_t up = (across < r16) ? integer_sqrt(r16 * r16 - across * across) : 0;
  // Clockwise from the top of the display
  const int32_t top = atan2_lookup((int16_t)across, (int16_t)up);
  *start_out = TRIG_MAX_ANGLE / 2 - top;
  *end_out = TRIG_MAX_ANGLE + top;
}

int32_t health_orbit_ring_sweep(int32_t start, int32_t end, int32_t value, int32_t full) {
  if (full <= 0 || value <= 0) {
    return 0;
  }
  const int64_t span = end - start;
  return (int32_t)((span * PBL_MIN(value, full)) / full);
}

int16_t health_orbit_week_row_center_y(int index) {
  return HEALTH_WEEK_ROW_TOP + index * HEALTH_WEEK_ROW_PITCH + HEALTH_WEEK_ROW_PITCH / 2;
}

bool health_orbit_week_row(GSize display, int index, MenuRoundShape *pill) {
  // The frame the round menu insets its pill from
  const int16_t frame_h = HEALTH_WEEK_ROW_HEIGHT + 2 * MENU_ROUND_PILL_INSET_Y;
  const GRect frame =
      GRect(0, health_orbit_week_row_center_y(index) - frame_h / 2, display.w, frame_h);
  return menu_round_pill(display, GRect(0, 0, display.w, display.h), frame, frame_h, 0, pill);
}
