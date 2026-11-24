/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! Vertical placement of the pins of the compact timeline list, where each pin is as tall as its
//! content and the selected pin is shown in full between the pins before and after it.

//! Spacing of the list, by display shape. The selected pin keeps the margin from the top and
//! bottom of the screen and the gap from the pins around it, which rest out toward the top and
//! bottom, as far as their own margin.
#define TIMELINE_LIST_MARGIN_RECT           4
#define TIMELINE_LIST_MARGIN_ROUND          16
#define TIMELINE_LIST_GAP_RECT              8
#define TIMELINE_LIST_GAP_ROUND             12
#define TIMELINE_LIST_NEIGHBOUR_MARGIN_RECT 2
//! On round, the other pins rest out into the narrow top and bottom of the display as far as their
//! text keeps this much room beside the circle.
#define TIMELINE_LIST_NEIGHBOUR_TEXT_WIDTH_ROUND 110

//! A pin of the list, in on-screen order from the top.
typedef struct TimelineListPin {
  //! Height of the pin, from the top of its content to the top of the next pin.
  int16_t height;
  //! Offset from the pin's top of the point that rests on the geometry's anchor when selected.
  int16_t anchor_y;
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
  //! Where the selected pin's anchor rests when nothing pushes it, with TimelineListAlignAnchor.
  int16_t anchor_y;
  //! Rows the selected pin's anchor can rest on, with TimelineListAlignAnchor.
  int16_t anchor_min;
  int16_t anchor_max;
  //! Space kept free at the top and bottom of the viewport around the selected pin.
  int16_t margin;
  //! Space kept free at the top and at the bottom of the viewport around the other pins.
  int16_t neighbour_margin;
  int16_t neighbour_margin_bottom;
  //! Space kept between the selected pin and the pins around it.
  int16_t gap;
  //! Space kept above the top pin with TimelineListAlignTop, for what shows before it.
  int16_t top_space;
  //! Where the top of a selected pin taking the whole screen rests, scrolled to its first page.
  int16_t page_top;
  TimelineListAlign align;
  //! The pins after the selected one are below it, so the pins before it are above.
  bool next_is_down;
} TimelineListGeometry;

//! Height of a page of a selected pin that does not fit between the margins: it takes the whole
//! screen, and the list scrolls through it a whole screen at a time, so no line is ever cut.
//! @param geometry The viewport.
int16_t timeline_list_page_height(const TimelineListGeometry *geometry);

//! Places the pins where the list rests.
//!
//! The selected pin always shows in full: it rests as the geometry aligns it, moving just enough
//! for it to fit between the margins, and for the pin after it to fit too if there is room. The
//! other pins stack beyond the gap from it and rest out toward the top and bottom of the viewport;
//! those that do not fit are pushed off screen beyond the edge they are by, as are the pins past
//! them. A selected pin taller than the screen between the margins takes the whole screen from its
//! top, scrolled through by whole pages.
//! @param geometry The viewport.
//! @param pins The pins, in on-screen order from the top.
//! @param num_pins Number of pins, at least 1.
//! @param selected Index of the selected pin.
//! @param scroll How far the list is scrolled through a selected pin taller than a page.
//! @param allowed Whether each pin may be on screen, or nullptr for all of them.
//! @param[out] tops_out Top of each pin.
//! @param[out] shown_out Whether each pin is on screen.
void timeline_list_arrange(const TimelineListGeometry *geometry, const TimelineListPin *pins,
                           int num_pins, int selected, int16_t scroll, const bool *allowed,
                           int16_t *tops_out, bool *shown_out);

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

//! Where text runs across a run of rows: from inside the display's edge on the side opposite the
//! circle to the gap short of the circle, or to inside the display's edge above and below it.
//! Text aligned toward the circle follows its curve.
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
//! is laid out, so it wraps alike, and ending where text on the rows it is drawn on ends, so text
//! aligned toward the circle follows its curve wherever it is drawn.
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
