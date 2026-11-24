/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

//! The path round list rows follow: the distance from the left edge of a round display of this
//! radius to the path, at dy from the middle (either sign).
//!
//! In the middle it is the rim. Towards the top and bottom it eases off the rim onto a gentler
//! curve, joined without a kink, whose slope levels off, so rows leaving the screen keep curving
//! outwards a little while mostly moving up or down, clipped by the bezel, instead of being
//! thrown sideways where the rim turns vertical. It goes on past the edge of the screen.
int16_t notifications_round_path_x(int16_t radius, int16_t dy);
