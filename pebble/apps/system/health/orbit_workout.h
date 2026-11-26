/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_workout.h
//! The workout screens of the round Health app, below the banner the sidebar turns into as for the
//! week: the kinds of workout to start, and the workout going on, its time over its measures.

#include "data.h"
#include "orbit_layout.h"

#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/graphics.h>
#include <applib/ui/menu_layer_round.h>

//! What the line under the banner says
typedef enum HealthOrbitWorkoutStatus {
  //! Above the kinds of workout to start
  HealthOrbitWorkoutStatusKinds,
  //! The kind of workout, counting down to it or going on
  HealthOrbitWorkoutStatusRunning,
  HealthOrbitWorkoutStatusPaused,
  //! Asking whether to end the workout
  HealthOrbitWorkoutStatusEnd,
  //! The workout ended, its summary below
  HealthOrbitWorkoutStatusComplete,
} HealthOrbitWorkoutStatus;

//! Middle of the workout's time, below the line under the banner
#define HEALTH_WORKOUT_TIME_Y 102

//! Seconds counted down before a workout starts
#define HEALTH_WORKOUT_COUNTDOWN 3

//! Height a status or the time rolls up by as it changes
#define HEALTH_WORKOUT_STATUS_ROLL 16
#define HEALTH_WORKOUT_TIME_ROLL   44

//! What the workout screens show
typedef struct HealthOrbitWorkoutScreen {
  //! Kind of the workout shown
  ActivitySessionType type;
  //! What the line under the banner says, what it said, and how far below the line what it says
  //! still is as it rolls up into place
  HealthOrbitWorkoutStatus status;
  HealthOrbitWorkoutStatus status_from;
  int16_t status_roll;
  //! Seconds left to count down before the workout starts, 0 once it has, or -1 before the
  //! countdown, with no time to show; what it was, and how far below its place the time still is
  //! as it rolls up into it
  int8_t countdown;
  int8_t countdown_from;
  int16_t time_roll;
  //! Radius of the bubbles of the buttons, by Select and Down
  int16_t button_radius;
  //! The measures shown: the workout going on, or the one that ended
  HealthWorkoutData workout;
} HealthOrbitWorkoutScreen;

//! Number of rows of measures a kind of workout has
int health_orbit_workout_num_rows(ActivitySessionType type);

//! Draws the kinds of workout to start, each row in the pill \a rows has for it, the line above
//! them
//! \a header_offset_y below where it rests
//! @param icons Icons of the kinds of workout, in their deeper colors to read on white
//! @param highlight The pill the selection's ring goes round, over the rows, or nullptr
void health_orbit_workout_draw_kinds(GContext *ctx, const HealthOrbitWorkoutScreen *screen,
                                     const MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS],
                                     int16_t header_offset_y, const MenuRoundShape *highlight,
                                     GDrawCommandImage *const *icons, void *i18n_owner);

//! Draws the workout: the line saying how it is going and its time, \a header_offset_y below where
//! they rest, over its measures, each in the pill \a rows has for it, and the bubbles of the
//! buttons that pause and resume it, and end it
void health_orbit_workout_draw(GContext *ctx, const HealthOrbitWorkoutScreen *screen,
                               const MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS],
                               int16_t header_offset_y, void *i18n_owner);
