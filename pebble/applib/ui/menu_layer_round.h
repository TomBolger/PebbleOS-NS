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
//! Radius of the bubble that highlights a menu's header row
#define MENU_ROUND_BUBBLE_RADIUS 200
//! A morph's progress when it reaches the pill, from the bubble at 0
#define MENU_ROUND_MORPH_PILL 1000

//! The highlight: a box with rounded corners. A pill and the bubble, a circle, are both boxes whose
//! corner radius is half their shorter side.
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

//! The bubble highlighting a header row: a circle, MENU_ROUND_BUBBLE_RADIUS in radius, hanging
//! from above the \a display, centred across it, whose bottom is at \a bottom (screen
//! coordinates).
MenuRoundShape menu_round_bubble(GSize display, int16_t bottom);

//! Half the width of the bubble whose bottom is at \a bottom, \a y down the screen (0 below it)
int16_t menu_round_bubble_half_width(int16_t bottom, int16_t y);

//! The shape \a progress of the way from \a bubble to \a pill (0 to MENU_ROUND_MORPH_PILL): the
//! box in between, its corner radius half its shorter side, so every step is a single circle or
//! pill. Past either end, when easing overshoots, the shape keeps its size and its bottom carries
//! on in its direction.
MenuRoundShape menu_round_morph(const MenuRoundShape *bubble, const MenuRoundShape *pill,
                                int32_t progress);

//! How far past one end a round list may rest so that, with the row at that end, \a end_h tall,
//! selected, its neighbour, \a next_h tall, is in the middle of a frame \a frame_h tall. Resting
//! no further out keeps the selection within a row of the middle, where the screen is wide.
int16_t menu_round_end_space(int16_t frame_h, int16_t end_h, int16_t next_h);

//! The focused height of the row \a i rows in from one end of a list, see menu_round_end_rows()
typedef int16_t (*MenuRoundRowHeight)(uint16_t i, void *context);

//! How many of the \a count rows at one end of a round list can be selected while the list rests
//! at that end, \a space past it, in a frame \a frame_h tall: the end row, and the rows after it
//! whose middle is not past the middle of the frame, \a row_h giving their focused heights. Laid
//! out at that height whatever is selected, they stay still as the selection moves between them.
uint16_t menu_round_end_rows(int16_t frame_h, int16_t space, uint16_t count,
                             MenuRoundRowHeight row_h, void *context);

//! Where a round centre-focused list rests, as the content offset, with the row at \a span_y,
//! \a span_h tall, selected: centred, but stopping \a top_space above the content's top and
//! \a bottom_space below its bottom, the content being \a content_h tall in a frame \a frame_h
//! tall. Content that fits with that space is centred as a whole.
int16_t menu_round_rest_offset(int16_t frame_h, int16_t content_h, int16_t span_y, int16_t span_h,
                               int16_t top_space, int16_t bottom_space);
