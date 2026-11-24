/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"

#include <time.h>

// The moon's phase, and a drawing of it, from the Halcylune watchface.

//! The moon's elongation from the sun in degrees [0, 360): 0 is new moon, 90 first quarter,
//! 180 full moon, 270 last quarter. Main periodic terms from Meeus (Astronomical Algorithms,
//! ch. 47-48), good to a few hours of phase timing.
float weather_moon_elongation(time_t utc_time);

//! Draws the moon centred at @p center, @p radius at most 12 px: the lit part in white with a
//! few grey maria, an @p outline around the lit part, and a dotted circle tracing the unlit
//! limb.
void weather_moon_draw(GContext *ctx, GPoint center, int radius, float elongation,
                       GColor outline);
