/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_card.h"

#include "orbit_draw.h"
#include "orbit_layout.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <pbl/i18n/i18n.h>
#include <pbl/services/activity/health_util.h>
#include <pbl/services/time.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/trig.h>

#include <applib/fonts/codepoint.h>
#include <applib/graphics/graphics_circle.h>

#if PBL_ROUND

////////////////////////////////////////////////////////////////////////////////////////////////////
// Layout

//! The ring of the Steps and Sleep cards, and the space inside it
#define CARD_RING_OUTER     127
#define CARD_RING_THICKNESS 14
#define CARD_INNER_RADIUS   (CARD_RING_OUTER - CARD_RING_THICKNESS)

//! Rows of the Steps and Sleep cards, from the middle of the display: the cap middle of each
//! line, and the pill's middle
#define CARD_TITLE_Y (-78)
#define CARD_VALUE_Y (-38)
#define CARD_SUB_Y   (-11)
#define CARD_PILL_Y  15
#define CARD_PILL_W  128
#define CARD_PILL_H  22

//! The two rings of the Today card, steps outside and sleep inside, and the space inside them
#define TODAY_STEPS_RING_OUTER 127
#define TODAY_SLEEP_RING_OUTER 112
#define TODAY_RING_THICKNESS   12
#define TODAY_INNER_RADIUS     (TODAY_SLEEP_RING_OUTER - TODAY_RING_THICKNESS)

//! Rows of the Today card, from the middle of the display
#define TODAY_DATE_Y   (-74)
#define TODAY_STEPS_Y  (-46)
#define TODAY_SLEEP_Y  (-8)
#define TODAY_DETAIL_Y 22
#define TODAY_PILL_Y   54
#define TODAY_PILL_W   124
#define TODAY_PILL_H   24

//! Space between the sleep's numbers and their units, and between a number and its word
#define TODAY_SLEEP_GAP 2
#define TODAY_LABEL_GAP 5

//! Space between the text and the ends of a pill
#define PILL_PADDING 6

//! Space between a bubble's text and its edge, and the rows of its value and unit from its middle
#define BUBBLE_PADDING 3
#define BUBBLE_VALUE_Y (-5)
#define BUBBLE_UNIT_Y  11

//! The typical tick across a ring
#define TICK_WIDTH 3

//! The bubbles below the numbers, from the middle of the display
static const GPoint s_bubble_offsets[HEALTH_ORBIT_CARD_MAX_BUBBLES] = {
  {-64, 56},
  {-16, 78},
  {32, 56},
};

#define TYPICAL_COLOR GColorYellow

//! PebbleOS has no sleep goal, so the Sleep ring fills toward the typical sleep, or toward this
//! much sleep until there is a typical night
#define SLEEP_RING_FALLBACK_TARGET (8 * PBL_SEC_PER_HOUR)

////////////////////////////////////////////////////////////////////////////////////////////////////
// Text

//! Middle of where a line \a height tall runs on row \a y inside the ring
static int16_t prv_line_center_x(GSize display, int16_t inner_radius, int16_t y, int16_t height) {
  return health_orbit_text_center_x(display, inner_radius, y - height / 2, y + height / 2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Shapes

static GPoint prv_display_center(GSize display) {
  return GPoint(display.w / 2, display.h / 2);
}

//! Fills a pill \a width wide and \a height tall centered on \a center, at least as wide as its
//! text needs
static void prv_fill_pill(GContext *ctx, GPoint center, int16_t width, int16_t height,
                          GColor color) {
  graphics_context_set_fill_color(ctx, color);
  const GRect box = GRect(center.x - width / 2, center.y - height / 2, width, height);
  graphics_fill_round_rect(ctx, &box, height / 2, GCornersAll);
}

//! Draws a pill with runs of text in it, widened if they need more room than \a width
static void prv_draw_pill(GContext *ctx, GPoint center, int16_t width, int16_t height, GColor color,
                          const HealthOrbitRun *runs, int num_runs, int16_t gap) {
  const int16_t needed =
      health_orbit_runs_width(ctx, runs, num_runs, gap) + height + 2 * PILL_PADDING;
  prv_fill_pill(ctx, center, PBL_MAX(width, needed), height, color);
  health_orbit_draw_runs(ctx, runs, num_runs, gap, center.x, center.y);
}

//! A bubble below the numbers: its value over its unit
typedef struct OrbitBubble {
  GColor color;
  const char *value;
  const char *unit;
  GColor unit_color;
} OrbitBubble;

//! Draws a bubble \a radius big, its text showing as far as it has grown
static void prv_draw_bubble(GContext *ctx, GPoint center, int16_t radius,
                            const OrbitBubble *bubble) {
  if (radius <= 0) {
    return;
  }
  graphics_context_set_fill_color(ctx, bubble->color);
  graphics_fill_circle(ctx, center, radius);
  const GDrawState state =
      health_orbit_clip(ctx, GRect(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
  // A value too wide for the bubble across the top of its capitals takes the smaller font
  const HealthOrbitRun value = {bubble->value, &health_orbit_font_gothic_18_bold, GColorWhite};
  const int32_t r = HEALTH_ORBIT_CARD_BUBBLE_RADIUS;
  const int32_t cap_top = BUBBLE_VALUE_Y - (value.font->baseline - value.font->cap_top + 1) / 2;
  const int16_t room = 2 * (integer_sqrt(r * r - cap_top * cap_top) - BUBBLE_PADDING);
  const HealthOrbitFont *font = (health_orbit_run_width(ctx, &value) <= room)
                                    ? value.font
                                    : &health_orbit_font_gothic_14_bold;
  health_orbit_draw_text(ctx, bubble->value, font, GColorWhite, center.x,
                         center.y + BUBBLE_VALUE_Y);
  health_orbit_draw_text(ctx, bubble->unit, &health_orbit_font_gothic_14, bubble->unit_color,
                         center.x, center.y + BUBBLE_UNIT_Y);
  graphics_context_set_drawing_state(ctx, state);
}

static void prv_draw_bubbles(GContext *ctx, GSize display, const HealthOrbitCard *card,
                             const OrbitBubble *bubbles, int num_bubbles) {
  const GPoint middle = prv_display_center(display);
  for (int i = 0; i < num_bubbles; i++) {
    const GPoint center =
        GPoint(middle.x + s_bubble_offsets[i].x, middle.y + s_bubble_offsets[i].y + card->offset_y);
    prv_draw_bubble(ctx, center, card->bubble_radius[i], &bubbles[i]);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Rings

//! A ring round the bezel, filling clockwise from below the sidebar toward \a full
typedef struct OrbitRing {
  int16_t outer_radius;
  int16_t thickness;
  GColor color;
  GColor track_color;
  int32_t value;
  int32_t full;
  //! Where the typical tick is, none if 0
  int32_t typical;
  //! How far a second fill reaches from the start, over the first, none if 0
  int32_t inner_value;
  GColor inner_color;
} OrbitRing;

//! The Steps ring: toward the typical steps of a whole day, with a tick at the typical steps by now
static OrbitRing prv_steps_ring(HealthData *health_data, int16_t outer_radius, int16_t thickness) {
  const int32_t steps = health_data_current_steps_get(health_data);
  return (OrbitRing){
    .outer_radius = outer_radius,
    .thickness = thickness,
    .color = GColorScreaminGreen,
    .track_color = GColorDarkGreen,
    .value = steps,
    .full = PBL_MAX(health_data_steps_get_cur_wday_average(health_data), steps),
    .typical = health_data_steps_get_current_average(health_data),
  };
}

//! The Sleep ring: last night's sleep toward the typical sleep, which it has a tick at, with the
//! deep sleep from its start
static OrbitRing prv_sleep_ring(HealthData *health_data, int16_t outer_radius, int16_t thickness,
                                bool typical_tick) {
  const int32_t sleep = health_data_current_sleep_get(health_data);
  const int32_t typical = health_data_sleep_get_cur_wday_average(health_data);
  const int32_t target = (typical > 0) ? typical : SLEEP_RING_FALLBACK_TARGET;
  return (OrbitRing){
    .outer_radius = outer_radius,
    .thickness = thickness,
    .color = GColorVividCerulean,
    .track_color = GColorDukeBlue,
    .value = sleep,
    .full = PBL_MAX(target, sleep),
    .typical = typical_tick ? typical : 0,
    .inner_value = health_data_current_deep_sleep_get(health_data),
    .inner_color = GColorVeryLightBlue,
  };
}

static int prv_get_rings(HealthOrbitSection section, HealthData *health_data,
                         OrbitRing rings_out[HEALTH_ORBIT_CARD_MAX_RINGS]) {
  switch (section) {
    case HealthOrbitSectionSteps:
      rings_out[0] = prv_steps_ring(health_data, CARD_RING_OUTER, CARD_RING_THICKNESS);
      return 1;
    case HealthOrbitSectionSleep:
      rings_out[0] = prv_sleep_ring(health_data, CARD_RING_OUTER, CARD_RING_THICKNESS,
                                    true /* typical_tick */);
      return 1;
    case HealthOrbitSectionToday:
      rings_out[0] = prv_steps_ring(health_data, TODAY_STEPS_RING_OUTER, TODAY_RING_THICKNESS);
      rings_out[1] = prv_sleep_ring(health_data, TODAY_SLEEP_RING_OUTER, TODAY_RING_THICKNESS,
                                    false /* typical_tick */);
      return 2;
    case HealthOrbitSectionCount:
      break;
  }
  return 0;
}

static void prv_fill_band(GContext *ctx, GPoint center, const OrbitRing *ring, int32_t from,
                          int32_t to) {
  graphics_fill_radial_capped_internal(ctx, center, ring->outer_radius - ring->thickness,
                                       ring->outer_radius, from, to);
}

//! The angle swept by a fill or track, as far as \a rest at rest
static int32_t prv_sweep(uint32_t state, int32_t rest) {
  return (state == HEALTH_ORBIT_CARD_SWEEP_REST) ? rest : (int32_t)state;
}

static void prv_draw_ring(GContext *ctx, GSize display, const OrbitRing *ring, uint32_t sweep_state,
                          uint32_t track_state, bool tracks) {
  int32_t start;
  int32_t end;
  health_orbit_ring_angles(display, ring->outer_radius, ring->thickness, &start, &end);
  const GPoint center = prv_display_center(display);
  // The track never runs past the end of the ring
  const int32_t track = tracks ? PBL_MIN(prv_sweep(track_state, end - start), end - start) : 0;
  if (track > 0) {
    graphics_context_set_fill_color(ctx, ring->track_color);
    prv_fill_band(ctx, center, ring, start, start + track);
  }
  const int32_t sweep =
      prv_sweep(sweep_state, health_orbit_ring_sweep(start, end, ring->value, ring->full));
  if (sweep > 0) {
    graphics_context_set_fill_color(ctx, ring->color);
    prv_fill_band(ctx, center, ring, start, start + sweep);
    const int32_t inner = health_orbit_ring_sweep(start, end, ring->inner_value, ring->full);
    if (inner > 0) {
      graphics_context_set_fill_color(ctx, ring->inner_color);
      prv_fill_band(ctx, center, ring, start, start + PBL_MIN(inner, sweep));
    }
  }
  const int32_t typical = health_orbit_ring_sweep(start, end, ring->typical, ring->full);
  if (ring->typical > 0 && track > 0 && typical <= track) {
    const int32_t angle = start + typical;
    const int32_t outer = ring->outer_radius + 1;
    const int32_t inner = ring->outer_radius - ring->thickness - 1;
    const int32_t sin = sin_lookup(angle);
    const int32_t cos = cos_lookup(angle);
    graphics_context_set_stroke_color(ctx, TYPICAL_COLOR);
    graphics_context_set_stroke_width(ctx, TICK_WIDTH);
    graphics_draw_line(
        ctx,
        GPoint(center.x + outer * sin / TRIG_MAX_RATIO, center.y - outer * cos / TRIG_MAX_RATIO),
        GPoint(center.x + inner * sin / TRIG_MAX_RATIO, center.y - inner * cos / TRIG_MAX_RATIO));
    graphics_context_set_stroke_width(ctx, 1);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Steps

static void prv_draw_steps(GContext *ctx, GSize display, const HealthOrbitCard *card,
                           HealthData *health_data, void *i18n_owner) {
  const int16_t middle = display.h / 2;
  const int16_t dy = card->offset_y;
  const int32_t steps = health_data_current_steps_get(health_data);
  const int32_t typical_day = health_data_steps_get_cur_wday_average(health_data);
  const int32_t typical_now = health_data_steps_get_current_average(health_data);

  /// The title of the Steps card of the round Health app
  health_orbit_draw_text(ctx, pbl_i18n_get("STEPS TODAY", i18n_owner),
                         &health_orbit_font_gothic_14_bold, GColorLightGray,
                         prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y, 12),
                         middle + CARD_TITLE_Y + dy);

  char count[16];
  health_orbit_format_count(count, sizeof(count), steps);
  health_orbit_draw_text(ctx, count, &health_orbit_font_leco_38_bold, GColorScreaminGreen,
                         prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_VALUE_Y, 28),
                         middle + CARD_VALUE_Y + dy);

  // There is no step goal, so the ring fills toward the typical steps of a whole day
  char typical_day_text[16];
  health_orbit_format_count(typical_day_text, sizeof(typical_day_text), typical_day);
  char of_text[32];
  /// Below the step count: the typical steps of a whole day, as "of 9,120"
  snprintf(of_text, sizeof(of_text), pbl_i18n_get("of %s", i18n_owner),
           (typical_day > 0) ? typical_day_text : EM_DASH);
  health_orbit_draw_text(ctx, of_text, &health_orbit_font_gothic_18_bold, GColorLightGray,
                         prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_SUB_Y, 14),
                         middle + CARD_SUB_Y + dy);

  char typical_text[16];
  char delta_text[16];
  health_orbit_format_count(typical_text, sizeof(typical_text), typical_now);
  health_orbit_format_count_delta(delta_text, sizeof(delta_text), steps - typical_now);
  const HealthOrbitRun pill_runs[] = {
    /// Short for typical, before the typical steps or sleep
    {pbl_i18n_get("TYP", i18n_owner), &health_orbit_font_gothic_14_bold, TYPICAL_COLOR},
    {(typical_now > 0) ? typical_text : EM_DASH, &health_orbit_font_gothic_14_bold, GColorWhite},
    {delta_text, &health_orbit_font_gothic_14_bold,
     (steps >= typical_now) ? GColorScreaminGreen : GColorMelon},
  };
  const int num_pill_runs = (typical_now > 0) ? 3 : 2;
  const GPoint pill_center =
      GPoint(prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_PILL_Y, CARD_PILL_H),
             middle + CARD_PILL_Y + dy);
  prv_draw_pill(ctx, pill_center, CARD_PILL_W, CARD_PILL_H, GColorArmyGreen, pill_runs,
                num_pill_runs, 5);

  char distance[HEALTH_WHOLE_AND_DECIMAL_LENGTH];
  const int32_t meters = health_data_current_distance_meters_get(health_data);
  if (meters > 0) {
    health_util_format_whole_and_decimal(distance, sizeof(distance), meters,
                                         health_util_get_distance_factor());
  } else {
    strncpy(distance, EN_DASH, sizeof(distance));
  }
  char active[12];
  const int32_t active_minutes =
      health_data_current_active_seconds_get(health_data) / PBL_SEC_PER_MIN;
  snprintf(active, sizeof(active), "%" PRId32, active_minutes);
  char calories[12];
  const int32_t kcal = health_data_current_calories_get(health_data);
  if (kcal > 0) {
    snprintf(calories, sizeof(calories), "%" PRId32, kcal);
  } else {
    strncpy(calories, EN_DASH, sizeof(calories));
  }
  const OrbitBubble bubbles[] = {
    {GColorDarkGray, distance,
     pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("mi"), pbl_i18n_noop("km")),
                  i18n_owner),
     GColorLightGray},
    /// Unit of the active minutes in a bubble of the round Health app
    {GColorWindsorTan, active, pbl_i18n_get("min", i18n_owner), GColorLightGray},
    /// Unit of the calories in a bubble of the round Health app
    {GColorDarkGray, calories, pbl_i18n_get("kcal", i18n_owner), GColorLightGray},
  };
  prv_draw_bubbles(ctx, display, card, bubbles, PBL_ARRAY_SIZE(bubbles));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Today

//! The date, as "WED 25 NOV"
static void prv_format_date(char *buffer, size_t size) {
  const time_t now = pbl_realtime();
  struct pbl_tm local_tm;
  pbl_localtime_r(&now, &local_tm);
  char weekday[8];
  char month[8];
  pbl_strftime_r(weekday, sizeof(weekday), "%a", &local_tm);
  pbl_strftime_r(month, sizeof(month), "%b", &local_tm);
  snprintf(buffer, size, "%s %d %s", weekday, local_tm.tm_mday, month);
  for (char *c = buffer; *c; c++) {
    *c = toupper((unsigned char)*c);
  }
}

static void prv_draw_today(GContext *ctx, GSize display, const HealthOrbitCard *card,
                           HealthData *health_data, void *i18n_owner) {
  const int16_t middle = display.h / 2;
  const int16_t dy = card->offset_y;
  const int16_t inner = TODAY_INNER_RADIUS;

  char date[24];
  prv_format_date(date, sizeof(date));
  health_orbit_draw_text(ctx, date, &health_orbit_font_gothic_14_bold, GColorLightGray,
                         prv_line_center_x(display, inner, middle + TODAY_DATE_Y, 12),
                         middle + TODAY_DATE_Y + dy);

  char steps[16];
  health_orbit_format_count(steps, sizeof(steps), health_data_current_steps_get(health_data));
  const HealthOrbitRun steps_runs[] = {
    {steps, &health_orbit_font_leco_28_light, GColorScreaminGreen},
    /// After the step count on the Today card of the round Health app
    {pbl_i18n_get("steps", i18n_owner), &health_orbit_font_gothic_18_bold, GColorWhite},
  };
  health_orbit_draw_runs(ctx, steps_runs, PBL_ARRAY_SIZE(steps_runs), TODAY_LABEL_GAP,
                         prv_line_center_x(display, inner, middle + TODAY_STEPS_Y, 22),
                         middle + TODAY_STEPS_Y + dy);

  const int32_t sleep = health_data_current_sleep_get(health_data);
  const int32_t minutes = sleep / PBL_SEC_PER_MIN;
  char hours_text[12];
  char minutes_text[12];
  snprintf(hours_text, sizeof(hours_text), "%d", (int)(minutes / PBL_MIN_PER_HOUR));
  snprintf(minutes_text, sizeof(minutes_text), "%02d", (int)(minutes % PBL_MIN_PER_HOUR));
  const GColor sleep_color = GColorVividCerulean;
  const HealthOrbitRun sleep_runs[] = {
    {hours_text, &health_orbit_font_leco_28_light, sleep_color},
    {pbl_i18n_get("h", i18n_owner), &health_orbit_font_gothic_18_bold, sleep_color},
    {minutes_text, &health_orbit_font_leco_28_light, sleep_color},
    {pbl_i18n_get("m", i18n_owner), &health_orbit_font_gothic_18_bold, sleep_color},
    /// After the hours and minutes slept on the Today card of the round Health app
    {pbl_i18n_get("sleep", i18n_owner), &health_orbit_font_gothic_18_bold, GColorWhite,
     TODAY_LABEL_GAP - TODAY_SLEEP_GAP},
  };
  health_orbit_draw_runs(ctx, sleep_runs, PBL_ARRAY_SIZE(sleep_runs), TODAY_SLEEP_GAP,
                         prv_line_center_x(display, inner, middle + TODAY_SLEEP_Y, 22),
                         middle + TODAY_SLEEP_Y + dy);

  // The sleep against the typical sleep and the deep sleep, the deep sleep alone if both do not fit
  const int32_t typical = health_data_sleep_get_cur_wday_average(health_data);
  char deep[16];
  health_orbit_format_duration(deep, sizeof(deep), health_data_current_deep_sleep_get(health_data),
                               i18n_owner);
  char detail[64];
  if (typical > 0) {
    /// Last night's sleep as a share of the typical sleep, and the deep sleep, as
    /// "93% of typical · deep 1h 40m"
    snprintf(detail, sizeof(detail), pbl_i18n_get("%d%% of typical · deep %s", i18n_owner),
             (int)(sleep * 100 / typical), deep);
  } else {
    detail[0] = '\0';
  }
  const GRangeHorizontal range = health_orbit_text_range(
      display, inner, middle + TODAY_DETAIL_Y - 7, middle + TODAY_DETAIL_Y + 7);
  const HealthOrbitRun detail_run = {detail, &health_orbit_font_gothic_14, GColorLightGray};
  if (detail[0] == '\0' || health_orbit_run_width(ctx, &detail_run) > range.size_w) {
    /// The deep sleep, as "deep 1h 40m"
    snprintf(detail, sizeof(detail), pbl_i18n_get("deep %s", i18n_owner), deep);
  }
  health_orbit_draw_text(ctx, detail, &health_orbit_font_gothic_14, GColorLightGray,
                         range.origin_x + range.size_w / 2, middle + TODAY_DETAIL_Y + dy);

  char distance[HEALTH_WHOLE_AND_DECIMAL_LENGTH];
  health_util_format_whole_and_decimal(distance, sizeof(distance),
                                       health_data_current_distance_meters_get(health_data),
                                       health_util_get_distance_factor());
  char active[12];
  snprintf(active, sizeof(active), "%d",
           (int)(health_data_current_active_seconds_get(health_data) / PBL_SEC_PER_MIN));
  const HealthOrbitRun pill_runs[] = {
    {distance, &health_orbit_font_gothic_18_bold, GColorWhite},
    {pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("mi"), pbl_i18n_noop("km")),
                  i18n_owner),
     &health_orbit_font_gothic_14, GColorLightGray},
    {"·", &health_orbit_font_gothic_14, GColorLightGray},
    {active, &health_orbit_font_gothic_18_bold, GColorWhite},
    {pbl_i18n_get("min", i18n_owner), &health_orbit_font_gothic_14, GColorLightGray},
  };
  const GPoint pill_center =
      GPoint(prv_line_center_x(display, inner, middle + TODAY_PILL_Y, TODAY_PILL_H),
             middle + TODAY_PILL_Y + dy);
  prv_draw_pill(ctx, pill_center, TODAY_PILL_W, TODAY_PILL_H, GColorDarkGray, pill_runs,
                PBL_ARRAY_SIZE(pill_runs), 3);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sleep

//! Last night's sleep in large numbers, as "6h52m"
static void prv_draw_sleep_duration(GContext *ctx, int32_t seconds, int16_t center_x, int16_t y,
                                    void *i18n_owner) {
  const GColor color = GColorVividCerulean;
  if (seconds <= 0) {
    health_orbit_draw_text(ctx, EM_DASH, &health_orbit_font_gothic_24_bold, color, center_x, y);
    return;
  }
  const int32_t minutes = seconds / PBL_SEC_PER_MIN;
  char hours_text[12];
  char minutes_text[12];
  snprintf(hours_text, sizeof(hours_text), "%d", (int)(minutes / PBL_MIN_PER_HOUR));
  snprintf(minutes_text, sizeof(minutes_text), (minutes >= PBL_MIN_PER_HOUR) ? "%02d" : "%d",
           (int)(minutes % PBL_MIN_PER_HOUR));
  const HealthOrbitRun runs[] = {
    {hours_text, &health_orbit_font_leco_38_bold, color},
    /// Short for hours, after the hours of sleep in large numbers
    {pbl_i18n_get("h", i18n_owner), &health_orbit_font_gothic_24_bold, color},
    {minutes_text, &health_orbit_font_leco_38_bold, color},
    /// Short for minutes, after the minutes of sleep in large numbers
    {pbl_i18n_get("m", i18n_owner), &health_orbit_font_gothic_24_bold, color},
  };
  // Under an hour, only the minutes
  const int first = (minutes >= PBL_MIN_PER_HOUR) ? 0 : 2;
  health_orbit_draw_runs(ctx, &runs[first], PBL_ARRAY_SIZE(runs) - first, 1, center_x, y);
}

static void prv_draw_sleep(GContext *ctx, GSize display, const HealthOrbitCard *card,
                           HealthData *health_data, void *i18n_owner) {
  const int16_t middle = display.h / 2;
  const int16_t dy = card->offset_y;
  const int32_t sleep = health_data_current_sleep_get(health_data);
  const int32_t typical = health_data_sleep_get_cur_wday_average(health_data);
  const int32_t deep = health_data_current_deep_sleep_get(health_data);

  /// The title of the Sleep card of the round Health app
  health_orbit_draw_text(ctx, pbl_i18n_get("LAST NIGHT", i18n_owner),
                         &health_orbit_font_gothic_14_bold, GColorBabyBlueEyes,
                         prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y, 12),
                         middle + CARD_TITLE_Y + dy);

  prv_draw_sleep_duration(ctx, sleep,
                          prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_VALUE_Y, 28),
                          middle + CARD_VALUE_Y + dy, i18n_owner);

  const int32_t start = health_data_sleep_get_start_time(health_data);
  const int32_t end = health_data_sleep_get_end_time(health_data);
  if (sleep > 0 && (start || end)) {
    char start_text[16];
    char end_text[16];
    health_orbit_format_time_of_day(start_text, sizeof(start_text), start);
    health_orbit_format_time_of_day(end_text, sizeof(end_text), end);
    char span[40];
    snprintf(span, sizeof(span), "%s " EN_DASH " %s", start_text, end_text);
    health_orbit_draw_text(ctx, span, &health_orbit_font_gothic_18_bold, GColorBabyBlueEyes,
                           prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_SUB_Y, 14),
                           middle + CARD_SUB_Y + dy);
  }

  char typical_text[16];
  health_orbit_format_duration(typical_text, sizeof(typical_text), typical, i18n_owner);
  char delta_text[16];
  delta_text[0] = (sleep < typical) ? '-' : '+';
  health_orbit_format_duration(delta_text + 1, sizeof(delta_text) - 1, sleep - typical, i18n_owner);
  const HealthOrbitRun pill_runs[] = {
    {pbl_i18n_get("TYP", i18n_owner), &health_orbit_font_gothic_14_bold, TYPICAL_COLOR},
    {(typical > 0) ? typical_text : EM_DASH, &health_orbit_font_gothic_14_bold, GColorWhite},
    {delta_text, &health_orbit_font_gothic_14_bold,
     (sleep < typical) ? GColorMelon : GColorScreaminGreen},
  };
  const int num_pill_runs = (typical > 0 && sleep > 0) ? 3 : 2;
  const GPoint pill_center =
      GPoint(prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_PILL_Y, CARD_PILL_H),
             middle + CARD_PILL_Y + dy);
  prv_draw_pill(ctx, pill_center, CARD_PILL_W, CARD_PILL_H, GColorLiberty, pill_runs, num_pill_runs,
                5);

  char deep_text[16];
  char share_text[16];
  char average_text[16];
  if (deep > 0) {
    health_orbit_format_short_duration(deep_text, sizeof(deep_text), deep, i18n_owner);
  } else {
    strncpy(deep_text, EN_DASH, sizeof(deep_text));
  }
  if (deep > 0 && sleep > 0) {
    snprintf(share_text, sizeof(share_text), "%d%%", (int)(PBL_MIN(deep, sleep) * 100 / sleep));
  } else {
    strncpy(share_text, EN_DASH, sizeof(share_text));
  }
  const int32_t average = health_data_sleep_get_monthly_average(health_data);
  if (average > 0) {
    health_orbit_format_short_duration(average_text, sizeof(average_text), average, i18n_owner);
  } else {
    strncpy(average_text, EN_DASH, sizeof(average_text));
  }
  /// Below the deep sleep in a bubble of the round Health app
  const char *deep_unit = pbl_i18n_get("deep", i18n_owner);
  const OrbitBubble bubbles[] = {
    {GColorVeryLightBlue, deep_text, deep_unit, GColorWhite},
    {GColorDukeBlue, share_text, deep_unit, GColorBabyBlueEyes},
    /// Below the 30 day average sleep in a bubble of the round Health app
    {GColorDukeBlue, average_text, pbl_i18n_get("30d", i18n_owner), GColorBabyBlueEyes},
  };
  prv_draw_bubbles(ctx, display, card, bubbles, PBL_ARRAY_SIZE(bubbles));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions

GColor health_orbit_section_color(HealthOrbitSection section) {
  switch (section) {
    case HealthOrbitSectionSteps:
      return GColorIslamicGreen;
    case HealthOrbitSectionSleep:
      return GColorCobaltBlue;
    case HealthOrbitSectionToday:
    case HealthOrbitSectionCount:
      break;
  }
  return GColorVividCerulean;
}

GColor health_orbit_section_bg_color(HealthOrbitSection section) {
  return (section == HealthOrbitSectionSleep) ? GColorOxfordBlue : GColorBlack;
}

int health_orbit_card_get_num_bubbles(HealthOrbitSection section) {
  return (section == HealthOrbitSectionToday) ? 0 : HEALTH_ORBIT_CARD_MAX_BUBBLES;
}

void health_orbit_card_init(HealthOrbitCard *card, HealthOrbitSection section) {
  *card = (HealthOrbitCard){.section = section};
  for (int i = 0; i < HEALTH_ORBIT_CARD_MAX_RINGS; i++) {
    card->sweep[i] = HEALTH_ORBIT_CARD_SWEEP_REST;
    card->track[i] = HEALTH_ORBIT_CARD_SWEEP_REST;
  }
  for (int i = 0; i < HEALTH_ORBIT_CARD_MAX_BUBBLES; i++) {
    card->bubble_radius[i] = HEALTH_ORBIT_CARD_BUBBLE_RADIUS;
  }
}

int health_orbit_card_get_sweeps(HealthOrbitSection section, HealthData *health_data,
                                 uint32_t sweeps_out[HEALTH_ORBIT_CARD_MAX_RINGS],
                                 uint32_t spans_out[HEALTH_ORBIT_CARD_MAX_RINGS]) {
  OrbitRing rings[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = prv_get_rings(section, health_data, rings);
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  for (int i = 0; i < num_rings; i++) {
    int32_t start;
    int32_t end;
    health_orbit_ring_angles(display, rings[i].outer_radius, rings[i].thickness, &start, &end);
    sweeps_out[i] = health_orbit_ring_sweep(start, end, rings[i].value, rings[i].full);
    if (spans_out) {
      spans_out[i] = end - start;
    }
  }
  return num_rings;
}

void health_orbit_card_draw_content(GContext *ctx, const HealthOrbitCard *card,
                                    HealthData *health_data, void *i18n_owner) {
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  graphics_context_set_antialiased(ctx, true);
  switch (card->section) {
    case HealthOrbitSectionSteps:
      prv_draw_steps(ctx, display, card, health_data, i18n_owner);
      break;
    case HealthOrbitSectionSleep:
      prv_draw_sleep(ctx, display, card, health_data, i18n_owner);
      break;
    case HealthOrbitSectionToday:
      prv_draw_today(ctx, display, card, health_data, i18n_owner);
      break;
    case HealthOrbitSectionCount:
      break;
  }
  graphics_context_set_antialiased(ctx, false);
}

void health_orbit_card_draw_rings(GContext *ctx, const HealthOrbitCard *card,
                                  HealthData *health_data, bool tracks) {
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  OrbitRing rings[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = prv_get_rings(card->section, health_data, rings);
  graphics_context_set_antialiased(ctx, true);
  for (int i = 0; i < num_rings; i++) {
    prv_draw_ring(ctx, display, &rings[i], card->sweep[i], card->track[i], tracks);
  }
  graphics_context_set_antialiased(ctx, false);
}

#endif
