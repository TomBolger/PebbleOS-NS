/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "sidebar_curve.h"

#include "list_layout.h"

#include <pbl/util/math.h>

int16_t timeline_sidebar_curve_x(const TimelineSidebarCurve *curve, int16_t y) {
  const int32_t distance = PBL_ABS(y - curve->height / 2);
  if (distance >= curve->bend) {
    return curve->wide_x;
  }
  // Smoothstep of how far into the bend the row is: 0 where it starts, 1 on the middle row
  const int64_t t = curve->bend - distance;
  const int64_t bend = curve->bend;
  const int64_t step = t * t * (3 * bend - 2 * t);
  return curve->wide_x + ((curve->narrow_x - curve->wide_x) * step) / (bend * bend * bend);
}

int16_t timeline_sidebar_curve_min_x(const TimelineSidebarCurve *curve, int16_t top,
                                     int16_t bottom) {
  // The edge only moves left away from the middle row, so the ends of the range are the leftmost
  return PBL_MIN(timeline_sidebar_curve_x(curve, top), timeline_sidebar_curve_x(curve, bottom - 1));
}

int16_t timeline_sidebar_curve_center_x(const TimelineSidebarCurve *curve, int16_t y) {
  const int16_t radius = curve->height / 2;
  const int16_t display_edge_x = curve->height - timeline_list_round_edge_inset(radius, y - radius);
  return (timeline_sidebar_curve_x(curve, y) + display_edge_x) / 2;
}
