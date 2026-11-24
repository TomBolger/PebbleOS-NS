/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "menu_layer_round.h"

#include <pbl/util/math.h>

//! How far the centre of either end of a pill with ends of \a radius, \a dy from the middle of
//! the display, can be from the middle while keeping MENU_ROUND_PILL_BEZEL_MARGIN plus \a margin
//! clear of the bezel.
static int32_t prv_pill_half_span(GSize display, int32_t dy, int32_t radius, int32_t margin) {
  const int32_t reach =
      (PBL_MIN(display.w, display.h) / 2) - MENU_ROUND_PILL_BEZEL_MARGIN - margin - radius;
  return (reach > dy) ? integer_sqrt(((int64_t)reach * reach) - ((int64_t)dy * dy)) : 0;
}

bool menu_round_pill(GSize display, GRect bounds, GRect frame, int16_t rest_h, int16_t left_margin,
                     MenuRoundShape *pill) {
  const int16_t height = frame.size.h - (2 * MENU_ROUND_PILL_INSET_Y);
  if (height <= 0) {
    return false;
  }
  const int16_t top = frame.origin.y + MENU_ROUND_PILL_INSET_Y;
  const int16_t radius = PBL_MAX(PBL_MIN(height, rest_h - (2 * MENU_ROUND_PILL_INSET_Y)) / 2, 1);

  const int32_t middle_y = display.h / 2;
  const int32_t dy =
      PBL_MAX(PBL_ABS(top + radius - middle_y), PBL_ABS(top + height - radius - middle_y));
  const int32_t middle_x = display.w / 2;
  const int32_t left = PBL_MAX(
      middle_x - prv_pill_half_span(display, dy, radius, left_margin) - radius, bounds.origin.x);
  const int32_t right = PBL_MIN(middle_x + prv_pill_half_span(display, dy, radius, 0) + radius,
                                grect_get_max_x(&bounds));
  if (right <= left) {
    return false;
  }
  *pill = (MenuRoundShape){
    .box = GRect(left, top, right - left, height),
    .radius = PBL_MIN(radius, (right - left) / 2),
  };
  return true;
}
