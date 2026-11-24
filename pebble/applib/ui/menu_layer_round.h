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
#define MENU_ROUND_BUBBLE_RADIUS 150
//! A morph's progress when it reaches the pill, from the bubble at 0
#define MENU_ROUND_MORPH_PILL 1000
//! A morph's progress when the pill starts spreading out from the bottom of the circle
#define MENU_ROUND_MORPH_PILL_GROWS 500
//! How far, per whole morph past its end, the shape carries on before settling
#define MENU_ROUND_MORPH_OVERSHOOT_PX 60

//! The highlight: a rounded box, and while morphing from the bubble to a pill, a circle as well
typedef struct {
  GRect box;
  int16_t radius;
  GPoint circle_center;
  //! 0 without a circle
  int16_t circle_radius;
} MenuRoundShape;

//! The pill over a highlight \a frame, in screen coordinates: inset MENU_ROUND_PILL_INSET_Y from
//! the top and bottom, with fully round ends whose centres stay on a circle concentric with the
//! \a display, so both ends keep MENU_ROUND_PILL_BEZEL_MARGIN, plus \a left_margin on the left,
//! clear of the bezel. The ends keep the radius of a pill \a rest_h tall while the highlight
//! stretches between rows. The pill stays within \a bounds horizontally.
//! @return false if there is no room for a pill
bool menu_round_pill(GSize display, GRect bounds, GRect frame, int16_t rest_h, int16_t left_margin,
                     MenuRoundShape *pill);

//! The bubble highlighting a header row: a circle hanging from above the \a display, centred
//! across it, whose bottom is at \a bottom (screen coordinates).
MenuRoundShape menu_round_bubble(GSize display, int16_t bottom);

//! The shape \a progress of the way from \a bubble to \a pill (0 to MENU_ROUND_MORPH_PILL, past
//! either end to overshoot): the bubble drops towards the pill as a circle, shrinking to the
//! pill's height as its round bottom reaches the pill's bottom, while the pill spreads out from
//! the circle's bottom over the last part of the way.
MenuRoundShape menu_round_morph(const MenuRoundShape *bubble, const MenuRoundShape *pill,
                                int32_t progress);

//! The box around all of \a shape
GRect menu_round_shape_bounds(const MenuRoundShape *shape);

//! How far past one end a round list may rest so that, with the row at that end, \a end_h tall,
//! selected, its neighbour, \a next_h tall, is in the middle of a frame \a frame_h tall. Resting
//! no further out keeps the selection within a row of the middle, where the screen is wide.
int16_t menu_round_end_space(int16_t frame_h, int16_t end_h, int16_t next_h);

//! Where a round centre-focused list rests, as the content offset, with the row at \a span_y,
//! \a span_h tall, selected: centred, but stopping \a top_space above the content's top and
//! \a bottom_space below its bottom, the content being \a content_h tall in a frame \a frame_h
//! tall. Content that fits with that space is centred as a whole.
int16_t menu_round_rest_offset(int16_t frame_h, int16_t content_h, int16_t span_y, int16_t span_h,
                               int16_t top_space, int16_t bottom_space);
