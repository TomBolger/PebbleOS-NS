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
