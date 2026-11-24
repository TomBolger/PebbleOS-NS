/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "notifications_round_path.h"

#include <pbl/util/math.h>

// Past the join the path is a hyperbola, x = x0 + SLOPE * (sqrt(B^2 + (t + T1)^2) -
// sqrt(B^2 + T1^2)) with t = |dy| - JOIN. B and T1 give it the rim's slope and curvature at the
// join, and its slope rises from there towards SLOPE. Lengths are fractions of the radius and
// SLOPE a ratio, all in 1/1024ths. They follow from a join 35 degrees round from the middle, where
// the rim's slope is 0.7, and a final slope of 0.9: rows leave mostly up or down.
#define Q          1024
#define PATH_JOIN  587
#define PATH_B     126
#define PATH_T1    156
#define PATH_SLOPE 922

int16_t notifications_round_path_x(int16_t radius, int16_t dy) {
  const int32_t d = (dy < 0) ? -dy : dy;
  const int32_t join = radius * PATH_JOIN / Q;
  if (d <= join) {
    return radius - integer_sqrt((int64_t)radius * radius - (int64_t)d * d);
  }
  const int64_t x0 = radius - integer_sqrt((int64_t)radius * radius - (int64_t)join * join);
  // In 1/1024 px.
  const int64_t b = (int64_t)radius * PATH_B;
  const int64_t t1 = (int64_t)radius * PATH_T1;
  const int64_t t = (int64_t)(d - join) * Q;
  const int64_t rise = integer_sqrt(b * b + (t + t1) * (t + t1)) - integer_sqrt(b * b + t1 * t1);
  return x0 + (rise * PATH_SLOPE / Q + Q / 2) / Q;
}
