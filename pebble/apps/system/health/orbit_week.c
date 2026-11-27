/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_week.h"

#include "orbit_draw.h"
#include "orbit_layout.h"

#include <stdio.h>
#include <string.h>

#include <pbl/i18n/i18n.h>
#include <pbl/services/time.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <applib/fonts/codepoint.h>

#if PBL_ROUND

//! The line above the rows, from the top of the display
#define HEADER_Y (HEALTH_WEEK_ROW_TOP - 8)

//! Space between a row's text and the ends of its pill, and kept between its fill and its value
#define ROW_TEXT_INSET 2
#define ROW_VALUE_GAP  8

//! The outline round the selected row
#define OUTLINE_WIDTH 2

typedef struct HealthOrbitWeekStyle {
  GColor track;
  GColor fill;
  GColor recent_fill;
  //! Shade of the stretches of the value where they came along the fill
  GColor span;
  GColor header_label;
} HealthOrbitWeekStyle;

static const HealthOrbitWeekStyle s_steps_style = {
  .track = GColorDarkGray,
  .fill = GColorIslamicGreen,
  .recent_fill = GColorIslamicGreen,
  // The walks and runs a step deeper than the fill, as on the steps ring
  .span = GColorDarkGreen,
  .header_label = GColorLightGray,
};

static const HealthOrbitWeekStyle s_sleep_style = {
  .track = GColorDukeBlue,
  .fill = GColorCobaltBlue,
  .recent_fill = GColorBlueMoon,
  // Only last night's row, on its recent fill, has the times of its deep sleep: a step deeper
  .span = GColorBlue,
  .header_label = GColorBabyBlueEyes,
};

typedef struct HealthOrbitWeekDay {
  char label[16];
  char value_text[16];
  int32_t value;
  //! Stretches of the value, in units of \a span_unit of it: the walks and runs in steps, the
  //! deep sleep in minutes
  const HealthSpans *spans;
  int32_t span_unit;
} HealthOrbitWeekDay;

typedef struct HealthOrbitWeek {
  HealthOrbitWeekDay days[HEALTH_ORBIT_WEEK_NUM_ROWS];
  //! What a full row is, the most of any day
  int32_t full;
  //! The day with the most, for the crown, or -1
  int best;
} HealthOrbitWeek;

//! The weekday \a days_ago days before today, as "Mon"
static void prv_format_weekday(char *buffer, size_t size, int days_ago) {
  struct pbl_tm local_tm;
  health_orbit_day_time(days_ago, &local_tm);
  pbl_strftime_r(buffer, size, "%a", &local_tm);
}

static void prv_load_week(HealthOrbitSection section, HealthData *health_data,
                          HealthOrbitWeek *week, void *i18n_owner) {
  *week = (HealthOrbitWeek){.best = -1};
  const bool sleep = (section == HealthOrbitSectionSleep);
  const int32_t *values =
      sleep ? health_data_sleep_get(health_data) : health_data_steps_get(health_data);
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    HealthOrbitWeekDay *day = &week->days[i];
    day->value = PBL_MAX(values[i], 0);
    // Only last night still has the times of its deep sleep
    if (sleep) {
      day->spans = (i == 0) ? health_data_deep_sleep_spans_get(health_data) : nullptr;
      day->span_unit = PBL_SEC_PER_MIN;
    } else {
      day->spans = health_data_walks_get(health_data, i);
      day->span_unit = 1;
    }
    if (i == 0) {
      strncpy(day->label,
              sleep ? pbl_i18n_get("Last night", i18n_owner) : pbl_i18n_get("Today", i18n_owner),
              sizeof(day->label) - 1);
    } else {
      // A night is the one of the evening it starts
      prv_format_weekday(day->label, sizeof(day->label), sleep ? i + 1 : i);
    }
    if (day->value <= 0) {
      strncpy(day->value_text, EN_DASH, sizeof(day->value_text));
    } else if (sleep) {
      const int minutes = day->value / PBL_SEC_PER_MIN;
      /// Hours and minutes slept on a night of the week, as "7h 05"
      snprintf(day->value_text, sizeof(day->value_text), pbl_i18n_get("%dh %02d", i18n_owner),
               minutes / PBL_MIN_PER_HOUR, minutes % PBL_MIN_PER_HOUR);
    } else {
      health_orbit_format_count(day->value_text, sizeof(day->value_text), day->value);
    }
    if (day->value > week->full) {
      week->full = day->value;
      week->best = i;
    }
  }
}

//! Average of the days with data
static int32_t prv_average(const HealthOrbitWeek *week) {
  int32_t sum = 0;
  int count = 0;
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    if (week->days[i].value > 0) {
      sum += week->days[i].value;
      count++;
    }
  }
  return count ? (sum / count) : 0;
}

static void prv_fill_round_rect(GContext *ctx, GRect box, int16_t radius, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_round_rect(ctx, &box, PBL_MIN(radius, PBL_MIN(box.size.w, box.size.h) / 2),
                           GCornersAll);
}

static void prv_draw_row(GContext *ctx, const HealthOrbitWeekStyle *style,
                         const HealthOrbitWeek *week, int index, const MenuRoundShape *pill,
                         GDrawCommandImage *crown) {
  const HealthOrbitWeekDay *day = &week->days[index];
  const GRect box = pill->box;
  const int16_t h = box.size.h;
  prv_fill_round_rect(ctx, box, pill->radius, style->track);

  // The fill keeps clear of the value unless the day is full
  const HealthOrbitRun value = {day->value_text, &health_orbit_font_gothic_14_bold, GColorWhite};
  const int16_t value_width = health_orbit_run_width(ctx, &value);
  const int16_t usable = box.size.w - value_width - h / 2 - ROW_VALUE_GAP;
  if (week->full > 0 && day->value > 0) {
    const int16_t fill = (day->value >= week->full)
                             ? box.size.w
                             : PBL_MAX(h, (int32_t)usable * day->value / week->full);
    const GRect fill_box = GRect(box.origin.x, box.origin.y, fill, h);
    prv_fill_round_rect(ctx, fill_box, pill->radius,
                        (index == 0) ? style->recent_fill : style->fill);
    // Each stretch is the fill's shape across its columns, so it is round where the fill is
    for (int i = 0; day->spans && i < day->spans->count; i++) {
      const HealthSpan *span = &day->spans->spans[i];
      int16_t from;
      int16_t to;
      if (health_orbit_bar_span(fill, day->value, span->from * day->span_unit,
                                span->to * day->span_unit, &from, &to)) {
        const GDrawState state =
            health_orbit_clip(ctx, GRect(box.origin.x + from, box.origin.y, to - from, h));
        prv_fill_round_rect(ctx, fill_box, pill->radius, style->span);
        graphics_context_set_drawing_state(ctx, state);
      }
    }
  }

  // The text shows as far as the pill has grown
  const GDrawState state = health_orbit_clip(ctx, box);
  const int16_t mid_y = box.origin.y + h / 2;
  const HealthOrbitRun label = {day->label, &health_orbit_font_gothic_14_bold, GColorWhite};
  const int16_t label_left = box.origin.x + h / 2 + ROW_TEXT_INSET;
  health_orbit_draw_runs(ctx, &label, 1, 0, label_left + health_orbit_run_width(ctx, &label) / 2,
                         mid_y);
  const int16_t value_right = grect_get_max_x(&box) - h / 2 - ROW_TEXT_INSET;
  health_orbit_draw_runs(ctx, &value, 1, 0, value_right - value_width / 2, mid_y);
  if (crown && index == week->best && day->value > 0) {
    const GSize size = gdraw_command_image_get_bounds_size(crown);
    gdraw_command_image_draw(
        ctx, crown,
        GPoint(value_right - value_width - ROW_VALUE_GAP / 2 - size.w, mid_y - size.h / 2));
  }
  graphics_context_set_drawing_state(ctx, state);
}

static void prv_draw_header(GContext *ctx, HealthOrbitSection section, HealthData *health_data,
                            const HealthOrbitWeek *week, const HealthOrbitWeekStyle *style,
                            int16_t offset_y, void *i18n_owner) {
  const bool sleep = (section == HealthOrbitSectionSleep);
  const int32_t values[] = {
    prv_average(week),
    sleep ? health_data_sleep_get_cur_wday_average(health_data)
          : health_data_steps_get_monthly_average(health_data),
  };
  char texts[PBL_ARRAY_SIZE(values)][16];
  for (size_t i = 0; i < PBL_ARRAY_SIZE(values); i++) {
    if (values[i] <= 0) {
      strncpy(texts[i], EN_DASH, sizeof(texts[i]));
    } else if (sleep) {
      health_orbit_format_duration(texts[i], sizeof(texts[i]), values[i], i18n_owner);
    } else {
      health_orbit_format_count(texts[i], sizeof(texts[i]), values[i]);
    }
  }
  const char *reference_label;
  if (sleep) {
    /// Before the typical sleep, above the week of the round Health app
    reference_label = pbl_i18n_get("· typical", i18n_owner);
  } else {
    /// Before the 30 day average steps, above the week of the round Health app
    reference_label = pbl_i18n_get("· 30-day", i18n_owner);
  }
  const HealthOrbitRun runs[] = {
    /// Before the average of the week, above the week of the round Health app
    {pbl_i18n_get("avg", i18n_owner), &health_orbit_font_gothic_14, style->header_label},
    {texts[0], &health_orbit_font_gothic_14_bold, GColorWhite},
    {reference_label, &health_orbit_font_gothic_14, style->header_label},
    {texts[1], &health_orbit_font_gothic_14_bold, GColorWhite},
  };
  // The reference is left out when both do not fit between the bezel's edges
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  MenuRoundShape top_row;
  health_orbit_week_row(display, 0, &top_row);
  const int gap = 3;
  const int num_runs = (health_orbit_runs_width(ctx, runs, 4, gap) <= top_row.box.size.w) ? 4 : 2;
  health_orbit_draw_runs(ctx, runs, num_runs, gap, display.w / 2, HEADER_Y + offset_y);
}

bool health_orbit_week_has_section(HealthOrbitSection section) {
  return (section == HealthOrbitSectionSteps) || (section == HealthOrbitSectionSleep);
}

void health_orbit_week_get_rest_rows(MenuRoundShape rows_out[HEALTH_ORBIT_WEEK_NUM_ROWS]) {
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    health_orbit_week_row(display, i, &rows_out[i]);
  }
}

void health_orbit_week_draw(GContext *ctx, HealthOrbitSection section, HealthData *health_data,
                            const MenuRoundShape rows[HEALTH_ORBIT_WEEK_NUM_ROWS],
                            int16_t header_offset_y, const MenuRoundShape *highlight,
                            GDrawCommandImage *crown, void *i18n_owner) {
  const HealthOrbitWeekStyle *style =
      (section == HealthOrbitSectionSleep) ? &s_sleep_style : &s_steps_style;
  HealthOrbitWeek week;
  prv_load_week(section, health_data, &week, i18n_owner);
  graphics_context_set_antialiased(ctx, true);
  prv_draw_header(ctx, section, health_data, &week, style, header_offset_y, i18n_owner);
  // The oldest first, so a row coming out of the notch passes under the rows already out
  for (int i = HEALTH_ORBIT_WEEK_NUM_ROWS - 1; i >= 0; i--) {
    if (rows[i].box.size.w > 0) {
      prv_draw_row(ctx, style, &week, i, &rows[i],
                   (section == HealthOrbitSectionSteps) ? crown : nullptr);
    }
  }
  // The outline round the selected day, over the rows wherever it is on its way between them
  if (highlight && highlight->box.size.w > 0) {
    health_orbit_fill_outline(ctx, highlight->box, highlight->radius, OUTLINE_WIDTH, GColorWhite);
  }
  graphics_context_set_antialiased(ctx, false);
}

#endif
