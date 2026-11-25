/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_layout.h
//! Geometry of the round Health app: the orbit sidebar and the section bubbles on it, the rings
//! round the bezel and the week rows. Pure functions of screen coordinates, so they can be tested
//! without drawing.

#include <stdbool.h>
#include <stdint.h>

#include <pbl/services/timeline/notification_layout.h>

#include <applib/graphics/gtypes.h>
#include <applib/ui/menu_layer_round.h>
#include <apps/system/timeline/list_layout.h>

//! The orbit sidebar is the round timeline's sidebar: the notification banner's circle hanging off
//! the right of the display as deep as the banner hangs off the top, with a notch in its edge
#define HEALTH_ORBIT_RADIUS       BANNER_CIRCLE_RADIUS
#define HEALTH_ORBIT_CENTER_INSET LAYOUT_TOP_BANNER_CENTER_Y_ROUND
#define HEALTH_ORBIT_NOTCH_WIDTH  7
#define HEALTH_ORBIT_NOTCH_HEIGHT 16

//! The section bubbles orbit the sidebar's center where the banner has its icon, as the timeline's
//! pin icons do
#define HEALTH_ORBIT_BUBBLE_DISTANCE \
  (LAYOUT_TOP_BANNER_ICON_CENTER_Y_ROUND - HEALTH_ORBIT_CENTER_INSET)
//! Rows between the bubbles of neighbouring sections
#define HEALTH_ORBIT_STEP 42

//! Space kept between the round end of a ring and the sidebar
#define HEALTH_RING_GAP 4

//! The week rows: the first one's top, the rows' pitch and the height of their pills
#define HEALTH_WEEK_ROW_TOP    79
#define HEALTH_WEEK_ROW_PITCH  23
#define HEALTH_WEEK_ROW_HEIGHT 20

//! The sidebar, notched or not, as the round timeline fills it a row at a time on the right of the
//! \a display. Turned a quarter round, the same circle is the banner at the top, filled a column at
//! a time.
TimelineListSidebar health_orbit_sidebar(GSize display, bool notched);

//! Center of the sidebar's circle with the sidebar turned by \a turn about the middle of the
//! \a display, clockwise, a negative quarter turn taking it to the top.
GPoint health_orbit_center(GSize display, int32_t turn);

//! Tip of the sidebar's notch with the sidebar turned by \a turn.
GPoint health_orbit_notch_tip(GSize display, int32_t turn);

//! Turns \a point by \a angle about the middle of the \a display, clockwise, which on an even
//! display is between its middle pixels, so a quarter turn maps the display onto itself.
GPoint health_orbit_turn(GSize display, GPoint point, int32_t angle);

//! Center of a section bubble \a offset rows below the notch along the orbit, as the timeline puts
//! its pin icons on the sidebar's circle by row, with the sidebar turned by \a turn.
GPoint health_orbit_bubble_center(GSize display, int16_t offset, int32_t turn);

//! Where text runs across the rows from \a top to \a bottom inside a ring whose inside is
//! \a inner_radius from the middle of the \a display, clear of the sidebar; none on rows that are
//! not all inside the ring.
GRangeHorizontal health_orbit_text_range(GSize display, int16_t inner_radius, int16_t top,
                                         int16_t bottom);

//! Middle of where text runs across the rows from \a top to \a bottom, see
//! health_orbit_text_range().
int16_t health_orbit_text_center_x(GSize display, int16_t inner_radius, int16_t top,
                                   int16_t bottom);

//! Angles a ring round the bezel, \a outer_radius from the middle of the \a display and
//! \a thickness thick, ends at for its round ends to keep HEALTH_RING_GAP from the sidebar. It runs
//! clockwise from \a start_out, below the sidebar, round the left to \a end_out, above it, past a
//! full turn.
void health_orbit_ring_angles(GSize display, int16_t outer_radius, int16_t thickness,
                              int32_t *start_out, int32_t *end_out);

//! Angle a ring from \a start to \a end sweeps for \a value of \a full, from none to all of it.
int32_t health_orbit_ring_sweep(int32_t start, int32_t end, int32_t value, int32_t full);

//! Center row of week row \a index, counted from the top.
int16_t health_orbit_week_row_center_y(int index);

//! The pill of week row \a index, as a round menu's pill on that row, its ends following the bezel.
//! @return false if the row has no room for a pill
bool health_orbit_week_row(GSize display, int index, MenuRoundShape *pill);
