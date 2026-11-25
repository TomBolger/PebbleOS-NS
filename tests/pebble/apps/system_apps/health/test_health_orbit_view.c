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

//! A Wednesday afternoon, 15:45
#define NOW_UTC 1764085500

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

static void prv_render(HealthData *health_data, HealthOrbitSection section) {
  s_view = health_orbit_view_create(health_data);
  prv_show_section(s_view, section);
  window_set_on_screen(&s_view->window, true, true);
  window_render(&s_view->window, &s_ctx);
}

// Tests
//////////////////////

void test_health_orbit_view__render_today(void) {
  prv_render(&(HealthData){}, HealthOrbitSectionToday);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_steps(void) {
  prv_render(&(HealthData){}, HealthOrbitSectionSteps);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__render_sleep(void) {
  prv_render(&(HealthData){}, HealthOrbitSectionSleep);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_orbit_view__launch_on_sleep(void) {
  s_launch_args = HealthCardType_Sleep;
  HealthOrbitView *view = health_orbit_view_create(&(HealthData){});
  cl_assert_equal_i(view->section, HealthOrbitSectionSleep);
  cl_assert_equal_i(view->orbit_offset, HealthOrbitSectionSleep * HEALTH_ORBIT_STEP);
  health_orbit_view_destroy(view);
}
