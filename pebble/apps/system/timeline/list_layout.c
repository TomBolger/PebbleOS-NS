/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "list_layout.h"

#include <pbl/util/math.h>

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

int16_t timeline_list_round_edge_inset(int16_t radius, int16_t distance_y) {
  const int32_t distance = PBL_MIN(PBL_ABS(distance_y), radius);
  return radius - integer_sqrt((int32_t)radius * radius - distance * distance);
}
