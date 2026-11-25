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
//! the right of the display as deep as the banner hangs off the top, with the timeline's notch in
//! its edge, as the timeline cuts it at the display's default text size
#define HEALTH_ORBIT_RADIUS       BANNER_CIRCLE_RADIUS
#define HEALTH_ORBIT_CENTER_INSET LAYOUT_TOP_BANNER_CENTER_Y_ROUND
#define HEALTH_ORBIT_NOTCH_WIDTH  10
#define HEALTH_ORBIT_NOTCH_HEIGHT 40

//! Radius of the selected section's bubble, at the notch
#define HEALTH_ORBIT_SELECTED_RADIUS 17
//! Space between the tip of the notch and the selected section's bubble
#define HEALTH_ORBIT_NOTCH_GAP 3

//! The section bubbles orbit the sidebar's center, the selected one the gap clear of the notch
#define HEALTH_ORBIT_BUBBLE_DISTANCE                                         \
  (HEALTH_ORBIT_RADIUS - HEALTH_ORBIT_NOTCH_WIDTH - HEALTH_ORBIT_NOTCH_GAP - \
   HEALTH_ORBIT_SELECTED_RADIUS)
//! Length of the orbit, all the way round, mostly off the display: a full turn, with pi as
//! 355 / 113, rounded
#define HEALTH_ORBIT_LENGTH ((2 * 355 * HEALTH_ORBIT_BUBBLE_DISTANCE + 113 / 2) / 113)
//! Distance along the orbit between the bubbles of neighbouring sections, which at rest are 42
//! rows apart
#define HEALTH_ORBIT_STEP 43

//! Days further along the orbit than this from the one it is on have no bubble, being out of sight
#define HEALTH_ORBIT_DAY_REACH (2 * HEALTH_ORBIT_STEP)
//! How far the orbit turns to swap the sections' bubbles for the days' and back: as little as keeps
//! each set out of sight while the other shows, the days in reach coming a step after the last of
//! the three sections, as if on one string, so they pass the notch as quickly as the sections do
#define HEALTH_ORBIT_SWAP (2 * HEALTH_ORBIT_STEP + HEALTH_ORBIT_STEP + HEALTH_ORBIT_DAY_REACH)

//! Space kept between the round end of a ring and the sidebar
#define HEALTH_RING_GAP 4

//! The week rows: the first one's top, the rows' pitch and the height of their pills
#define HEALTH_WEEK_ROW_TOP    79
#define HEALTH_WEEK_ROW_PITCH  23
#define HEALTH_WEEK_ROW_HEIGHT 20

//! The sidebar with a notch \a notch_width deep, as the round timeline fills it a row at a time on
//! the right of the \a display. Turned a quarter round, the same circle is the banner at the top,
//! filled a column at a time.
TimelineListSidebar health_orbit_sidebar(GSize display, int16_t notch_width);

//! Depth of the notch with the sidebar turned by \a turn: all of it on the right, closing as the
//! sidebar turns to the top, where the banner has none.
int16_t health_orbit_notch_width(int32_t turn);

//! Center of the sidebar's circle with the sidebar turned by \a turn about the middle of the
//! \a display, clockwise, a negative quarter turn taking it to the top.
GPoint health_orbit_center(GSize display, int32_t turn);

//! Tip of the sidebar's notch with the sidebar turned by \a turn, as deep as it is there.
GPoint health_orbit_notch_tip(GSize display, int32_t turn);

//! Turns \a point by \a angle about the middle of the \a display, clockwise, which on an even
//! display is between its middle pixels, so a quarter turn maps the display onto itself.
GPoint health_orbit_turn(GSize display, GPoint point, int32_t angle);

//! Center of a section bubble \a offset along the orbit below the notch, round the sidebar's
//! center as the timeline's pin icons are, with the sidebar turned by \a turn. HEALTH_ORBIT_LENGTH
//! along is all the way round, back where it started.
GPoint health_orbit_bubble_center(GSize display, int16_t offset, int32_t turn);

//! \a offset along the orbit taken the shorter way round, from minus to plus half its length.
int16_t health_orbit_wrap_offset(int16_t offset);

//! Angle round the sidebar's center from its middle row to where its edge meets the edge of the
//! \a display, above and below: as far as a line from its center turns across what shows of it.
int32_t health_orbit_sidebar_reach(GSize display);

//! Where text runs across the rows from \a top to \a bottom inside a ring whose inside is
//! \a inner_radius from the middle of the \a display, clear of the sidebar; none on rows that are
//! not all inside the ring.
GRangeHorizontal health_orbit_text_range(GSize display, int16_t inner_radius, int16_t top,
                                         int16_t bottom);

//! The middle of the usable part of the display, between the inside of a ring \a inner_radius from
//! the middle of the \a display and the sidebar's edge: an arc of the circle centered midway
//! between the middle of the display and the sidebar's center, its radius midway between the ring's
//! inside and the sidebar's. Text and the shapes beside it are centered on it, row by row.
//! @return The arc's column on \a row
int16_t health_orbit_middle_x(GSize display, int16_t inner_radius, int16_t row);

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
