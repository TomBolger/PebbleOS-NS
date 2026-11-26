/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_draw.h
//! Drawing helpers shared by the round Health app's cards and week: text lined up by its capitals,
//! the numbers formatted as they show, and clipping.

#include <stddef.h>
#include <stdint.h>

#include <pbl/services/time.h>

#include <applib/graphics/graphics.h>

//! A font with the rows of its cap tops and baselines in its line, to line text up by its capitals
typedef struct HealthOrbitFont {
  const char *key;
  int8_t cap_top;
  int8_t baseline;
} HealthOrbitFont;

extern const HealthOrbitFont health_orbit_font_gothic_14;
extern const HealthOrbitFont health_orbit_font_gothic_14_bold;
extern const HealthOrbitFont health_orbit_font_gothic_18_bold;
extern const HealthOrbitFont health_orbit_font_gothic_24_bold;
extern const HealthOrbitFont health_orbit_font_leco_38_bold;
extern const HealthOrbitFont health_orbit_font_leco_28_light;

//! A run of text in one font and color
typedef struct HealthOrbitRun {
  const char *text;
  const HealthOrbitFont *font;
  GColor color;
  //! Space before the run on top of the gap between runs, to set a word apart
  int8_t extra_gap;
} HealthOrbitRun;

//! Width of a run of text
int16_t health_orbit_run_width(GContext *ctx, const HealthOrbitRun *run);

//! Width of runs side by side \a gap apart
int16_t health_orbit_runs_width(GContext *ctx, const HealthOrbitRun *runs, int num_runs,
                                int16_t gap);

//! Draws runs side by side \a gap apart on one baseline, centered on \a center_x, the middle of the
//! tallest capitals on \a mid_y
void health_orbit_draw_runs(GContext *ctx, const HealthOrbitRun *runs, int num_runs, int16_t gap,
                            int16_t center_x, int16_t mid_y);

//! Draws text centered on \a center_x, the middle of its capitals on \a mid_y
void health_orbit_draw_text(GContext *ctx, const char *text, const HealthOrbitFont *font,
                            GColor color, int16_t center_x, int16_t mid_y);

//! A count with its thousands apart, as "7,842"
void health_orbit_format_count(char *buffer, size_t size, int32_t count);

//! A count's difference from another, signed, as "+932"
void health_orbit_format_count_delta(char *buffer, size_t size, int32_t delta);

//! A duration in hours and minutes, as "7h 25m", or "25m" under an hour
void health_orbit_format_duration(char *buffer, size_t size, int32_t seconds, void *i18n_owner);

//! A duration in hours and minutes as short as fits a bubble, as "1h40", or "40m" under an hour
void health_orbit_format_short_duration(char *buffer, size_t size, int32_t seconds,
                                        void *i18n_owner);

//! A time of day in seconds from midnight, before midnight if negative, as "11:48 PM"
void health_orbit_format_time_of_day(char *buffer, size_t size, int32_t seconds);

//! The local time \a days_ago days before now
void health_orbit_day_time(int days_ago, struct pbl_tm *tm_out);

//! A weekday, 0 for Sunday, in the two letters that fit a bubble, as "Mo"
const char *health_orbit_short_weekday(int wday, void *i18n_owner);

//! Fills an outline \a width wide round \a box, whose corners are \a radius round, over its edge
//! and outside it, leaving the rest of the box as it is, so it can be drawn over what is there
void health_orbit_fill_outline(GContext *ctx, GRect box, int16_t radius, int16_t width,
                               GColor color);

//! Clips drawing to \a box, in the drawing box's coordinates, within the current clip
//! @return The drawing state to restore with graphics_context_set_drawing_state()
GDrawState health_orbit_clip(GContext *ctx, GRect box);
