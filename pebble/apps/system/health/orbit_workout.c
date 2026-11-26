/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_workout.h"

#include "orbit_card.h"
#include "orbit_draw.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <pbl/i18n/i18n.h>
#include <pbl/services/activity/health_util.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#if PBL_ROUND

//! The line under the banner, and the middle of the time below it
#define STATUS_Y (HEALTH_WEEK_ROW_TOP - 8)
#define TIME_Y   HEALTH_WORKOUT_TIME_Y

//! Space between a row's text and the ends of its pill, and between a kind's icon and its name
#define ROW_TEXT_INSET 2
#define KIND_ICON_GAP  6

//! The ring round the selected kind of workout, in the Workout section's color
#define KIND_RING_WIDTH 3
#define KIND_RING_COLOR GColorChromeYellow
#define KIND_ROW_COLOR  GColorWhite

//! Colors of the measures, as the Workout app labels them
#define DISTANCE_COLOR GColorCyan
#define PACE_COLOR     GColorGreen
#define STEPS_COLOR    GColorShockingPink
#define KCAL_COLOR     GColorOrange
#define PAUSED_COLOR   GColorChromeYellow
#define END_COLOR      GColorRed

#define ROW_COLOR        GColorDarkGray
#define BACKGROUND_COLOR GColorBlack

static void prv_fill_round_rect(GContext *ctx, GRect box, int16_t radius, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_round_rect(ctx, &box, PBL_MIN(radius, PBL_MIN(box.size.w, box.size.h) / 2),
                           GCornersAll);
}

//! Copies \a text into \a buffer in capitals
static void prv_upper(char *buffer, size_t size, const char *text) {
  strncpy(buffer, text, size - 1);
  buffer[size - 1] = '\0';
  for (char *c = buffer; *c; c++) {
    *c = toupper((unsigned char)*c);
  }
}

//! What the line under the banner says for \a status, and its color
static GColor prv_status_text(HealthOrbitWorkoutStatus status, ActivitySessionType type,
                              char *buffer, size_t size, void *i18n_owner) {
  switch (status) {
    case HealthOrbitWorkoutStatusKinds:
      /// Above the kinds of workout to start in the round Health app
      prv_upper(buffer, size, pbl_i18n_get("Start a workout", i18n_owner));
      return GColorLightGray;
    case HealthOrbitWorkoutStatusPaused:
      /// Above a paused workout's time in the round Health app
      prv_upper(buffer, size, pbl_i18n_get("Paused", i18n_owner));
      return PAUSED_COLOR;
    case HealthOrbitWorkoutStatusEnd:
      /// Asking whether to end the workout going on, in the round Health app
      prv_upper(buffer, size, pbl_i18n_get("End workout?", i18n_owner));
      return END_COLOR;
    case HealthOrbitWorkoutStatusComplete:
      /// Above an ended workout's summary in the round Health app
      prv_upper(buffer, size, pbl_i18n_get("Complete", i18n_owner));
      return health_orbit_workout_color(type);
    case HealthOrbitWorkoutStatusRunning:
      break;
  }
  prv_upper(buffer, size, pbl_i18n_get(health_orbit_workout_name(type), i18n_owner));
  return health_orbit_workout_color(type);
}

//! Draws \a status centered on \a y within the band \a band_h tall about it
static void prv_draw_status_at(GContext *ctx, const HealthOrbitWorkoutScreen *screen,
                               HealthOrbitWorkoutStatus status, int16_t y, void *i18n_owner) {
  char text[24];
  const GColor color = prv_status_text(status, screen->type, text, sizeof(text), i18n_owner);
  health_orbit_draw_text(ctx, text, &health_orbit_font_gothic_14_bold, color, PBL_DISPLAY_WIDTH / 2,
                         y);
}

//! The line under the banner, rolling up from what it said to what it says
static void prv_draw_status(GContext *ctx, const HealthOrbitWorkoutScreen *screen, int16_t offset_y,
                            void *i18n_owner) {
  const int16_t y = STATUS_Y + offset_y;
  const int16_t roll = screen->status_roll;
  const GDrawState state = health_orbit_clip(
      ctx,
      GRect(0, y - HEALTH_WORKOUT_STATUS_ROLL / 2, PBL_DISPLAY_WIDTH, HEALTH_WORKOUT_STATUS_ROLL));
  if (roll != 0) {
    prv_draw_status_at(ctx, screen, screen->status_from, y + roll - HEALTH_WORKOUT_STATUS_ROLL,
                       i18n_owner);
  }
  prv_draw_status_at(ctx, screen, screen->status, y + roll, i18n_owner);
  graphics_context_set_drawing_state(ctx, state);
}

//! The time, or the seconds left of the countdown, centered on \a y
static void prv_draw_time_at(GContext *ctx, const HealthOrbitWorkoutScreen *screen, int countdown,
                             int16_t y) {
  if (countdown < 0) {
    return;
  }
  char text[16];
  GColor color = GColorWhite;
  if (countdown > 0) {
    snprintf(text, sizeof(text), "%d", countdown);
    color = health_orbit_workout_color(screen->type);
  } else {
    health_orbit_format_workout_time(text, sizeof(text), screen->workout.duration_s);
    // Its color changes with the line above it, as that rolls past half way
    const HealthOrbitWorkoutStatus status = (screen->status_roll > HEALTH_WORKOUT_STATUS_ROLL / 2)
                                                ? screen->status_from
                                                : screen->status;
    if (status == HealthOrbitWorkoutStatusPaused || status == HealthOrbitWorkoutStatusEnd) {
      color = PAUSED_COLOR;
    } else if (status == HealthOrbitWorkoutStatusComplete) {
      color = health_orbit_workout_color(screen->type);
    }
  }
  health_orbit_draw_text(ctx, text, &health_orbit_font_leco_38_bold, color, PBL_DISPLAY_WIDTH / 2,
                         y);
}

//! The time, rolling up from the countdown's last second
static void prv_draw_time(GContext *ctx, const HealthOrbitWorkoutScreen *screen, int16_t offset_y) {
  const int16_t y = TIME_Y + offset_y;
  const int16_t roll = screen->time_roll;
  const GDrawState state = health_orbit_clip(
      ctx, GRect(0, y - HEALTH_WORKOUT_TIME_ROLL / 2, PBL_DISPLAY_WIDTH, HEALTH_WORKOUT_TIME_ROLL));
  if (roll != 0) {
    prv_draw_time_at(ctx, screen, screen->countdown_from, y + roll - HEALTH_WORKOUT_TIME_ROLL);
  }
  prv_draw_time_at(ctx, screen, screen->countdown, y + roll);
  graphics_context_set_drawing_state(ctx, state);
}

//! The line under the banner, and the time below it when \a with_time, over the rows passing
//! behind them on their way to and from the notch
static void prv_draw_header(GContext *ctx, const HealthOrbitWorkoutScreen *screen, int16_t offset_y,
                            bool with_time, void *i18n_owner) {
  const int16_t top = STATUS_Y - HEALTH_WORKOUT_STATUS_ROLL / 2 + offset_y;
  const int16_t bottom = (with_time ? (TIME_Y + HEALTH_WORKOUT_TIME_ROLL / 2)
                                    : (STATUS_Y + HEALTH_WORKOUT_STATUS_ROLL / 2)) +
                         offset_y;
  graphics_context_set_fill_color(ctx, BACKGROUND_COLOR);
  graphics_fill_rect(ctx, &GRect(0, top, PBL_DISPLAY_WIDTH, bottom - top));
  prv_draw_status(ctx, screen, offset_y, i18n_owner);
  if (with_time) {
    prv_draw_time(ctx, screen, offset_y);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Measures

typedef enum WorkoutMeasure {
  WorkoutMeasureDistance,
  WorkoutMeasurePace,
  WorkoutMeasureSteps,
  WorkoutMeasureCadence,
  WorkoutMeasureCalories,
} WorkoutMeasure;

//! The measures of runs and walks, and of open workouts, which have no distance to pace
static const WorkoutMeasure s_paced_measures[] = {
  WorkoutMeasureDistance, WorkoutMeasurePace,     WorkoutMeasureSteps,
  WorkoutMeasureCadence,  WorkoutMeasureCalories,
};
static const WorkoutMeasure s_open_measures[] = {
  WorkoutMeasureSteps,
  WorkoutMeasureCadence,
  WorkoutMeasureCalories,
};

int health_orbit_workout_num_rows(ActivitySessionType type) {
  return (type == ActivitySessionType_Open) ? PBL_ARRAY_SIZE(s_open_measures)
                                            : PBL_ARRAY_SIZE(s_paced_measures);
}

//! A measure's label and color, and its value with its unit
static GColor prv_measure(WorkoutMeasure measure, const HealthWorkoutData *workout, char *label,
                          size_t label_size, char *value, size_t value_size, void *i18n_owner) {
  char number[16];
  switch (measure) {
    case WorkoutMeasureDistance:
      /// A workout's distance, on a row of the round Health app
      strncpy(label, pbl_i18n_get("Distance", i18n_owner), label_size);
      health_orbit_format_workout_distance(number, sizeof(number), workout->distance_m);
      snprintf(
          value, value_size, "%s %s", number,
          pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("mi"), pbl_i18n_noop("km")),
                       i18n_owner));
      return DISTANCE_COLOR;
    case WorkoutMeasurePace:
      /// A workout's average pace, on a row of the round Health app
      strncpy(label, pbl_i18n_get("Pace", i18n_owner), label_size);
      health_orbit_format_workout_pace(number, sizeof(number), workout->duration_s,
                                       workout->distance_m);
      snprintf(
          value, value_size, "%s %s", number,
          pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("/mi"), pbl_i18n_noop("/km")),
                       i18n_owner));
      return PACE_COLOR;
    case WorkoutMeasureSteps:
      /// A workout's steps, on a row of the round Health app
      strncpy(label, pbl_i18n_get("Steps", i18n_owner), label_size);
      health_orbit_format_count(value, value_size, workout->steps);
      return STEPS_COLOR;
    case WorkoutMeasureCadence:
      /// A workout's steps a minute, on a row of the round Health app
      strncpy(label, pbl_i18n_get("Cadence", i18n_owner), label_size);
      snprintf(value, value_size, "%" PRId32 " %s",
               health_orbit_workout_cadence(workout->steps, workout->duration_s),
               pbl_i18n_get("spm", i18n_owner));
      return STEPS_COLOR;
    case WorkoutMeasureCalories:
      /// A workout's active calories, on a row of the round Health app
      strncpy(label, pbl_i18n_get("Calories", i18n_owner), label_size);
      snprintf(value, value_size, "%" PRId32 " %s", workout->active_kcal,
               pbl_i18n_get("kcal", i18n_owner));
      return KCAL_COLOR;
  }
  return GColorWhite;
}

//! Draws a row of measures, its label on the left and its value on the right, its text showing as
//! far as its pill has grown
static void prv_draw_measure_row(GContext *ctx, const HealthOrbitWorkoutScreen *screen,
                                 WorkoutMeasure measure, const MenuRoundShape *pill,
                                 void *i18n_owner) {
  const GRect box = pill->box;
  const int16_t h = box.size.h;
  prv_fill_round_rect(ctx, box, pill->radius, ROW_COLOR);
  char label[20];
  char value[24];
  const GColor color = prv_measure(measure, &screen->workout, label, sizeof(label), value,
                                   sizeof(value), i18n_owner);
  label[sizeof(label) - 1] = '\0';
  const GDrawState state = health_orbit_clip(ctx, box);
  const int16_t mid_y = box.origin.y + h / 2;
  const HealthOrbitRun label_run = {label, &health_orbit_font_gothic_14_bold, color};
  const int16_t left = box.origin.x + h / 2 + ROW_TEXT_INSET;
  health_orbit_draw_runs(ctx, &label_run, 1, 0, left + health_orbit_run_width(ctx, &label_run) / 2,
                         mid_y);
  const HealthOrbitRun value_run = {value, &health_orbit_font_gothic_14_bold, GColorWhite};
  const int16_t right = grect_get_max_x(&box) - h / 2 - ROW_TEXT_INSET;
  health_orbit_draw_runs(ctx, &value_run, 1, 0, right - health_orbit_run_width(ctx, &value_run) / 2,
                         mid_y);
  graphics_context_set_drawing_state(ctx, state);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions

void health_orbit_workout_draw_kinds(GContext *ctx, const HealthOrbitWorkoutScreen *screen,
                                     const MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS],
                                     int16_t header_offset_y, const MenuRoundShape *highlight,
                                     int highlight_row, GDrawCommandImage *const *icons,
                                     void *i18n_owner) {
  graphics_context_set_antialiased(ctx, true);
  for (int i = HEALTH_WORKOUT_NUM_KINDS - 1; i >= 0; i--) {
    if (i == highlight_row && highlight && highlight->box.size.w > 0) {
      prv_fill_round_rect(ctx, grect_inset(highlight->box, GEdgeInsets(-KIND_RING_WIDTH)),
                          highlight->radius + KIND_RING_WIDTH, KIND_RING_COLOR);
    }
    const MenuRoundShape *pill = &rows[i];
    if (pill->box.size.w <= 0) {
      continue;
    }
    const GRect box = pill->box;
    // White, to read, the icon and the name in the kind's deeper color together in the middle of
    // the pill, as far as it has grown
    prv_fill_round_rect(ctx, box, pill->radius, KIND_ROW_COLOR);
    const ActivitySessionType type = health_orbit_workout_kinds[i];
    const HealthOrbitRun name = {
      pbl_i18n_get(health_orbit_workout_name(type), i18n_owner), &health_orbit_font_gothic_24_bold,
      health_orbit_workout_ink(type)
    };
    const GSize icon_size =
        icons && icons[i] ? gdraw_command_image_get_bounds_size(icons[i]) : GSizeZero;
    const int16_t name_w = health_orbit_run_width(ctx, &name);
    const int16_t width = icon_size.w + KIND_ICON_GAP + name_w;
    const int16_t left = box.origin.x + (box.size.w - width) / 2;
    const int16_t mid_y = box.origin.y + box.size.h / 2;
    const GDrawState state = health_orbit_clip(ctx, box);
    if (icon_size.w > 0) {
      gdraw_command_image_draw(ctx, icons[i], GPoint(left, mid_y - icon_size.h / 2));
    }
    health_orbit_draw_runs(ctx, &name, 1, 0, left + icon_size.w + KIND_ICON_GAP + name_w / 2,
                           mid_y);
    graphics_context_set_drawing_state(ctx, state);
  }
  // Counting down to the kind chosen, as the rows fold away behind it
  const bool counting_down = (screen->countdown > 0) || (screen->countdown_from > 0);
  prv_draw_header(ctx, screen, header_offset_y, counting_down, i18n_owner);
  graphics_context_set_antialiased(ctx, false);
}

void health_orbit_workout_draw(GContext *ctx, const HealthOrbitWorkoutScreen *screen,
                               const MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS],
                               int16_t header_offset_y, void *i18n_owner) {
  graphics_context_set_antialiased(ctx, true);
  const bool open = (screen->type == ActivitySessionType_Open);
  const WorkoutMeasure *measures = open ? s_open_measures : s_paced_measures;
  const int num_rows = health_orbit_workout_num_rows(screen->type);
  // The lowest first, so a row coming out of the notch passes under the rows already out
  for (int i = num_rows - 1; i >= 0; i--) {
    if (rows[i].box.size.w > 0) {
      prv_draw_measure_row(ctx, screen, measures[i], &rows[i], i18n_owner);
    }
  }
  prv_draw_header(ctx, screen, header_offset_y, true /* with_time */, i18n_owner);
  graphics_context_set_antialiased(ctx, false);
}

#endif
