/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_week.h
//! The week of a section of the round Health app: a row a day below the banner the sidebar turns
//! into, the most recent day on top, each a pill filling with the day's value.

#include "data.h"
#include "orbit_card.h"

#include <pbl/util/units.h>

#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/graphics.h>
#include <applib/ui/menu_layer_round.h>

#define HEALTH_ORBIT_WEEK_NUM_ROWS PBL_DAY_PER_WEEK

//! Whether a section has a week to show
bool health_orbit_week_has_section(HealthOrbitSection section);

//! The pill of each row where it rests
void health_orbit_week_get_rest_rows(MenuRoundShape rows_out[HEALTH_ORBIT_WEEK_NUM_ROWS]);

//! Draws the week of \a section, each row in the pill \a rows has for it, its text showing as far
//! as the pill has grown, and the line above them \a header_offset_y below where it rests.
//! @param highlight The pill the selection's outline goes round, or nullptr
//! @param highlight_row The row the outline is drawn with, under the rows above it
//! @param crown The crown to put by the best day, or nullptr
void health_orbit_week_draw(GContext *ctx, HealthOrbitSection section, HealthData *health_data,
                            const MenuRoundShape rows[HEALTH_ORBIT_WEEK_NUM_ROWS],
                            int16_t header_offset_y, const MenuRoundShape *highlight,
                            int highlight_row, GDrawCommandImage *crown, void *i18n_owner);
