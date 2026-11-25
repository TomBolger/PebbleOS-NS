/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_draw.h"

#include <inttypes.h>
#include <stdio.h>

#include <pbl/i18n/i18n.h>
#include <pbl/services/clock.h>
#include <pbl/util/math.h>
#include <pbl/util/units.h>

#include <applib/fonts/fonts.h>
#include <applib/graphics/text.h>

#if PBL_ROUND

const HealthOrbitFont health_orbit_font_gothic_14 = {FONT_KEY_GOTHIC_14, 5, 14};
const HealthOrbitFont health_orbit_font_gothic_14_bold = {FONT_KEY_GOTHIC_14_BOLD, 5, 14};
const HealthOrbitFont health_orbit_font_gothic_18_bold = {FONT_KEY_GOTHIC_18_BOLD, 7, 18};
const HealthOrbitFont health_orbit_font_gothic_24_bold = {FONT_KEY_GOTHIC_24_BOLD, 10, 24};
const HealthOrbitFont health_orbit_font_leco_38_bold = {FONT_KEY_LECO_38_BOLD_NUMBERS, 11, 38};
const HealthOrbitFont health_orbit_font_leco_28_light = {FONT_KEY_LECO_28_LIGHT_NUMBERS, 8, 28};

int16_t health_orbit_run_width(GContext *ctx, const HealthOrbitRun *run) {
  GFont font = fonts_get_system_font(run->font->key);
  return graphics_text_layout_get_max_used_size(
             ctx, run->text, font, GRect(0, 0, PBL_DISPLAY_WIDTH, 2 * run->font->baseline),
             GTextOverflowModeFill, GTextAlignmentLeft, nullptr)
      .w;
}

int16_t health_orbit_runs_width(GContext *ctx, const HealthOrbitRun *runs, int num_runs,
                                int16_t gap) {
  int16_t width = 0;
  for (int i = 0; i < num_runs; i++) {
    width += health_orbit_run_width(ctx, &runs[i]) + ((i > 0) ? (gap + runs[i].extra_gap) : 0);
  }
  return width;
}

void health_orbit_draw_runs(GContext *ctx, const HealthOrbitRun *runs, int num_runs, int16_t gap,
                            int16_t center_x, int16_t mid_y) {
  int16_t cap = 0;
  for (int i = 0; i < num_runs; i++) {
    cap = PBL_MAX(cap, runs[i].font->baseline - runs[i].font->cap_top);
  }
  const int16_t baseline = mid_y + (cap + 1) / 2;
  int16_t x = center_x - health_orbit_runs_width(ctx, runs, num_runs, gap) / 2;
  for (int i = 0; i < num_runs; i++) {
    if (i > 0) {
      x += runs[i].extra_gap;
    }
    const HealthOrbitFont *font = runs[i].font;
    const int16_t width = health_orbit_run_width(ctx, &runs[i]);
    graphics_context_set_text_color(ctx, runs[i].color);
    graphics_draw_text(ctx, runs[i].text, fonts_get_system_font(font->key),
                       GRect(x, baseline - font->baseline, width + 1, 2 * font->baseline),
                       GTextOverflowModeFill, GTextAlignmentLeft, nullptr);
    x += width + gap;
  }
}

void health_orbit_draw_text(GContext *ctx, const char *text, const HealthOrbitFont *font,
                            GColor color, int16_t center_x, int16_t mid_y) {
  health_orbit_draw_runs(ctx, &(HealthOrbitRun){text, font, color}, 1, 0, center_x, mid_y);
}

void health_orbit_format_count(char *buffer, size_t size, int32_t count) {
  if (count >= 1000) {
    snprintf(buffer, size, "%" PRId32 ",%03" PRId32, count / 1000, count % 1000);
  } else {
    snprintf(buffer, size, "%" PRId32, PBL_MAX(count, 0));
  }
}

void health_orbit_format_count_delta(char *buffer, size_t size, int32_t delta) {
  buffer[0] = (delta < 0) ? '-' : '+';
  health_orbit_format_count(buffer + 1, size - 1, PBL_ABS(delta));
}

void health_orbit_format_duration(char *buffer, size_t size, int32_t seconds, void *i18n_owner) {
  const int32_t minutes = PBL_ABS(seconds) / PBL_SEC_PER_MIN;
  if (minutes >= PBL_MIN_PER_HOUR) {
    /// A duration in hours and minutes, as "7h 25m"
    snprintf(buffer, size, pbl_i18n_get("%dh %dm", i18n_owner), (int)(minutes / PBL_MIN_PER_HOUR),
             (int)(minutes % PBL_MIN_PER_HOUR));
  } else {
    /// A duration in minutes, as "25m"
    snprintf(buffer, size, pbl_i18n_get("%dm", i18n_owner), (int)minutes);
  }
}

void health_orbit_format_short_duration(char *buffer, size_t size, int32_t seconds,
                                        void *i18n_owner) {
  const int32_t minutes = PBL_MAX(seconds, 0) / PBL_SEC_PER_MIN;
  if (minutes >= PBL_MIN_PER_HOUR) {
    /// A duration in hours and minutes in a bubble, as "1h40"
    snprintf(buffer, size, pbl_i18n_get("%dh%02d", i18n_owner), (int)(minutes / PBL_MIN_PER_HOUR),
             (int)(minutes % PBL_MIN_PER_HOUR));
  } else {
    health_orbit_format_duration(buffer, size, seconds, i18n_owner);
  }
}

void health_orbit_format_time_of_day(char *buffer, size_t size, int32_t seconds) {
  const int32_t minute =
      ((seconds / PBL_SEC_PER_MIN) % PBL_MIN_PER_DAY + PBL_MIN_PER_DAY) % PBL_MIN_PER_DAY;
  clock_format_time(buffer, size, minute / PBL_MIN_PER_HOUR, minute % PBL_MIN_PER_HOUR,
                    true /* add_space */);
}

GDrawState health_orbit_clip(GContext *ctx, GRect box) {
  const GDrawState state = graphics_context_get_drawing_state(ctx);
  box.origin = gpoint_add(box.origin, state.drawing_box.origin);
  grect_clip(&box, &state.clip_box);
  ctx->draw_state.clip_box = box;
  return state;
}

#endif
