/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "fake_pfs.h"
#include "test_health_app_includes.h"

#include <pbl/services/activity/workout_service.h>

#include <apps/system/health/data_private.h>

// The view's sections are chosen by its buttons, so the test drives it from the inside
#include <apps/system/health/orbit_view.c>

// Fakes
////////////////////////////////////

static uint32_t s_launch_args;

uint32_t app_launch_get_args(void) {
  return s_launch_args;
}

bool clock_is_24h_style(void) {
  return false;
}

//! What the health data does not keep, from today back: a week, then the same again each week
static const int32_t s_distance_history[] = {5600, 5100, 5750, 3540, 8200, 4430, 7020};
static const int32_t s_active_minutes_history[] = {47, 38, 52, 21, 74, 33, 61};

static int32_t prv_distance_on(int day) {
  return s_distance_history[day % PBL_DAY_PER_WEEK];
}

static int32_t prv_active_seconds_on(int day) {
  return s_active_minutes_history[day % PBL_DAY_PER_WEEK] * PBL_SEC_PER_MIN;
}

bool activity_get_metric(ActivityMetric metric, uint32_t history_len, int32_t *history) {
  cl_assert(history_len <= ACTIVITY_HISTORY_DAYS);
  for (uint32_t i = 0; i < history_len; i++) {
    switch (metric) {
      case ActivityMetricDistanceMeters:
        history[i] = prv_distance_on(i);
        break;
      case ActivityMetricActiveSeconds:
        history[i] = prv_active_seconds_on(i);
        break;
      default:
        history[i] = 0;
        break;
    }
  }
  return true;
}

//! 9,120 steps on a typical day, 95 every quarter hour
bool activity_get_step_averages(enum pbl_weekday day_of_week, ActivityMetricAverages *averages) {
  for (int i = 0; i < ACTIVITY_NUM_METRIC_AVERAGES; i++) {
    averages->average[i] = 95;
  }
  return true;
}

bool activity_get_metric_typical(ActivityMetric metric, enum pbl_weekday day, int32_t *value_out) {
  // 7h 25m of sleep on a typical night
  *value_out = (metric == ActivityMetricSleepTotalSeconds) ? (7 * 60 + 25) * PBL_SEC_PER_MIN : 0;
  return true;
}

//! The workout service, and the sessions the watch detected
static struct {
  bool ongoing;
  bool paused;
  ActivitySessionType type;
  WorkoutServiceMetrics metrics;
  int starts;
  int stops;
  int takeovers;
} s_workout;
static ActivitySession s_sessions[1];
static uint32_t s_num_sessions;

bool workout_service_is_workout_ongoing(void) {
  return s_workout.ongoing;
}

bool workout_service_is_workout_type_supported(ActivitySessionType type) {
  return (type == ActivitySessionType_Walk) || (type == ActivitySessionType_Run) ||
         (type == ActivitySessionType_Open);
}

bool workout_service_start_workout(ActivitySessionType type) {
  if (s_workout.ongoing) {
    return false;
  }
  s_workout.ongoing = true;
  s_workout.paused = false;
  s_workout.type = type;
  s_workout.metrics = (WorkoutServiceMetrics){};
  s_workout.starts++;
  return true;
}

bool workout_service_pause_workout(bool should_be_paused) {
  if (!s_workout.ongoing) {
    return false;
  }
  s_workout.paused = should_be_paused;
  return true;
}

bool workout_service_stop_workout(void) {
  if (!s_workout.ongoing) {
    return false;
  }
  s_workout.ongoing = false;
  s_workout.paused = false;
  s_workout.stops++;
  return true;
}

bool workout_service_takeover_activity_session(ActivitySession *session) {
  s_workout.takeovers++;
  s_num_sessions = 0;
  if (!workout_service_start_workout(session->type)) {
    return false;
  }
  s_workout.metrics = (WorkoutServiceMetrics){
    .duration_s = session->length_min * PBL_SEC_PER_MIN,
    .steps = session->step_data.steps,
    .distance_m = session->step_data.distance_meters,
    .active_kcal = session->step_data.active_kcalories,
  };
  return true;
}

bool workout_service_is_paused(void) {
  return s_workout.ongoing && s_workout.paused;
}

bool workout_service_get_current_workout_type(ActivitySessionType *type_out) {
  if (s_workout.ongoing) {
    *type_out = s_workout.type;
  }
  return s_workout.ongoing;
}

bool workout_service_get_current_workout_metrics(WorkoutServiceMetrics *metrics_out) {
  if (s_workout.ongoing) {
    *metrics_out = s_workout.metrics;
  }
  return s_workout.ongoing;
}

bool activity_get_sessions(uint32_t *session_entries, ActivitySession *sessions) {
  *session_entries = PBL_MIN(*session_entries, s_num_sessions);
  memcpy(sessions, s_sessions, *session_entries * sizeof(ActivitySession));
  return true;
}

// Setup and Teardown
////////////////////////////////////

static GContext s_ctx;
static FrameBuffer s_fb;

GContext *graphics_context_get_current_context(void) {
  return &s_ctx;
}

//! Wednesday 25 November 2026, 15:45
#define NOW_UTC 1795621500

void test_health_orbit_view__initialize(void) {
  pbl_realtime_set(NOW_UTC);
  s_launch_args = 0;
  s_workout = (typeof(s_workout)){};
  s_num_sessions = 0;

  framebuffer_init(&s_fb, &(GSize){PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT});
  framebuffer_clear(&s_fb);
  graphics_context_init(&s_ctx, &s_fb, GContextInitializationMode_App);
  s_app_state_get_graphics_context = &s_ctx;

  fake_spi_flash_init(0 /* offset */, 0x1000000 /* length */);
  fake_pfs_mount();
  pbl_fs_format("/");
  load_resource_fixture_in_flash(RESOURCES_FIXTURE_PATH, SYSTEM_RESOURCES_FIXTURE_NAME,
                                 false /* is_next */);
  resource_init();
}

static HealthOrbitView *s_view;

void test_health_orbit_view__cleanup(void) {
  if (s_view) {
    health_orbit_view_destroy(s_view);
    s_view = nullptr;
  }
}

// Helpers
//////////////////////

//! Sets the typical steps by 15:45, the 63 quarter hours before it, and over the whole day
static void prv_set_typical_steps(HealthData *data, int32_t by_now, int32_t whole_day) {
  const int now_quarter = 63;
  const int rest = ACTIVITY_NUM_METRIC_AVERAGES - now_quarter;
  data->step_averages.average[0] = by_now;
  for (int i = now_quarter; i < ACTIVITY_NUM_METRIC_AVERAGES; i++) {
    data->step_averages.average[i] = (whole_day - by_now) / rest;
  }
  data->step_averages.average[now_quarter] += (whole_day - by_now) % rest;
}

#define HOURS(h, m) (((h) * PBL_MIN_PER_HOUR + (m)) * PBL_SEC_PER_MIN)

//! A Wednesday afternoon's data: steps ahead of typical, a night shorter than typical
static HealthData prv_sample_data(void) {
  HealthData data = {
    .step_data = {7842, 7120, 8015, 4950, 11430, 6204, 9812},
    .current_distance_meters = 5600,
    .current_calories = 312,
    .current_active_seconds = 47 * PBL_SEC_PER_MIN,
    .monthly_step_average = 8455,
    .sleep_data =
        {HOURS(6, 52), HOURS(7, 20), HOURS(5, 56), HOURS(7, 48), HOURS(8, 31), HOURS(6, 15),
         HOURS(7, 2)},
    .typical_sleep = HOURS(7, 25),
    .deep_sleep = HOURS(1, 40),
    .deep_sleep_data =
        {HOURS(1, 40), HOURS(1, 28), HOURS(1, 0), HOURS(1, 45), HOURS(2, 0), HOURS(1, 10),
         HOURS(1, 35)},
    .sleep_start = -HOURS(0, 12),
    .sleep_end = HOURS(6, 40),
    .typical_sleep_start = -HOURS(0, 45),
    .typical_sleep_end = HOURS(6, 50),
    .monthly_sleep_average = HOURS(7, 10),
  };
  prv_set_typical_steps(&data, 6910, 9120);
  return data;
}

static void prv_render(HealthData *health_data, HealthOrbitSection section) {
  s_view = health_orbit_view_create(health_data);
  prv_show_section(s_view, section);
  window_set_on_screen(&s_view->window, true, true);
  window_render(&s_view->window, &s_ctx);
}

//! Shows the week at rest
static void prv_show_week(HealthOrbitView *view) {
  view->week = true;
  prv_rest(view);
}

// Tests
//////////////////////

static HealthData s_data;

void test_health_orbit_view__render_today(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionToday);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_today_no_data(void) {
  s_data = (HealthData){};
  prv_render(&s_data, HealthOrbitSectionToday);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_today_large(void) {
  s_data = prv_sample_data();
  s_data.step_data[0] = 98765;
  s_data.sleep_data[0] = HOURS(11, 58);
  s_data.deep_sleep = HOURS(10, 59);
  s_data.current_distance_meters = 88800;
  s_data.current_active_seconds = 888 * PBL_SEC_PER_MIN;
  prv_render(&s_data, HealthOrbitSectionToday);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

//! The sidebar turning up into the banner, \a eighths of the way, its notch closing as it goes
static void prv_render_turning(int eighths) {
  s_data = prv_sample_data();
  s_view = health_orbit_view_create(&s_data);
  prv_show_section(s_view, HealthOrbitSectionSteps);
  s_view->turn = -QUARTER_TURN * eighths / 8;
  window_set_on_screen(&s_view->window, true, true);
  window_render(&s_view->window, &s_ctx);
}

void test_health_orbit_view__render_steps_turning(void) {
  // Filled a row at a time across the notch
  prv_render_turning(2);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps_turned(void) {
  // Filled a column at a time across the notch, nearer the top
  prv_render_turning(6);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps_no_data(void) {
  s_data = (HealthData){};
  prv_render(&s_data, HealthOrbitSectionSteps);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps_large(void) {
  // The longest numbers fit their lines, pill and bubbles
  s_data = prv_sample_data();
  s_data.step_data[0] = 98765;
  prv_set_typical_steps(&s_data, 12345, 99999);
  s_data.current_distance_meters = 88800;
  s_data.current_calories = 4321;
  s_data.current_active_seconds = 888 * PBL_SEC_PER_MIN;
  prv_render(&s_data, HealthOrbitSectionSteps);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSleep);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep_no_data(void) {
  s_data = (HealthData){};
  prv_render(&s_data, HealthOrbitSectionSleep);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep_long(void) {
  // A long night, longer than typical, fits its lines, pill and bubbles
  s_data = prv_sample_data();
  s_data.sleep_data[0] = HOURS(11, 58);
  s_data.deep_sleep = HOURS(10, 59);
  s_data.typical_sleep = HOURS(8, 59);
  s_data.monthly_sleep_average = HOURS(10, 48);
  s_data.sleep_start = HOURS(20, 58);
  s_data.sleep_end = HOURS(8, 56);
  prv_render(&s_data, HealthOrbitSectionSleep);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep_nap(void) {
  // Under an hour, only the minutes
  s_data = prv_sample_data();
  s_data.sleep_data[0] = HOURS(0, 42);
  s_data.deep_sleep = HOURS(0, 9);
  s_data.sleep_start = HOURS(13, 5);
  s_data.sleep_end = HOURS(13, 47);
  prv_render(&s_data, HealthOrbitSectionSleep);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps_week(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_show_week(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep_week(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSleep);
  prv_show_week(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps_week_no_data(void) {
  s_data = (HealthData){};
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_show_week(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep_week_long(void) {
  // The longest values fit their rows, and the line above them the bezel
  s_data = prv_sample_data();
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    s_data.sleep_data[i] = HOURS(11, 58) - i * HOURS(0, 1);
  }
  s_data.typical_sleep = HOURS(10, 59);
  prv_render(&s_data, HealthOrbitSectionSleep);
  prv_show_week(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps_week_selected(void) {
  // Down moves the outline from today to the day before, then to the one before that
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_show_week(s_view);
  prv_move_selection(s_view, true /* down */);
  prv_move_selection(s_view, true /* down */);
  prv_rest(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_day(void) {
  // Select on Monday in the week of steps opens the days on Monday: its overview, laid out as the
  // Today card, and the orbit of days with Monday's bubble at the notch
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_show_week(s_view);
  s_view->week_selection = 2;
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_days_today(void) {
  // Select on the Today card keeps the card and swaps the sections for the days, today's a dot
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionToday);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  window_render(&s_view->window, &s_ctx);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__week_selection_stops_at_the_ends(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSleep);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(s_view->week);
  cl_assert_equal_i(s_view->week_selection, 0);
  // Up on today stays there
  prv_move_selection(s_view, false /* down */);
  cl_assert_equal_i(s_view->week_selection, 0);
  for (int i = 0; i < 2 * HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    prv_move_selection(s_view, true /* down */);
  }
  cl_assert_equal_i(s_view->week_selection, HEALTH_ORBIT_WEEK_NUM_ROWS - 1);
  // The outline moves from the row it was on
  prv_rest(s_view);
  prv_move_selection(s_view, false /* down */);
  cl_assert_equal_i(s_view->selection_from, HEALTH_ORBIT_WEEK_NUM_ROWS - 1);
  cl_assert_equal_i(s_view->week_selection, HEALTH_ORBIT_WEEK_NUM_ROWS - 2);
  cl_assert_equal_i(s_view->selection_travel, 0);
}

//! Presses a button and lets what it does finish
static void prv_press(void (*handler)(ClickRecognizerRef, void *), int times) {
  for (int i = 0; i < times; i++) {
    handler(nullptr, s_view);
    prv_rest(s_view);
  }
}

static void prv_down(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  prv_move_day(view, true /* down */);
}

static void prv_up(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  prv_move_day(view, false /* down */);
}

void test_health_orbit_view__days_open_from_today_and_back(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionToday);
  prv_press(prv_select_click_handler, 1);
  cl_assert(s_view->days);
  cl_assert_equal_i(s_view->day, 0);
  cl_assert_equal_i(s_view->swap, HEALTH_ORBIT_SWAP);
  // Today is the Today card itself
  cl_assert(s_view->card.day == nullptr);
  cl_assert_equal_i(s_view->card.section, HealthOrbitSectionToday);
  // Down goes back a day, the orbit of days turning a step
  prv_press(prv_down, 1);
  cl_assert_equal_i(s_view->day, 1);
  cl_assert_equal_i(s_view->day_offset, HEALTH_ORBIT_STEP);
  cl_assert(s_view->card.day == &s_view->day_data[s_view->day_slot]);
  cl_assert_equal_i(s_view->card.day->days_ago, 1);
  cl_assert_equal_i(s_view->card.day->steps, s_data.step_data[1]);
  // Select does nothing on a day
  prv_press(prv_select_click_handler, 1);
  cl_assert(s_view->days);
  cl_assert_equal_i(s_view->day, 1);
  // Up comes forward a day, and stops at today
  prv_press(prv_up, 3);
  cl_assert_equal_i(s_view->day, 0);
  cl_assert_equal_i(s_view->day_offset, 0);
  // Back swaps the days for the sections again, on the Today card
  prv_press(prv_back_click_handler, 1);
  cl_assert(!s_view->days);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionToday);
  cl_assert_equal_i(s_view->swap, 0);
  cl_assert_equal_i(s_view->neighbour_radius, NEIGHBOUR_BUBBLE_RADIUS);
  cl_assert(s_view->card.day == nullptr);
}

void test_health_orbit_view__days_swap_turns_as_the_buttons_say(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionToday);
  // In, clockwise, the days coming up from below with Today's colors
  prv_select_click_handler(nullptr, s_view);
  cl_assert(s_view->orbit_motion == &s_view->swap);
  cl_assert(s_view->orbit_down);
  cl_assert_equal_i(s_view->orbit_target, HEALTH_ORBIT_SWAP);
  // The card stays today's, so nothing slides
  cl_assert(!s_view->leaving_shown);
  prv_rest(s_view);
  // Down and Up turn the days as the sections turn
  prv_move_day(s_view, true /* down */);
  cl_assert(s_view->orbit_motion == &s_view->day_offset);
  cl_assert(s_view->orbit_down);
  cl_assert(s_view->leaving_shown);
  cl_assert(s_view->leaving.day == nullptr);
  prv_rest(s_view);
  prv_move_day(s_view, true /* down */);
  // The leaving card keeps its day as the next one is read
  cl_assert(s_view->leaving.day != s_view->card.day);
  cl_assert_equal_i(s_view->leaving.day->days_ago, 1);
  cl_assert_equal_i(s_view->card.day->days_ago, 2);
  prv_rest(s_view);
  // Back out, counterclockwise, the Today card sliding in from above as for Up
  prv_back_click_handler(nullptr, s_view);
  cl_assert(s_view->orbit_motion == &s_view->swap);
  cl_assert(!s_view->orbit_down);
  cl_assert_equal_i(s_view->orbit_target, 0);
  cl_assert(s_view->leaving_shown);
  cl_assert(s_view->card.offset_y < 0);
}

void test_health_orbit_view__days_stop_at_the_oldest_day_kept(void) {
  s_data = prv_sample_data();
  s_data.step_data[HEALTH_DATA_NUM_DAYS - 1] = 4321;
  s_data.sleep_data[HEALTH_DATA_NUM_DAYS - 1] = HOURS(6, 30);
  prv_render(&s_data, HealthOrbitSectionToday);
  prv_press(prv_select_click_handler, 1);
  prv_press(prv_down, HEALTH_DATA_NUM_DAYS + 5);
  cl_assert_equal_i(s_view->day, HEALTH_DATA_NUM_DAYS - 1);
  cl_assert_equal_i(HEALTH_DATA_NUM_DAYS, ACTIVITY_HISTORY_DAYS);
  // The oldest day has all its overview
  const HealthDayData *day = s_view->card.day;
  cl_assert_equal_i(day->days_ago, HEALTH_DATA_NUM_DAYS - 1);
  cl_assert_equal_i(day->steps, 4321);
  cl_assert_equal_i(day->sleep, HOURS(6, 30));
  cl_assert_equal_i(day->distance_meters, prv_distance_on(HEALTH_DATA_NUM_DAYS - 1));
  cl_assert_equal_i(day->active_seconds, prv_active_seconds_on(HEALTH_DATA_NUM_DAYS - 1));
  cl_assert_equal_i(day->typical_steps, 9120);
  // Down there turns nothing
  prv_move_day(s_view, true /* down */);
  cl_assert(s_view->animation == nullptr);
  // Up on today neither
  prv_press(prv_up, HEALTH_DATA_NUM_DAYS + 5);
  cl_assert_equal_i(s_view->day, 0);
  prv_move_day(s_view, false /* down */);
  cl_assert(s_view->animation == nullptr);
}

void test_health_orbit_view__day_opens_from_the_week_and_back(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_press(prv_select_click_handler, 1);
  s_view->week_selection = 4;
  // Select opens the days on the day selected, with Today's colors
  prv_press(prv_select_click_handler, 1);
  cl_assert(!s_view->week);
  cl_assert(s_view->days);
  cl_assert_equal_i(s_view->day, 4);
  cl_assert_equal_i(s_view->swap, HEALTH_ORBIT_SWAP);
  cl_assert_equal_i(s_view->color_section, HealthOrbitSectionToday);
  cl_assert(s_view->card.day == &s_view->day_data[s_view->day_slot]);
  cl_assert_equal_i(s_view->card.day->days_ago, 4);
  cl_assert_equal_i(s_view->card.day->steps, s_data.step_data[4]);
  cl_assert_equal_i(s_view->card.day->distance_meters, prv_distance_on(4));
  cl_assert_equal_i(s_view->card.day->active_seconds, prv_active_seconds_on(4));
  cl_assert_equal_i(s_view->card.day->typical_steps, 9120);
  // The same days as from the Today card
  prv_press(prv_down, 1);
  cl_assert_equal_i(s_view->day, 5);
  // Back goes back to the week with the day shown selected
  prv_press(prv_back_click_handler, 1);
  cl_assert(s_view->week);
  cl_assert(!s_view->days);
  cl_assert_equal_i(s_view->swap, 0);
  cl_assert_equal_i(s_view->color_section, HealthOrbitSectionSteps);
  cl_assert_equal_i(s_view->week_selection, 5);
  cl_assert_equal_i(s_view->neighbour_radius, 0);
  // And Back again to the section's card, the week starting on today the next time
  prv_press(prv_back_click_handler, 1);
  cl_assert(!s_view->week);
  cl_assert(s_view->card.day == nullptr);
  cl_assert_equal_i(s_view->card.section, HealthOrbitSectionSteps);
  prv_select_click_handler(nullptr, s_view);
  cl_assert_equal_i(s_view->week_selection, 0);
}

void test_health_orbit_view__back_from_a_day_before_the_week_selects_its_oldest_day(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_press(prv_select_click_handler, 1);
  s_view->week_selection = HEALTH_ORBIT_WEEK_NUM_ROWS - 1;
  prv_press(prv_select_click_handler, 1);
  prv_press(prv_down, 3);
  cl_assert_equal_i(s_view->day, HEALTH_ORBIT_WEEK_NUM_ROWS + 2);
  prv_press(prv_back_click_handler, 1);
  cl_assert(s_view->week);
  cl_assert_equal_i(s_view->week_selection, HEALTH_ORBIT_WEEK_NUM_ROWS - 1);
  // Today's row for today
  prv_press(prv_select_click_handler, 1);
  prv_press(prv_up, HEALTH_DATA_NUM_DAYS);
  prv_press(prv_back_click_handler, 1);
  cl_assert_equal_i(s_view->week_selection, 0);
}

void test_health_orbit_view__sleep_week_opens_the_day_the_night_ended(void) {
  // Row 2 of the Sleep week is the night that started on the evening three days ago, Sunday, and
  // ended on Monday morning: it opens Monday, its sleep that night's
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSleep);
  prv_press(prv_select_click_handler, 1);
  s_view->week_selection = 2;
  struct pbl_tm evening;
  health_orbit_day_time(2 + 1, &evening);
  cl_assert_equal_i(evening.tm_wday, PBL_SUNDAY);
  prv_press(prv_select_click_handler, 1);
  cl_assert(s_view->days);
  cl_assert_equal_i(s_view->day, 2);
  struct pbl_tm morning;
  health_orbit_day_time(s_view->day, &morning);
  cl_assert_equal_i(morning.tm_wday, PBL_MONDAY);
  cl_assert_equal_i(s_view->card.day->sleep, s_data.sleep_data[2]);
  cl_assert_equal_i(s_view->card.day->deep_sleep, s_data.deep_sleep_data[2]);
  // And back on the same row, in the Sleep week's colors
  prv_press(prv_back_click_handler, 1);
  cl_assert(s_view->week);
  cl_assert_equal_i(s_view->week_selection, 2);
  cl_assert_equal_i(s_view->color_section, HealthOrbitSectionSleep);
}

void test_health_orbit_view__day_reads_all_the_history_kept(void) {
  // The oldest day the watch keeps has all its overview, and none before it is read
  s_data = prv_sample_data();
  s_data.step_data[HEALTH_DATA_NUM_DAYS - 1] = 4321;
  s_data.sleep_data[HEALTH_DATA_NUM_DAYS - 1] = HOURS(6, 30);
  s_data.deep_sleep_data[HEALTH_DATA_NUM_DAYS - 1] = HOURS(1, 5);
  cl_assert_equal_i(HEALTH_DATA_NUM_DAYS, ACTIVITY_HISTORY_DAYS);
  for (int days_ago = HEALTH_DATA_NUM_DAYS - 1; days_ago <= HEALTH_DATA_NUM_DAYS; days_ago++) {
    HealthDayData day;
    health_data_day_get(&s_data, days_ago, &day);
    cl_assert_equal_i(day.days_ago, HEALTH_DATA_NUM_DAYS - 1);
    cl_assert_equal_i(day.steps, 4321);
    cl_assert_equal_i(day.sleep, HOURS(6, 30));
    cl_assert_equal_i(day.deep_sleep, HOURS(1, 5));
    cl_assert_equal_i(day.distance_meters, prv_distance_on(HEALTH_DATA_NUM_DAYS - 1));
    cl_assert_equal_i(day.active_seconds, prv_active_seconds_on(HEALTH_DATA_NUM_DAYS - 1));
    cl_assert_equal_i(day.typical_steps, 9120);
  }
}

void test_health_orbit_view__wrap_keeps_turning_the_same_way(void) {
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionWorkout);
  // Up on the first section, Workout, turns on round counterclockwise to the last, Sleep
  prv_up_down(s_view, false /* down */);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionSleep);
  cl_assert(s_view->orbit_target < s_view->orbit_from);
  cl_assert_equal_i(s_view->orbit_target + HEALTH_ORBIT_LENGTH,
                    HealthOrbitSectionSleep * HEALTH_ORBIT_STEP);
  prv_rest(s_view);
  // Down on the last turns on round clockwise to the first
  prv_up_down(s_view, true /* down */);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionWorkout);
  cl_assert(s_view->orbit_target > s_view->orbit_from);
  cl_assert_equal_i(s_view->orbit_target - HEALTH_ORBIT_LENGTH, 0);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->orbit_offset, 0);
}

void test_health_orbit_view__quick_launch_stops_at_workout(void) {
  // Opened with Up or Down, the sections stop at either end: Workout at the top
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionToday);
  s_view->wrap = false;
  prv_up_down(s_view, false /* down */);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionWorkout);
  prv_up_down(s_view, false /* down */);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionWorkout);
  cl_assert_equal_i(s_view->orbit_offset, 0);
  // and Sleep at the bottom
  prv_show_section(s_view, HealthOrbitSectionSleep);
  prv_up_down(s_view, true /* down */);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionSleep);
}

void test_health_orbit_view__launch_on_sleep(void) {
  s_launch_args = HealthCardType_Sleep;
  HealthOrbitView *view = health_orbit_view_create(&(HealthData){});
  cl_assert_equal_i(view->section, HealthOrbitSectionSleep);
  cl_assert_equal_i(view->orbit_offset, HealthOrbitSectionSleep * HEALTH_ORBIT_STEP);
  health_orbit_view_destroy(view);
}

// Workout
//////////////////////

//! A run 23 minutes 45 seconds in
static void prv_set_run(void) {
  workout_service_start_workout(ActivitySessionType_Run);
  s_workout.metrics = (WorkoutServiceMetrics){
    .duration_s = 23 * PBL_SEC_PER_MIN + 45,
    .steps = 3912,
    .distance_m = 4180,
    .active_kcal = 287,
  };
  s_workout.starts = 0;
}

//! A walk the watch detected, started 18 minutes ago
static void prv_set_detected_walk(void) {
  s_sessions[0] = (ActivitySession){
    .start_utc = NOW_UTC - 18 * PBL_SEC_PER_MIN,
    .length_min = 17,
    .type = ActivitySessionType_Walk,
    .ongoing = true,
    .step_data = {.steps = 1874, .active_kcalories = 64, .distance_meters = 1350},
  };
  s_num_sessions = 1;
}

static void prv_render_workout(void) {
  s_data = prv_sample_data();
  s_launch_args = 0;
  prv_render(&s_data, HealthOrbitSectionWorkout);
}

static void prv_rerender(void) {
  prv_rest(s_view);
  window_render(&s_view->window, &s_ctx);
}

void test_health_orbit_view__render_workout(void) {
  // No workout: the minutes active today and the three kinds of workout to start
  prv_render_workout();
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_workout_run(void) {
  prv_set_run();
  prv_render_workout();
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_workout_detected(void) {
  prv_set_detected_walk();
  prv_render_workout();
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_workout_open_paused(void) {
  workout_service_start_workout(ActivitySessionType_Open);
  workout_service_pause_workout(true);
  s_workout.metrics = (WorkoutServiceMetrics){
    .duration_s = PBL_SEC_PER_HOUR + 4 * PBL_SEC_PER_MIN + 9,
    .steps = 5210,
    .active_kcal = 402
  };
  prv_render_workout();
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_workout_kinds(void) {
  // Select with no workout: the kinds to start, Walk selected
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(s_view->week);
  cl_assert_equal_i(s_view->list, HealthOrbitListWorkoutKinds);
  prv_up_down(s_view, true /* down */);
  prv_rerender();
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

//! The color of the pixel at \a x, \a y of the display
static GColor prv_pixel(int16_t x, int16_t y) {
  const GBitmapDataRowInfo row = gbitmap_get_data_row_info(&s_ctx.dest_bitmap, y);
  cl_assert(x >= row.min_x && x <= row.max_x);
  return ((const GColor *)row.data)[x];
}

//! Renders the list with its selection's outline \a eighths of the way from row \a from to row
//! \a to
static void prv_render_selection_moving(int from, int to, int eighths) {
  s_view->selection_from = from;
  s_view->week_selection = to;
  s_view->selection_travel = (to - from) * prv_list_pitch(s_view) * eighths / 8;
  window_render(&s_view->window, &s_ctx);
}

//! Checks the selection's outline is drawn over the rows, white along the top and bottom of the
//! shape it is round
static void prv_check_outline_on_top(void) {
  MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS];
  prv_get_week_rows(s_view, rows);
  const MenuRoundShape outline = prv_selection_pill(s_view, rows);
  const int16_t x = outline.box.origin.x + outline.box.size.w / 2;
  cl_assert(gcolor_equal(prv_pixel(x, outline.box.origin.y), GColorWhite));
  cl_assert(gcolor_equal(prv_pixel(x, grect_get_max_y(&outline.box) - 1), GColorWhite));
}

void test_health_orbit_view__render_steps_week_moving(void) {
  // The outline between today and the day before, over the rows going Down and going Up
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionSteps);
  prv_show_week(s_view);
  for (int eighths = 1; eighths < 8; eighths++) {
    prv_render_selection_moving(0, 1, eighths);
    prv_check_outline_on_top();
    prv_render_selection_moving(1, 0, eighths);
    prv_check_outline_on_top();
  }
  prv_render_selection_moving(0, 1, 4);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

//! Checks the selection's frame \a elapsed into its move from \a from to \a to is within a row of
//! \a y and \a h
static void prv_check_selection_frame(GRect from, GRect to, bool up, int16_t elapsed, int16_t y,
                                      int16_t h) {
  const GRect frame = prv_menu_selection_frame(from, to, up, elapsed);
  cl_assert_equal_i(frame.origin.x, to.origin.x);
  cl_assert_equal_i(frame.size.w, to.size.w);
  cl_assert(PBL_ABS(frame.origin.y - y) <= 1);
  cl_assert(PBL_ABS(frame.size.h - h) <= 1);
}

void test_health_orbit_view__kinds_selection_moves_as_a_menu_does(void) {
  // As a round MenuLayer's highlight: the edge leaving draws in, then it lands past the far edge
  // of the row it moves to and draws back
  const GRect run = GRect(0, 100, 260, 44);
  const GRect walk = GRect(0, 146, 260, 44);
  prv_check_selection_frame(run, walk, false /* up */, 0, 100, 44);
  prv_check_selection_frame(run, walk, false /* up */, MENU_SELECTION_SHRINK_MS - 1, 107, 37);
  prv_check_selection_frame(run, walk, false /* up */, MENU_SELECTION_SHRINK_MS, 146, 54);
  prv_check_selection_frame(run, walk, false /* up */, MENU_SELECTION_MS, 146, 44);
  // Up, the other way round
  prv_check_selection_frame(walk, run, true /* up */, 0, 146, 44);
  prv_check_selection_frame(walk, run, true /* up */, MENU_SELECTION_SHRINK_MS - 1, 146, 37);
  prv_check_selection_frame(walk, run, true /* up */, MENU_SELECTION_SHRINK_MS, 90, 54);
  prv_check_selection_frame(walk, run, true /* up */, MENU_SELECTION_MS, 100, 44);
}

//! Renders the kinds with their selection \a elapsed into its move from \a from to \a to, and
//! checks the kinds' pills are drawn over it
static void prv_render_kinds_moving(int from, int to, int16_t elapsed) {
  s_view->selection_from = from;
  s_view->week_selection = to;
  s_view->selection_travel = elapsed;
  window_render(&s_view->window, &s_ctx);
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  for (int i = 0; i < HEALTH_WORKOUT_NUM_KINDS; i++) {
    MenuRoundShape pill;
    health_orbit_list_row(display, HealthOrbitListWorkoutKinds, i, &pill);
    // Just inside the top and bottom of each pill's left end, where it has no text
    const int16_t x = pill.box.origin.x + pill.radius + 2;
    cl_assert(gcolor_equal(prv_pixel(x, pill.box.origin.y + 1), GColorWhite));
    cl_assert(gcolor_equal(prv_pixel(x, grect_get_max_y(&pill.box) - 2), GColorWhite));
  }
}

void test_health_orbit_view__render_workout_kinds_moving_down(void) {
  // The selection's pill under the kinds' pills, on its way down from Run to Walk
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  for (int16_t elapsed = 0; elapsed <= MENU_SELECTION_MS; elapsed += 25) {
    prv_render_kinds_moving(0, 1, elapsed);
  }
  prv_render_kinds_moving(0, 1, MENU_SELECTION_SHRINK_MS + 25);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_workout_kinds_moving_up(void) {
  // and up from Walk to Run
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  for (int16_t elapsed = 0; elapsed <= MENU_SELECTION_MS; elapsed += 25) {
    prv_render_kinds_moving(1, 0, elapsed);
  }
  prv_render_kinds_moving(1, 0, MENU_SELECTION_SHRINK_MS + 25);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__kinds_selection_shows_round_the_selected_kind(void) {
  // At rest, in the Workout section's color just outside the selected kind's pill only
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_up_down(s_view, true /* down */);
  prv_rerender();
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  for (int i = 0; i < HEALTH_WORKOUT_NUM_KINDS; i++) {
    MenuRoundShape pill;
    health_orbit_list_row(display, HealthOrbitListWorkoutKinds, i, &pill);
    const GColor above = prv_pixel(pill.box.origin.x + pill.box.size.w / 2, pill.box.origin.y - 2);
    cl_assert_equal_b(gcolor_equal(above, GColorChromeYellow), i == 1);
  }
}

void test_health_orbit_view__render_workout_active(void) {
  // Select with a run going on: the run, its time over its measures
  prv_set_run();
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rerender();
  cl_assert_equal_i(s_view->list, HealthOrbitListWorkout);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__small_moon_leaves_its_bite_as_it_was(void) {
  // The crescent is drawn as it is, not as a disc with a disc of the bubble's color over it
  s_data = prv_sample_data();
  prv_render(&s_data, HealthOrbitSectionToday);
  graphics_context_set_default_drawing_state(&s_ctx, GContextInitializationMode_App);
  graphics_context_set_fill_color(&s_ctx, GColorRed);
  graphics_fill_rect(&s_ctx, &GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
  const GPoint center = GPoint(PBL_DISPLAY_WIDTH / 2, PBL_DISPLAY_HEIGHT / 2);
  const int16_t radius = 24;
  graphics_context_set_antialiased(&s_ctx, true);
  prv_draw_small_moon_icon(&s_ctx, center, radius, GColorBlue);
  // The moon's middle, its bite's, and its outer edge's
  const int16_t shift = (radius + 6) / 12;
  cl_assert(gcolor_equal(prv_pixel(center.x - shift - 6, center.y + shift + 6), GColorBlue));
  cl_assert(gcolor_equal(prv_pixel(center.x + 2 * shift, center.y - 2 * shift), GColorRed));
  cl_assert(gcolor_equal(prv_pixel(center.x - radius, center.y), GColorRed));
}

void test_health_orbit_view__turning_notch_leaves_what_is_under_it(void) {
  // The notch is left out of the sidebar as it turns, not painted over it in the background color
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  for (int eighths = 1; eighths < 8; eighths++) {
    s_data = prv_sample_data();
    prv_render(&s_data, HealthOrbitSectionSteps);
    graphics_context_set_default_drawing_state(&s_ctx, GContextInitializationMode_App);
    graphics_context_set_fill_color(&s_ctx, GColorRed);
    graphics_fill_rect(&s_ctx, &GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
    const int32_t turn = -QUARTER_TURN * eighths / 8;
    const SidebarSweep sweep = {.from = GColorBlue, .to = GColorBlue};
    prv_fill_sidebar(&s_ctx, display, turn, &sweep);
    // Along the line from the sidebar's center through the tip of the notch: in the notch short
    // of the tip, in the sidebar past it
    const GPoint center = health_orbit_center(display, turn);
    const GPoint tip = health_orbit_notch_tip(display, turn);
    const int32_t dx = tip.x - center.x;
    const int32_t dy = tip.y - center.y;
    const int32_t length = integer_sqrt(dx * dx + dy * dy);
    const int16_t notch_width = health_orbit_notch_width(turn);
    const int16_t in = PBL_MAX(notch_width / 2, 1) + 1;
    cl_assert(
        gcolor_equal(prv_pixel(tip.x + dx * in / length, tip.y + dy * in / length), GColorRed));
    cl_assert(
        gcolor_equal(prv_pixel(tip.x - dx * 3 / length, tip.y - dy * 3 / length), GColorBlue));
  }
}

void test_health_orbit_view__workout_rows_show_beside_the_header(void) {
  // A row passing behind the line under the banner and the time shows above and below them, what
  // is behind them left as it is between, not painted over
  prv_set_run();
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rerender();
  const int16_t band_top = HEALTH_WEEK_ROW_TOP - 8 - HEALTH_WORKOUT_STATUS_ROLL / 2;
  const int16_t band_bottom = HEALTH_WORKOUT_TIME_Y + HEALTH_WORKOUT_TIME_ROLL / 2;
  MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS] = {};
  rows[0] = (MenuRoundShape){GRect(20, band_top - 6, 40, band_bottom - band_top + 12), 6};
  graphics_context_set_default_drawing_state(&s_ctx, GContextInitializationMode_App);
  graphics_context_set_fill_color(&s_ctx, GColorRed);
  graphics_fill_rect(&s_ctx, &GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
  health_orbit_workout_draw(&s_ctx, &s_view->workout, rows, 0, s_view);
  const int16_t x = 40;
  cl_assert(!gcolor_equal(prv_pixel(x, band_top - 3), GColorRed));
  for (int16_t y = band_top; y < band_bottom; y++) {
    cl_assert(gcolor_equal(prv_pixel(x, y), GColorRed));
  }
  cl_assert(!gcolor_equal(prv_pixel(x, band_bottom + 2), GColorRed));
}

void test_health_orbit_view__render_workout_end(void) {
  // Paused, Down asks whether to end it
  prv_set_run();
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(s_workout.paused);
  prv_up_down(s_view, true /* down */);
  prv_rerender();
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusEnd);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_workout_complete(void) {
  // An open workout ended: its summary, as it was last
  workout_service_start_workout(ActivitySessionType_Open);
  s_workout.metrics = (WorkoutServiceMetrics){
    .duration_s = 41 * PBL_SEC_PER_MIN + 2,
    .steps = 2310,
    .active_kcal = 254
  };
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rerender();
  cl_assert(!s_workout.ongoing);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusComplete);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__workout_is_above_today(void) {
  // The app opens on Today, Workout's bubble a step above it and Steps' a step below
  s_data = prv_sample_data();
  s_view = health_orbit_view_create(&s_data);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionToday);
  cl_assert_equal_i(s_view->orbit_offset, HEALTH_ORBIT_STEP);
  cl_assert_equal_i(HealthOrbitSectionWorkout * HEALTH_ORBIT_STEP - s_view->orbit_offset,
                    -HEALTH_ORBIT_STEP);
  cl_assert_equal_i(HealthOrbitSectionSteps * HEALTH_ORBIT_STEP - s_view->orbit_offset,
                    HEALTH_ORBIT_STEP);
  // Up on Today turns a step counterclockwise to Workout, and Down back
  prv_up_down(s_view, false /* down */);
  cl_assert_equal_i(s_view->section, HealthOrbitSectionWorkout);
  cl_assert(!s_view->orbit_down);
  cl_assert_equal_i(s_view->orbit_target, s_view->orbit_from - HEALTH_ORBIT_STEP);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->orbit_offset, 0);
  // Its card has the large run shoe, and the kinds to start their icons
  cl_assert(s_view->card.run_icon == s_view->run_icon);
  cl_assert(s_view->run_icon);
  cl_assert_equal_i(gdraw_command_image_get_bounds_size(s_view->run_icon).w,
                    HEALTH_ORBIT_WORKOUT_SHOE_WIDTH);
  cl_assert(s_view->kind_icons[0] && s_view->kind_icons[1] && s_view->kind_icons[2]);
}

void test_health_orbit_view__workout_starts_after_its_countdown(void) {
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  // Walk, then Select counts down to it
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.countdown, HEALTH_WORKOUT_COUNTDOWN);
  cl_assert_equal_i(s_view->workout.type, ActivitySessionType_Walk);
  // Its rows are folded away while it counts down, and the buttons wait
  cl_assert_equal_i(s_view->row_travel[0], 0);
  prv_select_click_handler(nullptr, s_view);
  prv_up_down(s_view, true /* down */);
  cl_assert_equal_i(s_view->workout.countdown, HEALTH_WORKOUT_COUNTDOWN);
  cl_assert_equal_i(s_workout.starts, 0);
  // A second at a time
  for (int i = HEALTH_WORKOUT_COUNTDOWN - 1; i >= 0; i--) {
    prv_workout_tick(s_view);
    prv_rest(s_view);
    cl_assert_equal_i(s_view->workout.countdown, i);
    cl_assert_equal_i(s_workout.starts, (i == 0) ? 1 : 0);
  }
  cl_assert(s_workout.ongoing);
  cl_assert_equal_i(s_workout.type, ActivitySessionType_Walk);
  cl_assert_equal_i(s_view->list, HealthOrbitListWorkout);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusRunning);
  cl_assert_equal_i(s_view->row_travel[0], ROW_AT_REST);
}

void test_health_orbit_view__back_calls_off_the_countdown(void) {
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_back_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(!s_view->week);
  prv_workout_tick(s_view);
  prv_workout_tick(s_view);
  prv_workout_tick(s_view);
  cl_assert_equal_i(s_workout.starts, 0);
}

void test_health_orbit_view__workout_pauses_ends_and_shows_its_summary(void) {
  prv_set_run();
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  // Its buttons' bubbles show
  cl_assert_equal_i(s_view->workout.button_radius, HEALTH_WORKOUT_BUTTON_RADIUS);
  // Down, by the End bubble, asks to end it while it runs too, and Up takes that back, running
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusEnd);
  cl_assert(!s_workout.paused);
  prv_up_down(s_view, false /* down */);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusRunning);
  // Select pauses and resumes
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(s_workout.paused);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusPaused);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(!s_workout.paused);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusRunning);
  // Paused, Down asks, Up or Back takes it back, Select ends it
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusEnd);
  prv_back_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(s_view->week);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusPaused);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert_equal_i(s_workout.stops, 1);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusComplete);
  // Its buttons are done with
  cl_assert_equal_i(s_view->workout.button_radius, 0);
  cl_assert_equal_i(s_view->workout.workout.steps, 3912);
  // Its summary stays as the ticks go on
  prv_workout_tick(s_view);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusComplete);
  // Select goes back to the card, nothing going on
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(!s_view->week);
  cl_assert_equal_i(health_data_workout_get(&s_data)->state, HealthWorkoutStateNone);
}

void test_health_orbit_view__workout_ended_elsewhere_shows_its_summary(void) {
  prv_set_run();
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  workout_service_stop_workout();
  prv_workout_tick(s_view);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusComplete);
}

void test_health_orbit_view__detected_workout_is_taken_over(void) {
  prv_set_detected_walk();
  prv_render_workout();
  const HealthWorkoutData *workout = health_data_workout_get(&s_data);
  cl_assert_equal_i(workout->state, HealthWorkoutStateDetected);
  cl_assert_equal_i(workout->type, ActivitySessionType_Walk);
  cl_assert_equal_i(workout->duration_s, 18 * PBL_SEC_PER_MIN);
  cl_assert(workout->auto_detected);
  // Select turns it into a workout, still known as detected
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert_equal_i(s_workout.takeovers, 1);
  cl_assert(s_workout.ongoing);
  cl_assert_equal_i(workout->state, HealthWorkoutStateRunning);
  cl_assert(workout->auto_detected);
  cl_assert_equal_i(s_view->list, HealthOrbitListWorkout);
  cl_assert_equal_i(s_view->workout.workout.steps, 1874);
}

void test_health_orbit_view__workout_app_opens_the_workout_section(void) {
  // Nothing going on: the Workout card
  s_launch_args = ((HealthLaunchArgs){.card_type = HealthCardType_Workout}).args;
  HealthOrbitView *view = health_orbit_view_create(&s_data);
  cl_assert_equal_i(view->section, HealthOrbitSectionWorkout);
  cl_assert(!view->week);
  health_orbit_view_destroy(view);
  // A workout going on: straight to it
  s_data = (HealthData){};
  prv_set_run();
  view = health_orbit_view_create(&s_data);
  cl_assert(view->week);
  cl_assert_equal_i(view->list, HealthOrbitListWorkout);
  cl_assert_equal_i(view->workout.status, HealthOrbitWorkoutStatusRunning);
  health_orbit_view_destroy(view);
  // Asked to end it: ended, its summary shown
  s_launch_args =
      ((HealthLaunchArgs){.card_type = HealthCardType_Workout, .end_workout = true}).args;
  view = health_orbit_view_create(&s_data);
  cl_assert(!s_workout.ongoing);
  cl_assert_equal_i(view->workout.status, HealthOrbitWorkoutStatusComplete);
  cl_assert_equal_i(view->workout.workout.steps, 3912);
  health_orbit_view_destroy(view);
}

void test_health_orbit_view__workout_card_follows_the_workout_every_second(void) {
  prv_render_workout();
  cl_assert_equal_i(health_data_workout_get(&s_data)->state, HealthWorkoutStateNone);
  // Started elsewhere: the next tick shows it, then its time as it goes on
  prv_set_run();
  prv_workout_tick(s_view);
  cl_assert_equal_i(health_data_workout_get(&s_data)->state, HealthWorkoutStateRunning);
  s_workout.metrics.duration_s++;
  prv_workout_tick(s_view);
  cl_assert_equal_i(health_data_workout_get(&s_data)->duration_s, 23 * PBL_SEC_PER_MIN + 46);
  cl_assert(!health_data_workout_get(&s_data)->auto_detected);
}

void test_health_orbit_view__workout_ends_while_running(void) {
  // Down then Select ends a running workout, the asking line rolling back if taken back
  prv_set_run();
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  prv_back_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(s_view->week);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusRunning);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusRunning);
  prv_up_down(s_view, true /* down */);
  prv_rest(s_view);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert(!s_workout.ongoing);
  cl_assert_equal_i(s_view->workout.status, HealthOrbitWorkoutStatusComplete);
}

void test_health_orbit_view__workout_buttons_wait_for_the_countdown(void) {
  prv_render_workout();
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.button_radius, 0);
  prv_select_click_handler(nullptr, s_view);
  prv_rest(s_view);
  cl_assert_equal_i(s_view->workout.button_radius, 0);
  for (int i = 0; i < HEALTH_WORKOUT_COUNTDOWN; i++) {
    prv_workout_tick(s_view);
    prv_rest(s_view);
  }
  cl_assert_equal_i(s_view->workout.button_radius, HEALTH_WORKOUT_BUTTON_RADIUS);
}
