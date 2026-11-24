/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file menu_layer_round.h
//! Geometry of a MenuLayer's highlight on round displays. Pure functions of screen coordinates,
//! so they can be tested without drawing.

#include <stdbool.h>
#include <stdint.h>

#include <applib/graphics/gtypes.h>

//! Gap between the ends of the highlight pill and the bezel
#define MENU_ROUND_PILL_BEZEL_MARGIN 4
//! Gap between the highlight pill and the top and bottom of its row
#define MENU_ROUND_PILL_INSET_Y 3

//! The highlight: a box with rounded corners
typedef struct {
  GRect box;
  int16_t radius;
} MenuRoundShape;

//! The pill over a highlight \a frame, in screen coordinates: inset MENU_ROUND_PILL_INSET_Y from
//! the top and bottom, with fully round ends whose centres stay on a circle concentric with the
//! \a display, so both ends keep MENU_ROUND_PILL_BEZEL_MARGIN, plus \a left_margin on the left,
//! clear of the bezel. The ends keep the radius of a pill \a rest_h tall while the highlight
//! stretches between rows. The pill stays within \a bounds horizontally.
//! @return false if there is no room for a pill
bool menu_round_pill(GSize display, GRect bounds, GRect frame, int16_t rest_h, int16_t left_margin,
                     MenuRoundShape *pill);

//! How far past one end a round list may rest so that, with the row at that end, \a end_h tall,
//! selected, its neighbour, \a next_h tall, is in the middle of a frame \a frame_h tall. Resting
//! no further out keeps the selection within a row of the middle, where the screen is wide.
int16_t menu_round_end_space(int16_t frame_h, int16_t end_h, int16_t next_h);

//! How many rows of a round list with the pill fit in its frame
#define MENU_ROUND_VISIBLE_ROWS 5

//! The height of a row of a round list with the pill, \a focused_h tall when selected and
//! \a unfocused_h tall otherwise, in a frame \a frame_h tall: every row is as tall whatever is
//! selected: as tall as MENU_ROUND_VISIBLE_ROWS rows fit, or less if it is shorter selected. A
//! card, taller than that even unselected, or a page, taller than half the frame selected, keeps
//! the height it has selected.
int16_t menu_round_row_height(int16_t frame_h, int16_t focused_h, int16_t unfocused_h);

//! Where a round centre-focused list rests, as the content offset, with the row at \a span_y,
//! \a span_h tall, selected: centred, but stopping \a top_space above the content's top and
//! \a bottom_space below its bottom, the content being \a content_h tall in a frame \a frame_h
//! tall. Content that fits with that space is centred as a whole.
int16_t menu_round_rest_offset(int16_t frame_h, int16_t content_h, int16_t span_y, int16_t span_h,
                               int16_t top_space, int16_t bottom_space);

//! The shape \a progress thousandths of the way from \a from to \a to: the box in between, its
//! corners as round as half its shorter side. A pill and a circle are both such boxes, so every
//! step is a single pill or circle. Past either end, when \a progress overshoots 0 to 1000, the
//! shape keeps its size and only its centre carries on.
MenuRoundShape menu_round_morph(const MenuRoundShape *from, const MenuRoundShape *to,
                                int32_t progress);

//! Scroll position of a list of \a count rows \a row_h tall, with \a selected selected, whose
//! first row starts \a edge_rows rows above the middle: the selection walks down to the middle,
//! the list then scrolls under it and stops with the last row \a edge_rows rows below the middle.
int32_t menu_round_list_scroll(uint16_t selected, uint16_t count, int16_t row_h,
                               uint16_t edge_rows);
