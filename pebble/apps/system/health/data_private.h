/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "data.h"

#include <pbl/util/units.h>

#include <applib/platform.h>

typedef struct HealthData {
  //!< Current step / activity info
  int32_t step_data[HEALTH_DATA_NUM_DAYS]; //!< Step history for today and the days before it
  int32_t current_distance_meters;
  int32_t current_calories;
  int32_t current_active_seconds;

  //!< Typical step info
  ActivityMetricAverages step_averages;   //!< The step averages for the current day
  int32_t current_step_average;           //!< The current step average so far
  int32_t step_average_last_updated_time; //!< The time at which current_step_average was updated

  int32_t monthly_step_average;

  int32_t sleep_data[HEALTH_DATA_NUM_DAYS];      //!< Sleep history of the nights ending each day
  int32_t typical_sleep;                         //! Typical sleep for the current week day
  int32_t deep_sleep;                            //!< Amount of deep sleep last night
  int32_t deep_sleep_data[HEALTH_DATA_NUM_DAYS]; //!< Deep sleep history of those nights

  int32_t sleep_start;         //!< When the user went to sleep (seconds after midnight)
  int32_t sleep_end;           //!< When the user woke up (seconds after midnight)
  int32_t typical_sleep_start; //!< When the user typically goes to sleep
  int32_t typical_sleep_end;   //!< When the user typically wakes up

  int32_t monthly_sleep_average;

  uint32_t num_activity_sessions; //!< Number of activity sessions returned by the API
  ActivitySession activity_sessions[ACTIVITY_MAX_ACTIVITY_SESSIONS_COUNT]; //!< Activity sessions

  int32_t current_hr_bpm; //!< Current BPM
  int32_t resting_hr_bpm; //!< Resting BPM
  time_t hr_last_updated; //!< Time at which HR data was last updated

  HealthWorkoutData workout; //!< The workout going on

#if PBL_ROUND
  HealthSpans walks[HEALTH_DATA_WALK_DAYS]; //!< Walks and runs of today and the days before it
  uint8_t walks_read;                       //!< Bit per day of those read
  time_t walks_midnight;                    //!< Today's midnight when they were read
  int32_t walks_steps;                      //!< Today's steps when its walks were read
  HealthSpans deep_sleep_spans;             //!< Deep sleep of last night
#endif

  int32_t hr_zone1_minutes;
  int32_t hr_zone2_minutes;
  int32_t hr_zone3_minutes;
} HealthData;

//! Adds a stretch to stretches in the order they happened, merged with those it touches, and the
//! two closest merged if there are then too many
void health_data_spans_add(HealthSpans *spans, int32_t from, int32_t to);

//! Finds the walks and runs in the steps of each minute of a day as the activity algorithm detects
//! them, at least 10 minutes of 40 steps or more a minute, without 8 minutes in a row below that
//! @param minute_steps The steps of each minute of the day, in order
//! @param num_minutes Number of minutes in \a minute_steps
//! @param day_steps The day's steps, which the minutes' are scaled to
//! @param[out] walks_out Each walk or run, from the day's steps before it to those by its end
void health_data_find_walks(const uint8_t *minute_steps, uint32_t num_minutes, int32_t day_steps,
                            HealthSpans *walks_out);

//! Finds the deep sleep in a night's sessions, in minutes of its sleep
//! @param sessions The night's sessions, its sleep and deep sleep
//! @param num_sessions Number of \a sessions
//! @param[out] deep_out Each deep sleep, from the minutes slept before it to those by its end
void health_data_find_deep_sleep(const ActivitySession *sessions, uint32_t num_sessions,
                                 HealthSpans *deep_out);
