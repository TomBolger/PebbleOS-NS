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

static void prv_shift(int16_t *tops, int num_pins, int16_t delta) {
  for (int i = 0; i < num_pins; i++) {
    tops[i] += delta;
  }
}

void timeline_list_layout(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                          int num_pins, int selected, int16_t *tops_out) {
  const int16_t margin = geometry->margin;
  tops_out[selected] = PBL_MAX(geometry->anchor_y - pins[selected].anchor_y, margin);
  timeline_list_stack(pins, num_pins, selected, selected, tops_out);

  const int last = num_pins - 1;
  if (geometry->fill_bottom) {
    const int16_t gap = geometry->height - margin - (tops_out[last] + pins[last].height);
    if (gap > 0) {
      prv_shift(tops_out, num_pins, gap);
    }
  }
  if (geometry->fill_top) {
    // The selected pin is at or below the top pin, so it stays below the margin
    const int16_t gap = tops_out[0] - margin;
    if (gap > 0) {
      prv_shift(tops_out, num_pins, -gap);
    }
  }
}

int16_t timeline_list_round_edge_inset(int16_t radius, int16_t distance_y) {
  const int32_t distance = PBL_MIN(PBL_ABS(distance_y), radius);
  return radius - integer_sqrt((int32_t)radius * radius - distance * distance);
}
