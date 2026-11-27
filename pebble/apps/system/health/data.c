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
#include <pbl/util/size.h>
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

////////////////////////////////////////////////////////////////////////////////////////////////////
// Walks and deep sleep

//! The activity algorithm's walk and run detectors: the steps a minute each counts as active, how
//! long a walk or run lasts at least, and how many minutes below that it goes on through
#define WALK_MIN_STEPS_PER_MIN             40
#define RUN_MIN_STEPS_PER_MIN              130
#define STEP_ACTIVITY_MIN_MINUTES          10
#define STEP_ACTIVITY_MAX_INACTIVE_MINUTES 7

void health_data_spans_add(HealthSpans *spans, int32_t from, int32_t to) {
  from = PBL_CLAMP(from, 0, UINT16_MAX);
  to = PBL_CLAMP(to, from, UINT16_MAX);
  if (to <= from) {
    return;
  }
  // The stretch takes in those it touches, the others keep their order round it
  HealthSpan span = {from, to};
  HealthSpan kept[HEALTH_DATA_MAX_SPANS + 1];
  int count = 0;
  for (int i = 0; i < spans->count; i++) {
    const HealthSpan *other = &spans->spans[i];
    if (other->to < span.from || other->from > span.to) {
      kept[count++] = *other;
    } else {
      span.from = PBL_MIN(span.from, other->from);
      span.to = PBL_MAX(span.to, other->to);
    }
  }
  int at = count;
  while (at > 0 && kept[at - 1].from > span.from) {
    kept[at] = kept[at - 1];
    at--;
  }
  kept[at] = span;
  count++;
  if (count > HEALTH_DATA_MAX_SPANS) {
    // The two closest together become one
    int closest = 0;
    for (int i = 1; i < count - 1; i++) {
      if (kept[i + 1].from - kept[i].to < kept[closest + 1].from - kept[closest].to) {
        closest = i;
      }
    }
    kept[closest].to = kept[closest + 1].to;
    count--;
    memmove(&kept[closest + 1], &kept[closest + 2], (count - closest - 1) * sizeof(HealthSpan));
  }
  spans->count = count;
  memcpy(spans->spans, kept, count * sizeof(HealthSpan));
}

//! A walk or run detector going through a day's minutes
typedef struct StepActivity {
  uint8_t min_steps;
  uint8_t max_steps;
  //! First and last active minute of the walk or run going on, start -1 if none
  int32_t start;
  int32_t last;
  uint8_t inactive;
  //! The day's steps before its first minute and by the end of its last
  uint32_t steps_before;
  uint32_t steps_by_end;
} StepActivity;

static void prv_end_step_activity(StepActivity *activity, uint32_t total, int32_t day_steps,
                                  HealthSpans *walks) {
  if (activity->start >= 0 && activity->last + 1 - activity->start >= STEP_ACTIVITY_MIN_MINUTES) {
    health_data_spans_add(walks, (int64_t)activity->steps_before * day_steps / total,
                          (int64_t)activity->steps_by_end * day_steps / total);
  }
  activity->start = -1;
  activity->inactive = 0;
}

void health_data_find_walks(const uint8_t *minute_steps, uint32_t num_minutes, int32_t day_steps,
                            HealthSpans *walks_out) {
  *walks_out = (HealthSpans){};
  uint32_t total = 0;
  for (uint32_t i = 0; i < num_minutes; i++) {
    total += minute_steps[i];
  }
  if (total == 0 || day_steps <= 0) {
    return;
  }
  // The walk and run detectors run side by side, a minute fast enough for both counting for both
  StepActivity activities[] = {
    {.min_steps = WALK_MIN_STEPS_PER_MIN, .max_steps = RUN_MIN_STEPS_PER_MIN, .start = -1},
    {.min_steps = RUN_MIN_STEPS_PER_MIN, .max_steps = UINT8_MAX, .start = -1},
  };
  uint32_t steps = 0;
  for (uint32_t i = 0; i < num_minutes; i++) {
    const uint8_t minute = minute_steps[i];
    const uint32_t before = steps;
    steps += minute;
    for (size_t k = 0; k < PBL_ARRAY_SIZE(activities); k++) {
      StepActivity *activity = &activities[k];
      if (minute >= activity->min_steps && minute <= activity->max_steps) {
        if (activity->start < 0) {
          activity->start = i;
          activity->steps_before = before;
        }
        activity->last = i;
        activity->inactive = 0;
        activity->steps_by_end = steps;
      } else if (activity->start >= 0 &&
                 ++activity->inactive > STEP_ACTIVITY_MAX_INACTIVE_MINUTES) {
        prv_end_step_activity(activity, total, day_steps, walks_out);
      }
    }
  }
  // Those going on at the end of the minutes, as the algorithm reports them going on
  for (size_t k = 0; k < PBL_ARRAY_SIZE(activities); k++) {
    prv_end_step_activity(&activities[k], total, day_steps, walks_out);
  }
}

//! Minutes of a night's sleep before \a utc
static int32_t prv_minutes_slept_by(const ActivitySession *sessions, uint32_t num_sessions,
                                    time_t utc) {
  int32_t seconds = 0;
  for (uint32_t i = 0; i < num_sessions; i++) {
    const ActivitySession *session = &sessions[i];
    if (session->type == ActivitySessionType_Sleep || session->type == ActivitySessionType_Nap) {
      seconds += PBL_CLAMP(utc - session->start_utc, 0, session->length_min * PBL_SEC_PER_MIN);
    }
  }
  return seconds / PBL_SEC_PER_MIN;
}

void health_data_find_deep_sleep(const ActivitySession *sessions, uint32_t num_sessions,
                                 HealthSpans *deep_out) {
  *deep_out = (HealthSpans){};
  for (uint32_t i = 0; i < num_sessions; i++) {
    const ActivitySession *session = &sessions[i];
    if (session->type == ActivitySessionType_RestfulSleep ||
        session->type == ActivitySessionType_RestfulNap) {
      const time_t end_utc = session->start_utc + session->length_min * PBL_SEC_PER_MIN;
      health_data_spans_add(deep_out,
                            prv_minutes_slept_by(sessions, num_sessions, session->start_utc),
                            prv_minutes_slept_by(sessions, num_sessions, end_utc));
    }
  }
}

#if PBL_ROUND
//! Reads the walks and runs of the day \a days_ago days before today from its minutes, today's to
//! now, none if the watch no longer has them
static PBL_NOINLINE void prv_read_walks(int days_ago, int32_t day_steps, HealthSpans *walks_out) {
  *walks_out = (HealthSpans){};
  if (day_steps <= 0) {
    return;
  }
  // From noon, a day away from midnight whichever way the clock changed
  const time_t noon = time_start_of_today() - days_ago * PBL_SEC_PER_DAY + PBL_SEC_PER_DAY / 2;
  const time_t start = time_util_get_midnight_of(noon);
  const time_t end =
      days_ago ? time_util_get_midnight_of(noon + PBL_SEC_PER_DAY) : pbl_realtime() + 1;
  const uint32_t num_minutes = PBL_MIN((end - start + PBL_SEC_PER_MIN - 1) / PBL_SEC_PER_MIN,
                                       ACTIVITY_MINUTE_STEPS_MAX_MINUTES);
  uint8_t *minute_steps = app_malloc(num_minutes);
  if (!minute_steps) {
    return;
  }
  if (activity_get_minute_steps(minute_steps, num_minutes, start)) {
    health_data_find_walks(minute_steps, num_minutes, day_steps, walks_out);
  }
  app_free(minute_steps);
}

//! Forgets the walks read once it is a new day
static void prv_check_walks_day(HealthData *health_data) {
  const time_t midnight = time_start_of_today();
  if (midnight != health_data->walks_midnight) {
    health_data->walks_read = 0;
    health_data->walks_midnight = midnight;
  }
}

void health_data_update_walks(HealthData *health_data) {
  prv_check_walks_day(health_data);
  const int32_t steps = health_data->step_data[0];
  if ((health_data->walks_read & 1) && steps == health_data->walks_steps) {
    return;
  }
  prv_read_walks(0, steps, &health_data->walks[0]);
  health_data->walks_read |= 1;
  health_data->walks_steps = steps;
}

//! Reads the walks and runs of a day of the week before today if not yet read
static void prv_read_week_day_walks(HealthData *health_data, int days_ago) {
  if (!(health_data->walks_read & (1 << days_ago))) {
    prv_read_walks(days_ago, health_data->step_data[days_ago], &health_data->walks[days_ago]);
    health_data->walks_read |= (1 << days_ago);
  }
}

void health_data_read_week_walks(HealthData *health_data) {
  prv_check_walks_day(health_data);
  for (int i = 1; i < HEALTH_DATA_WALK_DAYS; i++) {
    prv_read_week_day_walks(health_data, i);
  }
}

const HealthSpans *health_data_walks_get(HealthData *health_data, int days_ago) {
  if (days_ago < 0 || days_ago >= HEALTH_DATA_WALK_DAYS ||
      !(health_data->walks_read & (1 << days_ago))) {
    return nullptr;
  }
  return &health_data->walks[days_ago];
}

const HealthSpans *health_data_deep_sleep_spans_get(HealthData *health_data) {
  return &health_data->deep_sleep_spans;
}
#endif

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

#if PBL_ROUND
  // Deep sleep is only drawn where it happened in the night the sessions are of
  if (health_data->sleep_data[0] > 0) {
    health_data_find_deep_sleep(health_data->activity_sessions, health_data->num_activity_sessions,
                                &health_data->deep_sleep_spans);
  } else {
    health_data->deep_sleep_spans = (HealthSpans){};
  }
  health_data_update_walks(health_data);
#endif

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
#if PBL_ROUND
    if (health_data->walks_read & 1) {
      day_out->walks = health_data->walks[0];
    }
    if (health_data->sleep_data[0] > 0) {
      day_out->deep_sleep_spans = health_data->deep_sleep_spans;
    }
#endif
    return;
  }
  *day_out = (HealthDayData){
    .days_ago = days_ago,
    .steps = PBL_MAX(health_data->step_data[days_ago], 0),
    .sleep = PBL_MAX(health_data->sleep_data[days_ago], 0),
    .deep_sleep = PBL_MAX(health_data->deep_sleep_data[days_ago], 0),
  };
  prv_load_past_day(day_out);
#if PBL_ROUND
  // The minute file holds under a week of minutes, so only the days of the week can have walks.
  // The sessions of the nights before last night are gone, so their deep sleep has no times.
  if (days_ago < HEALTH_DATA_WALK_DAYS) {
    prv_check_walks_day(health_data);
    prv_read_week_day_walks(health_data, days_ago);
    day_out->walks = health_data->walks[days_ago];
  }
#endif
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
