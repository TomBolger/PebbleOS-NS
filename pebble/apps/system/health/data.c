/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "data.h"

#include "data_private.h"

#include <string.h>
#include <time.h>

#include <pbl/kernel/compiler.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/services/activity/workout_service.h>
#include <pbl/services/time.h>
#include <pbl/util/math.h>
#include <pbl/util/testing.h>
#include <pbl/util/units.h>

#include <applib/app_malloc.h>
#include <applib/health_service_private.h>
#include <syscall/syscall.h>

PBL_T_STATIC void prv_merge_adjacent_sessions(ActivitySession *current, ActivitySession *previous) {
  if (previous == nullptr || current == nullptr) {
    return;
  }

  if (current->type != previous->type || (current->type != ActivitySessionType_RestfulNap &&
                                          current->type != ActivitySessionType_RestfulSleep)) {
    // We only merge sessions if they are "deep" sleep/nap
    return;
  }

  // [FBO]: note that this only works because sleep sessions are
  // all we care about and they are sorted. Don't try to extend this to walk
  // or run sessions

  const uint16_t max_apart_merge_secs = 5 * PBL_SEC_PER_MIN;
  time_t end_time = previous->start_utc + previous->length_min * PBL_SEC_PER_MIN;
  if ((end_time + max_apart_merge_secs) > current->start_utc) {
    current->length_min += previous->length_min + (current->start_utc - end_time) / PBL_SEC_PER_MIN;
    current->start_utc = previous->start_utc;
    previous->length_min = 0;
    previous->type = ActivitySessionType_None;
  }
}

// API Functions
////////////////////////////////////////////////////////////////////////////////////////////////////

HealthData *health_data_create(void) {
  return (HealthData *)app_zalloc_check(sizeof(HealthData));
}

void health_data_destroy(HealthData *health_data) {
  app_free(health_data);
}

void health_data_update_quick(HealthData *health_data) {
  const time_t now = pbl_realtime();
  struct pbl_tm local_tm;
  pbl_localtime_r(&now, &local_tm);

  // Get the current steps
  health_service_private_get_metric_history(HealthMetricStepCount, 1, health_data->step_data);

  // Get the typical step averages for every 15 minutes
  activity_get_step_averages(local_tm.tm_wday, &health_data->step_averages);

  health_data->current_hr_bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  // Get the most recent stable HR Reading timestamp.
  activity_get_metric(ActivityMetricHeartRateFilteredUpdatedTimeUTC, 1,
                      (int32_t *)&health_data->hr_last_updated);
}

void health_data_update(HealthData *health_data) {
  const time_t now = pbl_realtime();
  struct pbl_tm local_tm;
  pbl_localtime_r(&now, &local_tm);

  //! Step / activity related data
  // Get the step totals for today and the days before it
  health_service_private_get_metric_history(HealthMetricStepCount, HEALTH_DATA_NUM_DAYS,
                                            health_data->step_data);
  // Update distance / calories now that we have our steps
  health_data_update_step_derived_metrics(health_data);

  // Get the step averages for each 15 minute window. Used for typical steps
  activity_get_step_averages(local_tm.tm_wday, &health_data->step_averages);

  // Get the average steps for the past month
  activity_get_metric_monthly_avg(ActivityMetricStepCount, &health_data->monthly_step_average);

  //! Sleep related data
  health_service_private_get_metric_history(HealthMetricSleepSeconds, HEALTH_DATA_NUM_DAYS,
                                            health_data->sleep_data);
  // Check if we have sleep data for today. If not, we want to show the last sleep session
  // (yesterday's data)
  bool use_yesterday = (health_data->sleep_data[0] == 0 && health_data->sleep_data[1] > 0);
  int day_offset = use_yesterday ? 1 : 0;
  int wday = (local_tm.tm_wday - day_offset + 7) % 7;

  activity_get_metric_typical(ActivityMetricSleepTotalSeconds, wday, &health_data->typical_sleep);

  activity_get_metric(ActivityMetricSleepRestfulSeconds, HEALTH_DATA_NUM_DAYS,
                      health_data->deep_sleep_data);
  health_data->deep_sleep = health_data->deep_sleep_data[day_offset];

  int32_t sleep_start_history[2];
  activity_get_metric(ActivityMetricSleepEnterAtSeconds, 2, sleep_start_history);
  health_data->sleep_start = sleep_start_history[day_offset];

  int32_t sleep_end_history[2];
  activity_get_metric(ActivityMetricSleepExitAtSeconds, 2, sleep_end_history);
  health_data->sleep_end = sleep_end_history[day_offset];

  activity_get_metric_typical(ActivityMetricSleepEnterAtSeconds, wday,
                              &health_data->typical_sleep_start);
  activity_get_metric_typical(ActivityMetricSleepExitAtSeconds, wday,
                              &health_data->typical_sleep_end);
  activity_get_metric_monthly_avg(ActivityMetricSleepTotalSeconds,
                                  &health_data->monthly_sleep_average);

  //! Activity sessions
  health_data->num_activity_sessions = ACTIVITY_MAX_ACTIVITY_SESSIONS_COUNT;
  if (!activity_get_sessions(&health_data->num_activity_sessions, health_data->activity_sessions)) {
    PBL_LOG_ERR("Fetching activity sessions failed");
  } else {
    ActivitySession *previous_session = nullptr;
    for (unsigned int i = 0; i < health_data->num_activity_sessions; i++) {
      ActivitySession *session = &health_data->activity_sessions[i];
      prv_merge_adjacent_sessions(session, previous_session);
      previous_session = session;
    }
  }

  //! HR related data
  health_data_update_current_bpm(health_data);
  health_data_update_hr_zone_minutes(health_data);
}

void health_data_update_step_derived_metrics(HealthData *health_data) {
  // get distance in meters
  health_data->current_distance_meters = health_service_sum_today(HealthMetricWalkedDistanceMeters);

  // get calories
  health_data->current_calories = health_service_sum_today(HealthMetricActiveKCalories) +
                                  health_service_sum_today(HealthMetricRestingKCalories);

  health_data->current_active_seconds = health_service_sum_today(HealthMetricActiveSeconds);
}

void health_data_update_steps(HealthData *health_data, uint32_t new_steps) {
  health_data->step_data[0] = new_steps;
  health_data_update_step_derived_metrics(health_data);
}

void health_data_update_sleep(HealthData *health_data, uint32_t new_sleep,
                              uint32_t new_deep_sleep) {
  health_data->sleep_data[0] = new_sleep;
  health_data->deep_sleep = new_deep_sleep;
  health_data->deep_sleep_data[0] = new_deep_sleep;
}

void health_data_update_current_bpm(HealthData *health_data) {
  health_data->resting_hr_bpm = activity_prefs_heart_get_resting_hr();

  uint32_t current_hr_timestamp;
  activity_get_metric(ActivityMetricHeartRateRawUpdatedTimeUTC, 1,
                      (int32_t *)&current_hr_timestamp);
  if (current_hr_timestamp > (uint32_t)health_data->hr_last_updated) {
    health_data->current_hr_bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    health_data->hr_last_updated = current_hr_timestamp;
  }
}

uint16_t health_data_hr_get_today_bpm(uint8_t *minute_bpm) {
  memset(minute_bpm, 0, PBL_MIN_PER_DAY);

  const time_t midnight = time_start_of_today();
  const time_t now = pbl_realtime();
  const uint16_t num_minutes = time_util_get_minute_of_day(now) + 1;
  const uint32_t num_utc_minutes = ((now - midnight) / PBL_SEC_PER_MIN) + 1;
  if (num_utc_minutes == num_minutes) {
    activity_get_minute_heart_rate(minute_bpm, num_minutes, midnight);
    return num_minutes;
  }

  // The clock skipped or repeated an hour today, so move each reading to its local clock minute
  // for it to line up with the hour labels
  uint8_t *utc_bpm = app_malloc_check(num_utc_minutes);
  if (activity_get_minute_heart_rate(utc_bpm, num_utc_minutes, midnight)) {
    for (uint32_t i = 0; i < num_utc_minutes; i++) {
      const int minute = time_util_get_minute_of_day(midnight + (time_t)i * PBL_SEC_PER_MIN);
      if (utc_bpm[i] && minute < num_minutes) {
        minute_bpm[minute] = utc_bpm[i];
      }
    }
  }
  app_free(utc_bpm);
  return num_minutes;
}

void health_data_update_hr_zone_minutes(HealthData *health_data) {
  activity_get_metric(ActivityMetricHeartRateZone1Minutes, 1, &health_data->hr_zone1_minutes);
  activity_get_metric(ActivityMetricHeartRateZone2Minutes, 1, &health_data->hr_zone2_minutes);
  activity_get_metric(ActivityMetricHeartRateZone3Minutes, 1, &health_data->hr_zone3_minutes);
}

int32_t *health_data_steps_get(HealthData *health_data) {
  return health_data->step_data;
}

int32_t health_data_current_steps_get(HealthData *health_data) {
  return health_data->step_data[0];
}

int32_t health_data_current_distance_meters_get(HealthData *health_data) {
  return health_data->current_distance_meters;
}

int32_t health_data_current_calories_get(HealthData *health_data) {
  return health_data->current_calories;
}

int32_t health_data_current_active_seconds_get(HealthData *health_data) {
  return health_data->current_active_seconds;
}

static int32_t prv_health_data_get_n_average_chunks(HealthData *health_data, int number_of_chunks) {
  uint32_t total_steps_avg = 0;
  for (int i = 0; (i < ACTIVITY_NUM_METRIC_AVERAGES) && (i < number_of_chunks); i++) {
    if (health_data->step_averages.average[i] != ACTIVITY_METRIC_AVERAGES_UNKNOWN) {
      total_steps_avg += health_data->step_averages.average[i];
    }
  }
  return total_steps_avg;
}

int32_t health_data_steps_get_current_average(HealthData *health_data) {
  // get the current minutes into today
  time_t utc_sec = pbl_realtime();
  struct pbl_tm local_tm;
  pbl_localtime_r(&utc_sec, &local_tm);
  int32_t today_min = local_tm.tm_hour * PBL_MIN_PER_HOUR + local_tm.tm_min;
  const int k_minutes_per_step_avg = PBL_MIN_PER_DAY / ACTIVITY_NUM_METRIC_AVERAGES;

  // each average chunk is 15 mins long
  if (health_data->step_average_last_updated_time !=
      ((today_min / k_minutes_per_step_avg) * k_minutes_per_step_avg)) {
    // current_step_average is stale
    health_data->current_step_average =
        prv_health_data_get_n_average_chunks(health_data, today_min / k_minutes_per_step_avg);
    health_data->step_average_last_updated_time =
        (today_min / k_minutes_per_step_avg) * k_minutes_per_step_avg;
  }
  return health_data->current_step_average;
}

int32_t health_data_steps_get_current_average_minute(HealthData *health_data) {
  return health_data->step_average_last_updated_time;
}

int32_t health_data_steps_get_cur_wday_average(HealthData *health_data) {
  return prv_health_data_get_n_average_chunks(health_data, ACTIVITY_NUM_METRIC_AVERAGES);
}

int32_t health_data_steps_get_monthly_average(HealthData *health_data) {
  return health_data->monthly_step_average;
}

int32_t *health_data_sleep_get(HealthData *health_data) {
  return health_data->sleep_data;
}

int32_t health_data_current_sleep_get(HealthData *health_data) {
  if (health_data->sleep_data[0] == 0 && health_data->sleep_data[1] > 0) {
    return health_data->sleep_data[1];
  }
  return health_data->sleep_data[0];
}

int32_t health_data_sleep_get_cur_wday_average(HealthData *health_data) {
  return health_data->typical_sleep;
}

int32_t health_data_current_deep_sleep_get(HealthData *health_data) {
  return health_data->deep_sleep;
}

int32_t *health_data_deep_sleep_get(HealthData *health_data) {
  return health_data->deep_sleep_data;
}

//! A metric's value on the day \a days_ago days before today, 0 if it has none
static PBL_NOINLINE int32_t prv_metric_on_day(ActivityMetric metric, int days_ago) {
  int32_t history[HEALTH_DATA_NUM_DAYS] = {};
  if (!activity_get_metric(metric, days_ago + 1, history)) {
    return 0;
  }
  return PBL_MAX(history[days_ago], 0);
}

//! Reads what a day before today has beyond the history the health data keeps, apart from the
//! drawing that shows today's, which has it all
static PBL_NOINLINE void prv_load_past_day(HealthDayData *day) {
  day->distance_meters = prv_metric_on_day(ActivityMetricDistanceMeters, day->days_ago);
  day->active_seconds = prv_metric_on_day(ActivityMetricActiveSeconds, day->days_ago);

  const time_t time = pbl_realtime() - day->days_ago * PBL_SEC_PER_DAY;
  struct pbl_tm local_tm;
  pbl_localtime_r(&time, &local_tm);
  ActivityMetricAverages averages;
  if (activity_get_step_averages(local_tm.tm_wday, &averages)) {
    for (int i = 0; i < ACTIVITY_NUM_METRIC_AVERAGES; i++) {
      if (averages.average[i] != ACTIVITY_METRIC_AVERAGES_UNKNOWN) {
        day->typical_steps += averages.average[i];
      }
    }
  }
  if (!activity_get_metric_typical(ActivityMetricSleepTotalSeconds, local_tm.tm_wday,
                                   &day->typical_sleep)) {
    day->typical_sleep = 0;
  }
}

void health_data_day_get(HealthData *health_data, int days_ago, HealthDayData *day_out) {
  days_ago = PBL_CLAMP(days_ago, 0, HEALTH_DATA_NUM_DAYS - 1);
  if (days_ago == 0) {
    *day_out = (HealthDayData){
      .steps = health_data_current_steps_get(health_data),
      .typical_steps_by_now = health_data_steps_get_current_average(health_data),
      .typical_steps = health_data_steps_get_cur_wday_average(health_data),
      .distance_meters = health_data->current_distance_meters,
      .active_seconds = health_data->current_active_seconds,
      .sleep = health_data_current_sleep_get(health_data),
      .deep_sleep = health_data->deep_sleep,
      .typical_sleep = health_data->typical_sleep,
    };
    return;
  }
  *day_out = (HealthDayData){
    .days_ago = days_ago,
    .steps = PBL_MAX(health_data->step_data[days_ago], 0),
    .sleep = PBL_MAX(health_data->sleep_data[days_ago], 0),
    .deep_sleep = PBL_MAX(health_data->deep_sleep_data[days_ago], 0),
  };
  prv_load_past_day(day_out);
}

//! The latest walk, run or open workout the watch detected that is still going on
static PBL_NOINLINE bool prv_find_detected_session(ActivitySession *session_out) {
  uint32_t num_sessions = ACTIVITY_MAX_ACTIVITY_SESSIONS_COUNT;
  ActivitySession *sessions = app_zalloc_check(sizeof(ActivitySession) * num_sessions);
  bool found = false;
  if (activity_get_sessions(&num_sessions, sessions)) {
    for (int i = (int)num_sessions - 1; i >= 0; i--) {
      if (sessions[i].ongoing && workout_service_is_workout_type_supported(sessions[i].type)) {
        *session_out = sessions[i];
        found = true;
        break;
      }
    }
  }
  app_free(sessions);
  return found;
}

void health_data_update_workout(HealthData *health_data) {
  HealthWorkoutData *workout = &health_data->workout;
  ActivitySessionType type;
  WorkoutServiceMetrics metrics;
  if (workout_service_get_current_workout_type(&type) &&
      workout_service_get_current_workout_metrics(&metrics)) {
    // A detected session turned into a workout of its kind was detected
    const bool auto_detected =
        (workout->state != HealthWorkoutStateNone) && (workout->type == type) &&
        ((workout->state == HealthWorkoutStateDetected) || workout->auto_detected);
    *workout = (HealthWorkoutData){
      .state = workout_service_is_paused() ? HealthWorkoutStatePaused : HealthWorkoutStateRunning,
      .type = type,
      .auto_detected = auto_detected,
      .duration_s = metrics.duration_s,
      .distance_m = metrics.distance_m,
      .steps = metrics.steps,
      .active_kcal = metrics.active_kcal,
    };
    return;
  }
  ActivitySession session;
  if (prv_find_detected_session(&session)) {
    *workout = (HealthWorkoutData){
      .state = HealthWorkoutStateDetected,
      .type = session.type,
      .auto_detected = true,
      .duration_s = PBL_MAX(pbl_realtime() - session.start_utc, 0),
      .distance_m = session.step_data.distance_meters,
      .steps = session.step_data.steps,
      .active_kcal = session.step_data.active_kcalories,
      .session = session,
    };
    return;
  }
  *workout = (HealthWorkoutData){};
}

const HealthWorkoutData *health_data_workout_get(HealthData *health_data) {
  return &health_data->workout;
}

int32_t health_data_sleep_get_monthly_average(HealthData *health_data) {
  return health_data->monthly_sleep_average;
}

int32_t health_data_sleep_get_start_time(HealthData *health_data) {
  return health_data->sleep_start;
}

int32_t health_data_sleep_get_end_time(HealthData *health_data) {
  return health_data->sleep_end;
}

int32_t health_data_sleep_get_typical_start_time(HealthData *health_data) {
  return health_data->typical_sleep_start;
}

int32_t health_data_sleep_get_typical_end_time(HealthData *health_data) {
  return health_data->typical_sleep_end;
}

int32_t health_data_sleep_get_num_sessions(HealthData *health_data) {
  return health_data->num_activity_sessions;
}

ActivitySession *health_data_sleep_get_sessions(HealthData *health_data) {
  return health_data->activity_sessions;
}

uint32_t health_data_hr_get_current_bpm(HealthData *health_data) {
  return health_data->current_hr_bpm;
}

uint32_t health_data_hr_get_resting_bpm(HealthData *health_data) {
  return health_data->resting_hr_bpm;
}

time_t health_data_hr_get_last_updated_timestamp(HealthData *health_data) {
  return health_data->hr_last_updated;
}

int32_t health_data_hr_get_zone1_minutes(HealthData *health_data) {
  return health_data->hr_zone1_minutes;
}

int32_t health_data_hr_get_zone2_minutes(HealthData *health_data) {
  return health_data->hr_zone2_minutes;
}

int32_t health_data_hr_get_zone3_minutes(HealthData *health_data) {
  return health_data->hr_zone3_minutes;
}
