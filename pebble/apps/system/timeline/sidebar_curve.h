/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

//! The left edge of the round timeline's sidebar, bent toward the selected pin: straight and far
//! in at the top and bottom, it bends out around the middle row, where the bar is narrowest and
//! the selected pin's title reaches it. The neighbours' icons sit in the wide parts, the selected
//! pin's in the narrow middle.

typedef struct TimelineSidebarCurve {
  //! Height of the display, which is as wide as it is tall.
  int16_t height;
  //! Edge of the bar at the top and bottom, where the bar is widest.
  int16_t wide_x;
  //! Edge of the bar on the middle row, where the bar is narrowest.
  int16_t narrow_x;
  //! Distance from the middle row over which the edge bends.
  int16_t bend;
} TimelineSidebarCurve;

//! The edge of the bar on a row. It bends smoothly, with no corner where the bend starts nor in
//! the middle.
//! @param curve The curve.
//! @param y The row.
//! @return The x of the edge.
int16_t timeline_sidebar_curve_x(const TimelineSidebarCurve *curve, int16_t y);

//! The edge of the bar farthest left over a range of rows, where text on those rows has to end.
//! @param curve The curve.
//! @param top The first row.
//! @param bottom The row past the last.
//! @return The smallest x of the edge on those rows.
int16_t timeline_sidebar_curve_min_x(const TimelineSidebarCurve *curve, int16_t top,
                                     int16_t bottom);

//! The middle of the bar on a row, between its edge and the round display's edge, where an icon on
//! that row sits.
//! @param curve The curve.
//! @param y The row.
//! @return The x of the middle of the bar.
int16_t timeline_sidebar_curve_center_x(const TimelineSidebarCurve *curve, int16_t y);
