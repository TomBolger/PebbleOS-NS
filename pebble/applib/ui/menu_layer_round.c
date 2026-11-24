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

MenuRoundShape menu_round_bubble(GSize display, int16_t bottom) {
  const int16_t r = MENU_ROUND_BUBBLE_RADIUS;
  return (MenuRoundShape){
    .box = GRect((display.w / 2) - r, bottom - (2 * r), 2 * r, 2 * r),
    .radius = r,
  };
}

int16_t menu_round_bubble_half_width(int16_t bottom, int16_t y) {
  const int32_t radius = MENU_ROUND_BUBBLE_RADIUS;
  const int32_t dy = PBL_ABS(y - (bottom - radius));
  return (dy < radius) ? (int16_t)integer_sqrt((radius * radius) - (dy * dy)) : 0;
}

static int16_t prv_lerp(int16_t from, int16_t to, int32_t progress) {
  return (int16_t)(from + ((to - from) * progress / MENU_ROUND_MORPH_PILL));
}

MenuRoundShape menu_round_morph(const MenuRoundShape *bubble, const MenuRoundShape *pill,
                                int32_t progress) {
  const GRect *from = &bubble->box;
  const GRect *to = &pill->box;
  const int32_t m = PBL_CLAMP(progress, 0, MENU_ROUND_MORPH_PILL);
  const int16_t w = prv_lerp(from->size.w, to->size.w, m);
  const int16_t h = prv_lerp(from->size.h, to->size.h, m);
  const int16_t center_x =
      prv_lerp(from->origin.x + (from->size.w / 2), to->origin.x + (to->size.w / 2), m);
  const int16_t bottom = prv_lerp(grect_get_max_y(from), grect_get_max_y(to), progress);
  return (MenuRoundShape){
    .box = GRect(center_x - (w / 2), bottom - h, w, h),
    .radius = PBL_MIN(w, h) / 2,
  };
}

int16_t menu_round_end_space(int16_t frame_h, int16_t end_h, int16_t next_h) {
  return PBL_MAX((int16_t)((frame_h / 2) - end_h - (next_h / 2)), 0);
}

int16_t menu_round_rest_offset(int16_t frame_h, int16_t content_h, int16_t span_y, int16_t span_h,
                               int16_t top_space, int16_t bottom_space) {
  // The content may have been measured before the selected row took its height
  content_h = PBL_MAX(content_h, (int16_t)(span_y + span_h));
  const int16_t highest = top_space;
  const int16_t lowest = (int16_t)(frame_h - content_h - bottom_space);
  if (lowest >= highest) {
    return (int16_t)((lowest + highest) / 2);
  }
  const int16_t centred = (int16_t)((frame_h / 2) - span_y - (span_h / 2));
  return PBL_CLAMP(centred, lowest, highest);
}
