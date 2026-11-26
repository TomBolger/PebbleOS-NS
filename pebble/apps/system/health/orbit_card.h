/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_card.h
//! The cards of the round Health app, one per section of the orbit: rings round the bezel that
//! stop short of the sidebar, with the numbers inside them.

#include "data.h"

#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/graphics.h>

//! The sections on the orbit, from the top: Workout above Today, where the app opens
typedef enum HealthOrbitSection {
  HealthOrbitSectionWorkout,
  HealthOrbitSectionToday,
  HealthOrbitSectionSteps,
  HealthOrbitSectionSleep,
  HealthOrbitSectionCount,
} HealthOrbitSection;

//! Most rings a card has
#define HEALTH_ORBIT_CARD_MAX_RINGS 2
//! Most bubbles a card has below its numbers
#define HEALTH_ORBIT_CARD_MAX_BUBBLES 3
//! Radius of those bubbles
#define HEALTH_ORBIT_CARD_BUBBLE_RADIUS 23
//! A ring's sweep at rest: as far as its value
#define HEALTH_ORBIT_CARD_SWEEP_REST UINT32_MAX

//! How a card is drawn
typedef struct HealthOrbitCard {
  HealthOrbitSection section;
  //! The day whose overview the card is in place of the section's own, as the Today card has
  //! today's, or nullptr
  const HealthDayData *day;
  //! How far down its numbers and bubbles are moved from where they rest
  int16_t offset_y;
  //! Angle each ring sweeps from its start, or HEALTH_ORBIT_CARD_SWEEP_REST
  uint32_t sweep[HEALTH_ORBIT_CARD_MAX_RINGS];
  //! Angle each ring's track sweeps from its start, or HEALTH_ORBIT_CARD_SWEEP_REST for all of it
  uint32_t track[HEALTH_ORBIT_CARD_MAX_RINGS];
  //! Radius of each bubble
  int16_t bubble_radius[HEALTH_ORBIT_CARD_MAX_BUBBLES];
  //! The Workout card's run shoe, shown large with no workout going on
  GDrawCommandImage *run_icon;
} HealthOrbitCard;

//! Width the Workout card's run shoe is drawn at
#define HEALTH_ORBIT_WORKOUT_SHOE_WIDTH 96

//! The kinds of workout, in the order the Workout app offers them
extern const ActivitySessionType health_orbit_workout_kinds[3];

//! Color of a kind of workout, as the Workout app has it
GColor health_orbit_workout_color(ActivitySessionType type);

//! Deeper color of a kind of workout, to read on white, as the Workout app's action bars have it
GColor health_orbit_workout_ink(ActivitySessionType type);

//! Name of a kind of workout, to be translated, as "Run"
const char *health_orbit_workout_name(ActivitySessionType type);

//! A workout's time as a stopwatch shows it, as "12:34" or "1:02:03"
void health_orbit_format_workout_time(char *buffer, size_t size, int32_t seconds);

//! A workout's distance in the user's unit, as "3.5"
void health_orbit_format_workout_distance(char *buffer, size_t size, int32_t meters);

//! A workout's average pace in the user's unit, as "8:42", or a dash before it has one
void health_orbit_format_workout_pace(char *buffer, size_t size, int32_t duration_s,
                                      int32_t distance_m);

//! A workout's steps a minute
int32_t health_orbit_workout_cadence(int32_t steps, int32_t duration_s);

//! Color of the sidebar on a section's card
GColor health_orbit_section_color(HealthOrbitSection section);

//! Background color of a section's card
GColor health_orbit_section_bg_color(HealthOrbitSection section);

//! A section's card at rest, or the overview of \a day in its place if not nullptr
void health_orbit_card_init(HealthOrbitCard *card, HealthOrbitSection section,
                            const HealthDayData *day);

//! How far each of a card's rings sweeps for the data
//! @param[out] sweeps_out Angle swept by each ring
//! @param[out] spans_out Angle each ring spans, its whole track, or nullptr
//! @return Number of rings
int health_orbit_card_get_sweeps(const HealthOrbitCard *card, HealthData *health_data,
                                 uint32_t sweeps_out[HEALTH_ORBIT_CARD_MAX_RINGS],
                                 uint32_t spans_out[HEALTH_ORBIT_CARD_MAX_RINGS]);

//! Number of bubbles a card has
int health_orbit_card_get_num_bubbles(const HealthOrbitCard *card);

//! Draws a card's numbers and bubbles
void health_orbit_card_draw_content(GContext *ctx, const HealthOrbitCard *card,
                                    HealthData *health_data, void *i18n_owner);

//! Draws a card's rings, over its content as that slides past them, with their tracks or not
void health_orbit_card_draw_rings(GContext *ctx, const HealthOrbitCard *card,
                                  HealthData *health_data, bool tracks);
