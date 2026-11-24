/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "list_layout.h"

#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/trig.h>

void timeline_list_stack(const TimelineListPin *pins, int num_pins, int first_placed,
                         int last_placed, int16_t *tops) {
  for (int i = first_placed - 1; i >= 0; i--) {
    tops[i] = tops[i + 1] - pins[i].height;
  }
  for (int i = last_placed + 1; i < num_pins; i++) {
    tops[i] = tops[i - 1] + pins[i - 1].height;
  }
}

int16_t timeline_list_page_height(const TimelineListGeometry *geometry) {
  return geometry->height;
}

int16_t timeline_list_selected_top(const TimelineListGeometry *geometry,
                                   const TimelineListPin *pins, int num_pins, int selected,
                                   bool *whole_screen_out) {
  const TimelineListPin *pin = &pins[selected];
  const int16_t margin = geometry->margin;
  if (geometry->align == TimelineListAlignAnchor) {
    // Its anchor always rests on the geometry's anchor
    const int16_t top = geometry->anchor_y - pin->anchor_y;
    *whole_screen_out = (top < margin) || (top + pin->height > geometry->height - margin);
    return top;
  }
  *whole_screen_out = (pin->height > geometry->height - 2 * margin);
  if (*whole_screen_out) {
    return geometry->page_top;
  }
  const TimelineListPin *above = (selected > 0) ? &pins[selected - 1] : nullptr;
  const TimelineListPin *below = (selected + 1 < num_pins) ? &pins[selected + 1] : nullptr;
  const TimelineListPin *next = geometry->next_is_down ? below : above;
  // The whole pin is on screen, and the pin after it if there is room, then the pin before it
  int16_t lo = margin;
  int16_t hi = geometry->height - margin - pin->height;
  const TimelineListPin *neighbours[] = {next, (next == below) ? above : below};
  for (size_t i = 0; i < PBL_ARRAY_SIZE(neighbours); i++) {
    const TimelineListPin *neighbour = neighbours[i];
    if (!neighbour) {
      continue;
    }
    int16_t keep_lo = lo;
    int16_t keep_hi = hi;
    if (neighbour == above) {
      keep_lo = PBL_MAX(lo, neighbour->reach_top + neighbour->height + geometry->gap);
    } else {
      keep_hi =
          PBL_MIN(hi, neighbour->reach_bottom - neighbour->height - geometry->gap - pin->height);
    }
    if (keep_lo <= keep_hi) {
      lo = keep_lo;
      hi = keep_hi;
    }
  }
  // The top pin rests at the top
  const int16_t top = margin + geometry->top_space;
  if (above) {
    const int16_t below_above = PBL_CLAMP(above->edge_top + above->height + geometry->gap, lo, hi);
    if (below_above - geometry->gap - above->height >= above->edge_top) {
      return PBL_MAX(below_above, top);
    }
  }
  // The pin above does not fit, so the selected pin is the top one
  return PBL_CLAMP(top, lo, hi);
}

void timeline_list_arrange(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                           int num_pins, int selected, int16_t scroll, int16_t *tops_out,
                           bool *shown_out) {
  const int16_t height = geometry->height;
  bool whole_screen;
  const int16_t sel_top =
      timeline_list_selected_top(geometry, pins, num_pins, selected, &whole_screen) - scroll;
  tops_out[selected] = sel_top;
  shown_out[selected] = true;
  // The pins around the selected one stack beyond the gap from it, those within reach resting out
  // toward their edge, the others pushed off it with the pins past them
  int16_t bottom = sel_top - geometry->gap;
  int first_shown = selected;
  for (int i = selected - 1; i >= 0; i--) {
    const int16_t top = bottom - pins[i].height;
    shown_out[i] = shown_out[i + 1] && (top >= pins[i].reach_top);
    if (shown_out[i]) {
      first_shown = i;
      tops_out[i] = top;
      bottom = top;
    } else {
      bottom = PBL_MIN(bottom, 0);
      tops_out[i] = bottom - pins[i].height;
      bottom = tops_out[i];
    }
  }
  const int16_t push_up = PBL_MAX(tops_out[first_shown] - pins[first_shown].edge_top, 0);
  for (int i = first_shown; i < selected; i++) {
    tops_out[i] -= push_up;
  }
  int16_t top = sel_top + pins[selected].height + geometry->gap;
  int last_shown = selected;
  for (int i = selected + 1; i < num_pins; i++) {
    shown_out[i] = shown_out[i - 1] && (top + pins[i].height <= pins[i].reach_bottom);
    if (shown_out[i]) {
      last_shown = i;
    } else {
      top = PBL_MAX(top, height);
    }
    tops_out[i] = top;
    top += pins[i].height;
  }
  const int16_t push_down = PBL_MAX(
      height - pins[last_shown].edge_bottom - tops_out[last_shown] - pins[last_shown].height, 0);
  for (int i = selected + 1; i <= last_shown; i++) {
    tops_out[i] += push_down;
  }
}

void timeline_list_layout(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                          int num_pins, int selected, int16_t *tops_out) {
  const int16_t margin = geometry->margin;
  switch (geometry->align) {
    case TimelineListAlignAnchor:
      tops_out[selected] = geometry->anchor_y - pins[selected].anchor_y;
      timeline_list_stack(pins, num_pins, selected, selected, tops_out);
      break;
    case TimelineListAlignTop:
      tops_out[0] = margin + geometry->top_space;
      timeline_list_stack(pins, num_pins, 0, 0, tops_out);
      break;
  }
  const int16_t shift = PBL_MAX(margin - tops_out[selected], 0);
  for (int i = 0; i < num_pins; i++) {
    tops_out[i] += shift;
  }
}

int16_t timeline_list_page_bottom(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                                  int num_pins, int selected, int16_t page_top,
                                  int16_t min_height) {
  const int16_t bottom = PBL_MAX(geometry->height - geometry->margin, page_top + min_height);
  if (selected + 1 >= num_pins) {
    return bottom;
  }
  const int16_t above_next = geometry->height - geometry->margin - pins[selected + 1].height;
  return (above_next - page_top >= min_height) ? above_next : bottom;
}

int16_t timeline_list_max_scroll(int16_t content_height, int16_t page_height) {
  if (page_height <= 0 || content_height <= page_height) {
    return 0;
  }
  return ((content_height - 1) / page_height) * page_height;
}

//! Distance from a row to the nearest or the farthest row of a run, 0 to the nearest of a run
//! spanning it
static int16_t prv_rows_distance(int16_t row, int16_t top, int16_t bottom, bool farthest) {
  const int16_t to_top = PBL_ABS(row - top);
  const int16_t to_bottom = PBL_ABS(row - bottom);
  if (farthest) {
    return PBL_MAX(to_top, to_bottom);
  }
  return (row >= top && row <= bottom) ? 0 : PBL_MIN(to_top, to_bottom);
}

//! Half the width of a circle at a distance from its middle row, 0 beyond its radius
static int16_t prv_half_width(int16_t radius, int16_t distance_y) {
  const int32_t distance = PBL_MIN(PBL_ABS(distance_y), radius);
  return integer_sqrt((int32_t)radius * radius - distance * distance);
}

int16_t timeline_list_arc_depth(const TimelineListArc *arc, int16_t top, int16_t bottom) {
  // The circle covers the column of its edge
  const int16_t distance = prv_rows_distance(arc->display_radius, top, bottom, false);
  return arc->center_inset + prv_half_width(arc->radius, distance) + 1;
}

//! Distance from the side opposite the circle to inside the display's edge across a run of rows
static int16_t prv_edge_inset(const TimelineListArc *arc, int16_t top, int16_t bottom) {
  // The edge of the display inset as text flow does it
  const int16_t r = arc->display_radius - arc->text_inset;
  const int16_t distance = prv_rows_distance(arc->display_radius, top, bottom, true);
  return PBL_MAX(arc->text_inset + r - prv_half_width(r, distance), arc->min_inset);
}

int16_t timeline_list_arc_edge_inset(const TimelineListArc *arc, int16_t top, int16_t bottom) {
  // The edge of the display inset as text flow does it
  const int16_t r = arc->display_radius - arc->text_inset;
  const int16_t distance = prv_rows_distance(arc->display_radius, top, bottom, true);
  return PBL_MAX(arc->text_inset + r - prv_half_width(r, distance), arc->min_inset);
}

int16_t timeline_list_arc_text_width(const TimelineListArc *arc, int16_t head_top,
                                     int16_t head_bottom, int16_t top, int16_t bottom) {
  const int16_t edge = timeline_list_arc_edge_inset(arc, head_top, head_bottom);
  const int16_t clear = timeline_list_arc_depth(arc, top, bottom) + arc->gap;
  return 2 * arc->display_radius - PBL_MAX(edge, clear) - edge;
}

int16_t timeline_list_arc_text_inset(const TimelineListArc *arc, int16_t head_top,
                                     int16_t head_bottom, int16_t top, int16_t bottom,
                                     int16_t width) {
  // Clear of the circle where it is deepest
  const int16_t limit =
      2 * arc->display_radius - timeline_list_arc_depth(arc, top, bottom) - arc->gap - width;
  return PBL_MIN(timeline_list_arc_edge_inset(arc, head_top, head_bottom), limit);
}

int16_t timeline_list_arc_text_range(const TimelineListArc *arc, int16_t top, int16_t bottom,
                                     int16_t *end_out) {
  // Always the gap short of the circle, even where the circle is beyond the display's edge, the
  // display cutting off what is past it
  const int16_t clear = timeline_list_arc_depth(arc, top, bottom) + arc->gap;
  *end_out = 2 * arc->display_radius - clear;
  return prv_edge_inset(arc, top, bottom);
}

bool timeline_list_arc_in_reach(const TimelineListArc *arc, int16_t top, int16_t bottom) {
  int16_t end;
  const int16_t start = timeline_list_arc_text_range(arc, top, bottom, &end);
  // The display's edge on the circle's side is as far in as on the other side
  return (end <= 2 * arc->display_radius - start);
}

//! Whether text of a width, aligned the gap short of the circle, shows whole on a row
static bool prv_fits(const TimelineListArc *arc, int16_t row, int16_t width) {
  int16_t end;
  const int16_t start = timeline_list_arc_text_range(arc, row, row, &end);
  return (end - start >= width) && timeline_list_arc_in_reach(arc, row, row);
}

int16_t timeline_list_arc_band(const TimelineListArc *arc, int16_t width, int16_t *bottom_out) {
  const int16_t height = 2 * arc->display_radius;
  int16_t top = 0;
  while (top < arc->display_radius && !prv_fits(arc, top, width)) {
    top++;
  }
  int16_t bottom = 0;
  while (bottom < arc->display_radius && !prv_fits(arc, height - 1 - bottom, width)) {
    bottom++;
  }
  *bottom_out = bottom;
  return top;
}

int16_t timeline_list_arc_drawn_range(const TimelineListArc *arc, int16_t top, int16_t bottom,
                                      int16_t dy, int16_t *end_out) {
  int16_t end;
  const int16_t start = timeline_list_arc_text_range(arc, top, bottom, &end);
  // It moves along the circle, as far from it as where it rests, the display cutting it off
  const int16_t shift = timeline_list_arc_depth(arc, top, bottom) -
                        timeline_list_arc_depth(arc, top + dy, bottom + dy);
  *end_out = end + shift;
  return start + shift;
}

int32_t timeline_list_arc_angle(int16_t distance, int16_t length) {
  // A full turn is 2 * pi * distance long, with pi as 355 / 113
  return (int32_t)((int64_t)length * TRIG_MAX_ANGLE * 113 / (2 * 355 * (int64_t)distance));
}

//! A trigonometric ratio of a length, rounded to the nearest pixel, so that angles a hair apart
//! land on the same pixel
static int16_t prv_trig_scale(int32_t ratio, int16_t length) {
  const int32_t scaled = ratio * length;
  const int32_t half = TRIG_MAX_RATIO / 2;
  return (int16_t)((scaled >= 0) ? ((scaled + half) / TRIG_MAX_RATIO)
                                 : -((-scaled + half) / TRIG_MAX_RATIO));
}

int16_t timeline_list_arc_icon_inset(const TimelineListArc *arc, int16_t distance, int32_t angle,
                                     int16_t *y_out) {
  *y_out = arc->display_radius + prv_trig_scale(sin_lookup(angle), distance);
  return arc->center_inset + prv_trig_scale(cos_lookup(angle), distance);
}

int32_t timeline_list_arc_row_angle(const TimelineListArc *arc, int16_t distance, int16_t row) {
  const int16_t dy = PBL_CLAMP(row - arc->display_radius, -distance, distance);
  const int16_t dx = prv_half_width(distance, dy);
  const int32_t angle = atan2_lookup(dy, dx);
  return (angle > TRIG_MAX_ANGLE / 2) ? (angle - TRIG_MAX_ANGLE) : angle;
}

int32_t timeline_list_arc_max_angle(const TimelineListArc *arc, int16_t distance, int16_t inset) {
  const int32_t reach = arc->display_radius - inset;
  int32_t lo = 0;
  int32_t hi = TRIG_MAX_ANGLE / 4;
  while (lo < hi) {
    const int32_t angle = (lo + hi + 1) / 2;
    int16_t y;
    const int32_t dx = arc->display_radius - timeline_list_arc_icon_inset(arc, distance, angle, &y);
    const int32_t dy = y - arc->display_radius;
    if (dx * dx + dy * dy <= reach * reach) {
      lo = angle;
    } else {
      hi = angle - 1;
    }
  }
  return lo;
}

//! Depth of the sidebar's circle on a row a distance from its middle row
static int16_t prv_circle_depth(const TimelineListSidebar *sidebar, int16_t distance) {
  // The circle covers the column of its edge
  return sidebar->center_inset + prv_half_width(sidebar->radius, distance) + 1;
}

int16_t timeline_list_sidebar_depth(const TimelineListSidebar *sidebar, int16_t row) {
  const int16_t distance = PBL_ABS(row - sidebar->height / 2);
  if (distance > sidebar->radius) {
    return 0;
  }
  const int16_t depth = prv_circle_depth(sidebar, distance);
  const int16_t half_notch = sidebar->notch_height / 2;
  if (distance >= half_notch || sidebar->notch_width <= 0) {
    return depth;
  }
  // The notch's sides run straight from its tip to its base on the circle's edge, so each row
  // steps evenly and no pixel sticks out
  const int16_t tip = prv_circle_depth(sidebar, 0) - sidebar->notch_width;
  const int16_t base = prv_circle_depth(sidebar, half_notch);
  return PBL_MIN(depth, tip + (base - tip) * distance / half_notch);
}
