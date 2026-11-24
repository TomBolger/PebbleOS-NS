/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! Vertical placement of the pins of the compact timeline list, where each pin is as tall as its
//! content and the selected pin is shown in full between the pins before and after it.

//! Spacing of the list, by display shape. The selected pin keeps the margin from the top and
//! bottom of the screen and the gap from the pins around it, which rest out toward the top and
//! bottom: on rect as far as their margin, on round as far as their time and title fit beside
//! the circle.
#define TIMELINE_LIST_MARGIN_RECT           4
#define TIMELINE_LIST_MARGIN_ROUND          16
#define TIMELINE_LIST_GAP_RECT              8
#define TIMELINE_LIST_GAP_ROUND             10
#define TIMELINE_LIST_NEIGHBOUR_MARGIN_RECT 2

//! A pin of the list, in on-screen order from the top.
typedef struct TimelineListPin {
  //! Height of the pin, from the top of its content to the top of the next pin.
  int16_t height;
  //! Offset from the pin's top of the point that rests on the geometry's anchor when selected.
  int16_t anchor_y;
  //! How close to the top and to the bottom of the viewport the pin rests when it is not
  //! selected, out toward them.
  int16_t edge_top;
  int16_t edge_bottom;
  //! Highest its top, and lowest its bottom, can be for it to show when it is not selected, the
  //! display cutting off what is beyond its edges.
  int16_t reach_top;
  int16_t reach_bottom;
} TimelineListPin;

//! Where the list rests.
typedef enum TimelineListAlign {
  //! The selected pin's anchor rests on the geometry's anchor.
  TimelineListAlignAnchor,
  //! The top pin rests below the top margin and the geometry's top space.
  TimelineListAlignTop,
} TimelineListAlign;

//! The viewport the list is laid out in.
typedef struct TimelineListGeometry {
  //! Height of the viewport.
  int16_t height;
  //! Where the selected pin's anchor rests, with TimelineListAlignAnchor.
  int16_t anchor_y;
  //! Space kept free at the top and bottom of the viewport around the selected pin.
  int16_t margin;
  //! Space kept between the selected pin and the pins around it.
  int16_t gap;
  //! Space kept above the top pin with TimelineListAlignTop, for what shows before it.
  int16_t top_space;
  //! Where the top of a selected pin taking the whole screen rests, scrolled to its first page,
  //! with TimelineListAlignTop.
  int16_t page_top;
  TimelineListAlign align;
  //! The pins after the selected one are below it, so the pins before it are above.
  bool next_is_down;
} TimelineListGeometry;

//! Height of a page of a selected pin that does not fit between the margins: the whole screen,
//! as the list scrolls through it a whole screen at a time.
//! @param geometry The viewport.
int16_t timeline_list_page_height(const TimelineListGeometry *geometry);

//! Where the selected pin rests, before the list scrolls through it.
//!
//! Anchored, its anchor always rests on the geometry's anchor. Aligned to the top, the top pin
//! rests at the top, the selected pin moving up just enough for it to fit between the margins,
//! and for the pin after it, then the one before it, to fit too if there is room. A selected pin
//! that does not fit between the margins takes the whole screen, and the list scrolls through it
//! a page at a time: anchored from where its anchor rests, aligned to the top from its top.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param[out] whole_screen_out Whether the selected pin takes the whole screen.
//! @return Top of the selected pin.
int16_t timeline_list_selected_top(const TimelineListGeometry *geometry,
                                   const TimelineListPin *pins, int num_pins, int selected,
                                   bool *whole_screen_out);

//! Places the pins where the list rests: the selected pin as timeline_list_selected_top() places
//! it, less the scroll, and the other pins stacked beyond the gap from it, resting out toward
//! their edges, showing as far as they reach. Those that do not are pushed off screen beyond the
//! edge they are by, as are the pins past them.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param scroll How far the list is scrolled through a selected pin taking the whole screen.
//! @param[out] tops_out Top of each pin.
//! @param[out] shown_out Whether each pin is on screen.
void timeline_list_arrange(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                           int num_pins, int selected, int16_t scroll, int16_t *tops_out,
                           bool *shown_out);

//! Computes the top of each pin.
//!
//! The pins stack without gaps and rest as the geometry aligns them, except that the selected
//! pin's top never rests above the margin: a selected pin taller than the screen shows from its
//! top and pushes its neighbours off screen.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param[out] tops_out Top of each pin.
void timeline_list_layout(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                          int num_pins, int selected, int16_t *tops_out);

//! Bottom of the page the selected pin shows its text in: above the pin after it on screen, so
//! that pin stays whole on screen, unless that leaves the page too short. A page is never
//! shorter than the shortest page, even if it goes on below the screen.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param page_top Top of the page.
//! @param min_height Shortest page.
//! @return Bottom of the page.
int16_t timeline_list_page_bottom(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                                  int num_pins, int selected, int16_t page_top, int16_t min_height);

//! How far the list scrolls through a pin to show its end, in whole pages.
//! @param content_height Height of the pin.
//! @param page_height Height of a page.
//! @return The largest scroll, a multiple of the page height, or 0 if the pin fits on a page.
int16_t timeline_list_max_scroll(int16_t content_height, int16_t page_height);

//! Places the pins outside a run of placed pins against their neighbours, without gaps.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins.
//! @param first_placed Index of the first pin of the run whose top is already in tops.
//! @param last_placed Index of the last pin of the run.
//! @param[in,out] tops Top of each pin; only the run is read.
void timeline_list_stack(const TimelineListPin *pins, int num_pins, int first_placed,
                         int last_placed, int16_t *tops);

//! A circle hanging off a side of a round display, as the round sidebar does, and the text that
//! runs beside it. Distances are from the sides of the display's bounds.
typedef struct TimelineListArc {
  //! Radius of the display; its middle row is this far from the top.
  int16_t display_radius;
  //! Text keeps this far inside the display's edge, as text flow does.
  int16_t text_inset;
  //! Text keeps at least this far from the side of the display's bounds.
  int16_t min_inset;
  //! Distance from the circle's side of the display to the circle's center, negative when the
  //! center is beyond that side. The center is on the middle row.
  int16_t center_inset;
  //! Radius of the circle.
  int16_t radius;
  //! Text keeps this far from the circle.
  int16_t gap;
} TimelineListArc;

//! How deep the circle reaches into the display across a run of rows.
//! @param arc The circle.
//! @param top First row.
//! @param bottom Last row.
//! @return Distance from the circle's side of the display to its farthest edge across the rows,
//! 0 or less if the rows miss it.
int16_t timeline_list_arc_depth(const TimelineListArc *arc, int16_t top, int16_t bottom);

//! How far in from the side of the display opposite the circle its edge is across a run of rows,
//! where text there starts.
//! @param arc The circle.
//! @param top First row.
//! @param bottom Last row.
//! @return Distance from the side of the display's bounds to inside its edge where the rows are
//! narrowest, at least the minimum inset. Off the display, its middle.
int16_t timeline_list_arc_edge_inset(const TimelineListArc *arc, int16_t top, int16_t bottom);

//! How much room text beside the circle has to rest in: from the edge inset across its head to
//! the display's edge on the other side, but the gap short of the circle anywhere across its rows.
//! @param arc The circle.
//! @param head_top First row of the text's head, the lines it is placed by.
//! @param head_bottom Last row of the text's head.
//! @param top First row of the text.
//! @param bottom Last row of the text.
//! @return Width of the text.
int16_t timeline_list_arc_text_width(const TimelineListArc *arc, int16_t head_top,
                                     int16_t head_bottom, int16_t top, int16_t bottom);

//! Where text beside the circle starts, from the side of the display opposite the circle: at the
//! edge inset across its head, but never closer to the circle than the gap anywhere across its
//! rows. Text is placed alike at rest and moving, keeping the width it rests in.
//! @param arc The circle.
//! @param head_top First row of the text's head.
//! @param head_bottom Last row of the text's head.
//! @param top First row of the text.
//! @param bottom Last row of the text.
//! @param width How far the text reaches toward the circle from where it starts.
//! @return Distance from the side of the display's bounds to where the text starts.
int16_t timeline_list_arc_text_inset(const TimelineListArc *arc, int16_t head_top,
                                     int16_t head_bottom, int16_t top, int16_t bottom,
                                     int16_t width);

//! Where text runs across a run of rows: from inside the display's edge on the side opposite the
//! circle to the gap short of the circle, or short of the display's edge where the circle is
//! beyond it. Text aligned toward the circle sits that gap from it, following its curve.
//! @param arc The circle.
//! @param top First row.
//! @param bottom Last row.
//! @param[out] end_out Distance from the side of the display's bounds opposite the circle to where
//! the text ends.
//! @return Distance from the side of the display's bounds opposite the circle to where the text
//! starts, at least the minimum inset.
int16_t timeline_list_arc_text_range(const TimelineListArc *arc, int16_t top, int16_t bottom,
                                     int16_t *end_out);

//! Rows text of a width fits on beside the circle, from the top and bottom of the display.
//! @param arc The circle.
//! @param width Width of the text.
//! @param[out] bottom_out Distance from the bottom of the display to the last row it fits on.
//! @return Distance from the top of the display to the first row it fits on.
int16_t timeline_list_arc_band(const TimelineListArc *arc, int16_t width, int16_t *bottom_out);

//! Where text laid out on a run of rows runs when it is drawn on other rows: as wide as where it
//! is laid out, so it wraps alike, and as far from the circle as where it is laid out, so it moves
//! along the circle's curve, even past the display's edge, where the display cuts it off.
//! @param arc The circle.
//! @param top First row of the text where it is laid out.
//! @param bottom Last row of the text where it is laid out.
//! @param dy How far down the text is drawn from where it is laid out.
//! @param[out] end_out Distance from the side of the display's bounds opposite the circle to where
//! the text ends.
//! @return Distance from the side of the display's bounds opposite the circle to where the text
//! starts.
int16_t timeline_list_arc_drawn_range(const TimelineListArc *arc, int16_t top, int16_t bottom,
                                      int16_t dy, int16_t *end_out);

//! Angle around the circle's center that spans a length along a circle around it.
//! @param distance Radius of the circle the length is along.
//! @param length Length along that circle.
//! @return Angle, TRIG_MAX_ANGLE being a full turn.
int32_t timeline_list_arc_angle(int16_t distance, int16_t length);

//! Where an icon is on a circle around the circle's center, turned by an angle from the row of the
//! center: a positive angle turns it down. Past a quarter turn it is behind the circle's side of
//! the display.
//! @param arc The circle.
//! @param distance Distance from the circle's center to the icon's.
//! @param angle Angle from the circle's middle row, TRIG_MAX_ANGLE being a full turn.
//! @param[out] y_out Row of the icon's center.
//! @return Distance from the circle's side of the display to the icon's center.
int16_t timeline_list_arc_icon_inset(const TimelineListArc *arc, int16_t distance, int32_t angle,
                                     int16_t *y_out);

//! Angle to turn an icon on a circle around the circle's center by for it to be on a row.
//! @param arc The circle.
//! @param distance Distance from the circle's center to the icon's.
//! @param row Row of the icon's center.
//! @return Angle, a quarter turn at most either way.
int32_t timeline_list_arc_row_angle(const TimelineListArc *arc, int16_t distance, int16_t row);

//! How far an icon on a circle around the circle's center turns either way before its center
//! leaves the display.
//! @param arc The circle.
//! @param distance Distance from the circle's center to the icon's.
//! @param inset How far inside the display's edge the icon's center stays.
//! @return Angle, TRIG_MAX_ANGLE being a full turn.
int32_t timeline_list_arc_max_angle(const TimelineListArc *arc, int16_t distance, int16_t inset);

//! The round sidebar: a circle hanging off a side of the display, its notch cut into its edge in
//! the middle row, pointing at the selected pin.
typedef struct TimelineListSidebar {
  //! Height of the display; its middle row is half of it from the top.
  int16_t height;
  //! Distance from the sidebar's side of the display to the circle's center, negative when the
  //! center is beyond that side. The center is on the middle row.
  int16_t center_inset;
  //! Radius of the circle.
  int16_t radius;
  //! How deep the notch cuts into the circle at its tip, and how tall it is at the circle's edge.
  int16_t notch_width;
  int16_t notch_height;
} TimelineListSidebar;

//! How far the sidebar reaches into the display on a row, notch included, for it to be filled one
//! row at a time.
//! @param sidebar The sidebar.
//! @param row The row.
//! @return Distance from the sidebar's side of the display to its edge on the row, 0 or less if
//! the row misses it.
int16_t timeline_list_sidebar_depth(const TimelineListSidebar *sidebar, int16_t row);
