/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "fake_pfs.h"
#include "test_health_app_includes.h"

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

void test_health_orbit_view__launch_on_sleep(void) {
  s_launch_args = HealthCardType_Sleep;
  HealthOrbitView *view = health_orbit_view_create(&(HealthData){});
  cl_assert_equal_i(view->section, HealthOrbitSectionSleep);
  cl_assert_equal_i(view->orbit_offset, HealthOrbitSectionSleep * HEALTH_ORBIT_STEP);
  health_orbit_view_destroy(view);
}
