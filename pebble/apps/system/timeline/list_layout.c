/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "list_layout.h"

#include <pbl/util/math.h>
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

int32_t timeline_list_arc_angle(int16_t distance, int16_t length) {
  // A full turn is 2 * pi * distance long, with pi as 355 / 113
  return (int32_t)((int64_t)length * TRIG_MAX_ANGLE * 113 / (2 * 355 * (int64_t)distance));
}

int16_t timeline_list_arc_icon_inset(const TimelineListArc *arc, int16_t distance, int32_t angle,
                                     int16_t *y_out) {
  *y_out = arc->display_radius + (int16_t)(sin_lookup(angle) * distance / TRIG_MAX_RATIO);
  return arc->center_inset + (int16_t)(cos_lookup(angle) * distance / TRIG_MAX_RATIO);
}
