/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/services/activity/activity.h>

typedef struct {
  int32_t sum;
  int32_t avg;
  int32_t max;
} BasicHealthStats;

typedef struct {
  BasicHealthStats weekday;
  BasicHealthStats weekend;
  BasicHealthStats daily;
} WeeklyStats;

//! Health data model
typedef struct HealthData HealthData;

//! Days the health data has the history of, today and the days before it: all the watch keeps
#define HEALTH_DATA_NUM_DAYS ACTIVITY_HISTORY_DAYS

//! A stretch of a day's value, from how much of it there was when the stretch began to how much
//! there was when it ended
typedef struct HealthSpan {
  uint16_t from;
  uint16_t to;
} HealthSpan;

//! Most stretches a day keeps of a kind, the closest ones merged past it
#define HEALTH_DATA_MAX_SPANS 6

//! Stretches of a day's value, in the order they happened
typedef struct HealthSpans {
  uint8_t count;
  HealthSpan spans[HEALTH_DATA_MAX_SPANS];
} HealthSpans;

//! Days the walks are read for, today and the days of the week before it: the minute file they
//! are found in holds about five and a half days of minutes
#define HEALTH_DATA_WALK_DAYS 7

//! What the overview of a day shows
typedef struct HealthDayData {
  //! How many days before today the day is
  int days_ago;
  int32_t steps;
  //! Typical steps of the day's weekday: by now for today, over the whole day before it
  int32_t typical_steps_by_now;
  int32_t typical_steps;
  int32_t distance_meters;
  int32_t active_seconds;
  //! Sleep of the night that ended on the day, and its deep sleep
  int32_t sleep;
  int32_t deep_sleep;
  //! Typical sleep of a night ending on the day's weekday
  int32_t typical_sleep;
  //! The day's walks and runs, in its steps, as far as the watch has its minutes, none for a day
  //! before the week
  HealthSpans walks;
  //! The night's deep sleep, in minutes of its sleep, if the watch still has its sessions
  HealthSpans deep_sleep_spans;
} HealthDayData;

//! What the workout going on is at
typedef enum HealthWorkoutState {
  //! No workout, nor a walk or run the watch has detected
  HealthWorkoutStateNone,
  //! A walk, run or open workout the watch detected and is still going on, not yet a workout
  HealthWorkoutStateDetected,
  HealthWorkoutStateRunning,
  HealthWorkoutStatePaused,
} HealthWorkoutState;

//! The workout going on, or the session the watch detected
typedef struct HealthWorkoutData {
  HealthWorkoutState state;
  ActivitySessionType type;
  //! Whether the watch detected it, as far as the health data has seen it go from a detected
  //! session to a workout
  bool auto_detected;
  //! Time it has gone on, without its pauses
  int32_t duration_s;
  int32_t distance_m;
  int32_t steps;
  int32_t active_kcal;
  //! The detected session, to turn into a workout
  ActivitySession session;
} HealthWorkoutData;

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions
//

//! Create a health data structure
//! @return A pointer to the new HealthData structure
HealthData *health_data_create(void);

//! Destroy a health data structure
//! @param health_data A pointer to an existing health data structure
void health_data_destroy(HealthData *health_data);

//! Fetch the current activity data from the system
//! @param health_data A pointer to the health data to use
void health_data_update(HealthData *health_data);

//! Fetch only the data required to display the initial card.
//! This helps reduce lag when opening the app
//! @param health_data A pointer to the health data to use
void health_data_update_quick(HealthData *health_data);

//! Fetch the current data for step derived metrics (distance, active time, calories)
//! @param health_data A pointer to the health data to use
void health_data_update_step_derived_metrics(HealthData *health_data);

//! Update the number of steps the user has taken today
//! @param health_data A pointer to the health data to use
//! @param new_steps the new value of the steps for today
void health_data_update_steps(HealthData *health_data, uint32_t new_steps);

//! Update the number of seconds the user has slept today
//! @param health_data A pointer to the health data to use
//! @param new_sleep the new value of the seconds of sleep today
//! @param new_deep_sleep the new value of the seconds of deep sleep today
void health_data_update_sleep(HealthData *health_data, uint32_t new_sleep, uint32_t new_deep_sleep);

//! Update the current HR BPM
//! @param health_data A pointer to the health data to use
void health_data_update_current_bpm(HealthData *health_data);

//! Update the time in HR zones
//! @param health_data A pointer to the health data to use
void health_data_update_hr_zone_minutes(HealthData *health_data);

//! Get the current step count
//! @param health_data A pointer to the health data to use
//! @return the current step count
int32_t health_data_current_steps_get(HealthData *health_data);

//! Get the historical step data
//! @param health_data A pointer to the health data to use
//! @return A pointer to historical step data
int32_t *health_data_steps_get(HealthData *health_data);

//! Get the current distance traveled in meters
//! @param health_data A pointer to the health data to use
//! @return the current distance traveled in meters
int32_t health_data_current_distance_meters_get(HealthData *health_data);

//! Get the current calories
//! @param health_data A pointer to the health data to use
//! @return the current calories
int32_t health_data_current_calories_get(HealthData *health_data);

//! Get the time spent active today
//! @param health_data A pointer to the health data to use
//! @return the active time today in seconds
int32_t health_data_current_active_seconds_get(HealthData *health_data);

//! Get current number of steps that should be taken by this time today
//! @param health_data A pointer to the health data to use
//! @return The integer number of steps that should be taken by this time today
int32_t health_data_steps_get_current_average(HealthData *health_data);

//! Get the minute-of-day of the latest completed step-average bin. Reflects the
//! value cached by the most recent health_data_steps_get_current_average() call.
//! @param health_data A pointer to the health data to use
//! @return The minute-of-day (0..1439) of the latest completed step-average bin
int32_t health_data_steps_get_current_average_minute(HealthData *health_data);

//! Get the step average for the current day of the week
//! @param health_data A pointer to the health data to use
//! @return An integer value for the number of steps that are typically taken on this week day
int32_t health_data_steps_get_cur_wday_average(HealthData *health_data);

//! Get the step average over the past month
//! @param health_data A pointer to the health data to use
//! @return An integer value for the average number of steps that we taken over the past month
int32_t health_data_steps_get_monthly_average(HealthData *health_data);

//! Get the historical sleep data
//! @param health_data A pointer to the health data to use
//! @return A pointer to historical sleep data
int32_t *health_data_sleep_get(HealthData *health_data);

//! Get the current sleep length
//! @param health_data A pointer to the health data to use
//! @return the current sleep length
int32_t health_data_current_sleep_get(HealthData *health_data);

//! Gets the typical sleep duration for the current weekday
int32_t health_data_sleep_get_cur_wday_average(HealthData *health_data);

//! Get the current deep sleep data
//! @param health_data A pointer to the health data to use
//! @return the current deep sleep length
int32_t health_data_current_deep_sleep_get(HealthData *health_data);

//! Get what the overview of a day shows, from today to HEALTH_DATA_NUM_DAYS - 1 days before it.
//! Today's is what the health data has; a day before it reads its history and typical values.
//! @param health_data A pointer to the health data to use
//! @param days_ago How many days before today the day is
//! @param[out] day_out What the overview of the day shows
void health_data_day_get(HealthData *health_data, int days_ago, HealthDayData *day_out);

//! Reads today's walks and runs again if there have been steps since they were last read, and
//! forgets those of the days before it once it is a new day
//! @param health_data A pointer to the health data to use
void health_data_update_walks(HealthData *health_data);

//! Reads the walks and runs of the days of the week before today not yet read
//! @param health_data A pointer to the health data to use
void health_data_read_week_walks(HealthData *health_data);

//! Get the walks and runs of a day of the week as read
//! @param health_data A pointer to the health data to use
//! @param days_ago How many days before today the day is, less than HEALTH_DATA_WALK_DAYS
//! @return The day's walks and runs, nullptr if not read
const HealthSpans *health_data_walks_get(HealthData *health_data, int days_ago);

//! Get the deep sleep of the night of health_data_sleep_get()'s first entry
//! @param health_data A pointer to the health data to use
//! @return Its deep sleep in minutes of its sleep, none if the watch no longer has its sessions
const HealthSpans *health_data_deep_sleep_spans_get(HealthData *health_data);

//! Read the workout going on from the workout service, or else the walk, run or open workout the
//! watch has detected and is still going on
//! @param health_data A pointer to the health data to use
void health_data_update_workout(HealthData *health_data);

//! Get the workout going on as last read
//! @param health_data A pointer to the health data to use
//! @return The workout, its state none if there is none
const HealthWorkoutData *health_data_workout_get(HealthData *health_data);

//! Get the sleep average over the past month
//! @param health_data A pointer to the health data to use
//! @return The average daily sleep over the past month
int32_t health_data_sleep_get_monthly_average(HealthData *health_data);

// Get the sleep start time
int32_t health_data_sleep_get_start_time(HealthData *health_data);

// Get the sleep end time
int32_t health_data_sleep_get_end_time(HealthData *health_data);

// Get the typical sleep start time
int32_t health_data_sleep_get_typical_start_time(HealthData *health_data);

// Get the typical sleep end time
int32_t health_data_sleep_get_typical_end_time(HealthData *health_data);

// Get the number of sleep sessions
int32_t health_data_sleep_get_num_sessions(HealthData *health_data);

// Get today's sleep sessions
ActivitySession *health_data_sleep_get_sessions(HealthData *health_data);

// Get current BPM
uint32_t health_data_hr_get_current_bpm(HealthData *health_data);

// Get resting BPM
uint32_t health_data_hr_get_resting_bpm(HealthData *health_data);

// Get HR last updated timestamp
time_t health_data_hr_get_last_updated_timestamp(HealthData *health_data);

// Get number of minutes in Zone 1
int32_t health_data_hr_get_zone1_minutes(HealthData *health_data);

// Get number of minutes in Zone 2
int32_t health_data_hr_get_zone2_minutes(HealthData *health_data);

// Get number of minutes in Zone 3
int32_t health_data_hr_get_zone3_minutes(HealthData *health_data);

//! Read today's heart rate for each minute of the local day
//! @param minute_bpm Filled with PBL_MIN_PER_DAY entries, 0 where there is no reading
//! @return Number of minutes of today so far, including the current one
uint16_t health_data_hr_get_today_bpm(uint8_t *minute_bpm);
