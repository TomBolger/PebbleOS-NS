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

//! The bubbles below the numbers: the middle row of the three from the middle of the display, and
//! each one's center from where they are centered on that row
#define BUBBLES_Y 67
static const GPoint s_bubble_offsets[HEALTH_ORBIT_CARD_MAX_BUBBLES] = {
  {-48, -11},
  {0, 11},
  {48, -11},
};

#define TYPICAL_COLOR GColorYellow

//! The steps of the walks and runs shade the steps ring a step deeper, in the Steps section's
//! color, and deep sleep the sleep ring in the color the Sleep card's deep sleep bubble has
#define WALK_SHADE_COLOR       GColorIslamicGreen
#define DEEP_SLEEP_SHADE_COLOR GColorVeryLightBlue

//! The Workout ring fills over an hour
#define WORKOUT_RING_FULL_S      PBL_SEC_PER_HOUR
#define WORKOUT_IDLE_TRACK_COLOR GColorWindsorTan

//! With no workout going on: the middle of the run shoe and of the line below it, from the middle
//! of the display
#define WORKOUT_SHOE_Y (-14)
#define WORKOUT_LINE_Y 46

//! Colors of the workout's measures, as the Workout app labels them
#define WORKOUT_DISTANCE_COLOR GColorCyan
#define WORKOUT_PACE_COLOR     GColorGreen
#define WORKOUT_STEPS_COLOR    GColorShockingPink
#define WORKOUT_KCAL_COLOR     GColorOrange
#define WORKOUT_PAUSED_COLOR   GColorChromeYellow

//! PebbleOS has no sleep goal, so the Sleep ring fills toward the typical sleep, or toward this
//! much sleep until there is a typical night
#define SLEEP_RING_FALLBACK_TARGET (8 * PBL_SEC_PER_HOUR)

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
  const int16_t row = display.h / 2 + BUBBLES_Y;
  const GPoint middle = GPoint(health_orbit_middle_x(display, CARD_INNER_RADIUS, row), row);
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
  //! Stretches of the value shaded where they came along the fill, in units of \a span_unit of it
  const HealthSpans *spans;
  int32_t span_unit;
  GColor span_color;
} OrbitRing;

//! The Steps ring: toward the typical steps of a whole day, with a tick at the typical steps by
//! now, the steps of the walks and runs shaded
static OrbitRing prv_steps_ring(const HealthDayData *day, int16_t outer_radius, int16_t thickness) {
  return (OrbitRing){
    .outer_radius = outer_radius,
    .thickness = thickness,
    .color = GColorScreaminGreen,
    .track_color = GColorDarkGreen,
    .value = day->steps,
    .full = PBL_MAX(day->typical_steps, day->steps),
    .typical = day->typical_steps_by_now,
    .spans = &day->walks,
    .span_unit = 1,
    .span_color = WALK_SHADE_COLOR,
  };
}

//! The Sleep ring: the night's sleep toward the typical sleep, which it has a tick at, the deep
//! sleep shaded
static OrbitRing prv_sleep_ring(const HealthDayData *day, int16_t outer_radius, int16_t thickness,
                                bool typical_tick) {
  const int32_t target = (day->typical_sleep > 0) ? day->typical_sleep : SLEEP_RING_FALLBACK_TARGET;
  return (OrbitRing){
    .outer_radius = outer_radius,
    .thickness = thickness,
    .color = GColorVividCerulean,
    .track_color = GColorDukeBlue,
    .value = day->sleep,
    .full = PBL_MAX(target, day->sleep),
    .typical = typical_tick ? day->typical_sleep : 0,
    .spans = &day->deep_sleep_spans,
    .span_unit = PBL_SEC_PER_MIN,
    .span_color = DEEP_SLEEP_SHADE_COLOR,
  };
}

//! Color of a kind of workout's ring track
static GColor prv_workout_track_color(ActivitySessionType type) {
  switch (type) {
    case ActivitySessionType_Run:
      return GColorWindsorTan;
    case ActivitySessionType_Walk:
      return GColorDukeBlue;
    default:
      break;
  }
  return GColorDarkGreen;
}

//! The Workout ring: the time it has gone on toward an hour, all of it past an hour, as there is
//! no goal for a workout; with none going on, its track alone
static OrbitRing prv_workout_ring(const HealthWorkoutData *workout) {
  const bool going_on = (workout->state != HealthWorkoutStateNone);
  return (OrbitRing){
    .outer_radius = CARD_RING_OUTER,
    .thickness = CARD_RING_THICKNESS,
    .color = health_orbit_workout_color(workout->type),
    .track_color = going_on ? prv_workout_track_color(workout->type) : WORKOUT_IDLE_TRACK_COLOR,
    .value = going_on ? workout->duration_s : 0,
    .full = PBL_MAX(WORKOUT_RING_FULL_S, workout->duration_s),
  };
}

//! Whether a card is laid out as the Today card: today's, or a day's overview
static bool prv_is_day_card(const HealthOrbitCard *card) {
  return (card->section == HealthOrbitSectionToday) || card->day;
}

//! The day a card shows: its own, or today, read into \a today
static const HealthDayData *prv_get_day(const HealthOrbitCard *card, HealthData *health_data,
                                        HealthDayData *today) {
  if (card->day) {
    return card->day;
  }
  health_data_day_get(health_data, 0, today);
  return today;
}

//! A card's rings, which point into \a today for today's
static int prv_get_rings(const HealthOrbitCard *card, HealthData *health_data, HealthDayData *today,
                         OrbitRing rings_out[HEALTH_ORBIT_CARD_MAX_RINGS]) {
  const HealthDayData *day = prv_get_day(card, health_data, today);
  if (prv_is_day_card(card)) {
    rings_out[0] = prv_steps_ring(day, TODAY_STEPS_RING_OUTER, TODAY_RING_THICKNESS);
    rings_out[1] =
        prv_sleep_ring(day, TODAY_SLEEP_RING_OUTER, TODAY_RING_THICKNESS, false /* typical_tick */);
    return 2;
  }
  switch (card->section) {
    case HealthOrbitSectionSteps:
      rings_out[0] = prv_steps_ring(day, CARD_RING_OUTER, CARD_RING_THICKNESS);
      return 1;
    case HealthOrbitSectionSleep:
      rings_out[0] =
          prv_sleep_ring(day, CARD_RING_OUTER, CARD_RING_THICKNESS, true /* typical_tick */);
      return 1;
    case HealthOrbitSectionWorkout:
      rings_out[0] = prv_workout_ring(health_data_workout_get(health_data));
      return 1;
    case HealthOrbitSectionToday:
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

//! Shades the stretches of the value along the fill, as far as it has swept from \a start: square
//! across the band, round where the fill's own ends are
static void prv_fill_spans(GContext *ctx, GPoint center, const OrbitRing *ring, int32_t start,
                           int32_t end, int32_t sweep) {
  if (!ring->spans) {
    return;
  }
  const uint16_t inner = ring->outer_radius - ring->thickness;
  graphics_context_set_fill_color(ctx, ring->span_color);
  for (int i = 0; i < ring->spans->count; i++) {
    const HealthSpan *span = &ring->spans->spans[i];
    int32_t from;
    int32_t to;
    if (!health_orbit_ring_span(start, end, ring->full, sweep, span->from * ring->span_unit,
                                span->to * ring->span_unit, &from, &to)) {
      continue;
    }
    graphics_fill_radial_internal(ctx, center, inner, ring->outer_radius, start + from, start + to);
    // An end of the fill is the stretch's: its round cap, a disc at that end, in its color
    if (from == 0) {
      prv_fill_band(ctx, center, ring, start, start);
    }
    if (to == sweep) {
      prv_fill_band(ctx, center, ring, start + sweep, start + sweep);
    }
  }
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
    prv_fill_spans(ctx, center, ring, start, end, sweep);
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
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y),
                         middle + CARD_TITLE_Y + dy);

  char count[16];
  health_orbit_format_count(count, sizeof(count), steps);
  health_orbit_draw_text(ctx, count, &health_orbit_font_leco_38_bold, GColorScreaminGreen,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_VALUE_Y),
                         middle + CARD_VALUE_Y + dy);

  // There is no step goal, so the ring fills toward the typical steps of a whole day
  char typical_day_text[16];
  health_orbit_format_count(typical_day_text, sizeof(typical_day_text), typical_day);
  char of_text[32];
  /// Below the step count: the typical steps of a whole day, as "of 9,120"
  snprintf(of_text, sizeof(of_text), pbl_i18n_get("of %s", i18n_owner),
           (typical_day > 0) ? typical_day_text : EM_DASH);
  health_orbit_draw_text(ctx, of_text, &health_orbit_font_gothic_18_bold, GColorLightGray,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_SUB_Y),
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
      GPoint(health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_PILL_Y),
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

//! The date \a days_ago days before today, as "WED 25 NOV"
static void prv_format_date(char *buffer, size_t size, int days_ago) {
  struct pbl_tm local_tm;
  health_orbit_day_time(days_ago, &local_tm);
  char weekday[8];
  char month[8];
  pbl_strftime_r(weekday, sizeof(weekday), "%a", &local_tm);
  pbl_strftime_r(month, sizeof(month), "%b", &local_tm);
  snprintf(buffer, size, "%s %d %s", weekday, local_tm.tm_mday, month);
  for (char *c = buffer; *c; c++) {
    *c = toupper((unsigned char)*c);
  }
}

//! The Today card, or the overview of a day before today laid out as it, titled with its date
static void prv_draw_today(GContext *ctx, GSize display, const HealthOrbitCard *card,
                           HealthData *health_data, void *i18n_owner) {
  const int16_t middle = display.h / 2;
  const int16_t dy = card->offset_y;
  const int16_t inner = TODAY_INNER_RADIUS;
  HealthDayData today;
  const HealthDayData *day = prv_get_day(card, health_data, &today);

  char date[24];
  prv_format_date(date, sizeof(date), day->days_ago);
  health_orbit_draw_text(ctx, date, &health_orbit_font_gothic_14_bold, GColorLightGray,
                         health_orbit_middle_x(display, inner, middle + TODAY_DATE_Y),
                         middle + TODAY_DATE_Y + dy);

  char steps[16];
  health_orbit_format_count(steps, sizeof(steps), day->steps);
  const HealthOrbitRun steps_runs[] = {
    {steps, &health_orbit_font_leco_28_light, GColorScreaminGreen},
    /// After the step count on the Today card of the round Health app
    {pbl_i18n_get("steps", i18n_owner), &health_orbit_font_gothic_18_bold, GColorWhite},
  };
  health_orbit_draw_runs(ctx, steps_runs, PBL_ARRAY_SIZE(steps_runs), TODAY_LABEL_GAP,
                         health_orbit_middle_x(display, inner, middle + TODAY_STEPS_Y),
                         middle + TODAY_STEPS_Y + dy);

  const int32_t sleep = day->sleep;
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
                         health_orbit_middle_x(display, inner, middle + TODAY_SLEEP_Y),
                         middle + TODAY_SLEEP_Y + dy);

  // The sleep against the typical sleep and the deep sleep, the deep sleep alone if both do not fit
  const int32_t typical = day->typical_sleep;
  char deep[16];
  health_orbit_format_duration(deep, sizeof(deep), day->deep_sleep, i18n_owner);
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
  const int16_t detail_x = health_orbit_middle_x(display, inner, middle + TODAY_DETAIL_Y);
  const int16_t room =
      2 * PBL_MIN(detail_x - range.origin_x, range.origin_x + range.size_w - detail_x);
  const HealthOrbitRun detail_run = {detail, &health_orbit_font_gothic_14, GColorLightGray};
  if (detail[0] == '\0' || health_orbit_run_width(ctx, &detail_run) > room) {
    /// The deep sleep, as "deep 1h 40m"
    snprintf(detail, sizeof(detail), pbl_i18n_get("deep %s", i18n_owner), deep);
  }
  health_orbit_draw_text(ctx, detail, &health_orbit_font_gothic_14, GColorLightGray, detail_x,
                         middle + TODAY_DETAIL_Y + dy);

  char distance[HEALTH_WHOLE_AND_DECIMAL_LENGTH];
  health_util_format_whole_and_decimal(distance, sizeof(distance), day->distance_meters,
                                       health_util_get_distance_factor());
  char active[12];
  snprintf(active, sizeof(active), "%d", (int)(day->active_seconds / PBL_SEC_PER_MIN));
  const HealthOrbitRun pill_runs[] = {
    {distance, &health_orbit_font_gothic_18_bold, GColorWhite},
    {pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("mi"), pbl_i18n_noop("km")),
                  i18n_owner),
     &health_orbit_font_gothic_14, GColorLightGray},
    {"·", &health_orbit_font_gothic_14, GColorLightGray},
    {active, &health_orbit_font_gothic_18_bold, GColorWhite},
    {pbl_i18n_get("min", i18n_owner), &health_orbit_font_gothic_14, GColorLightGray},
  };
  const GPoint pill_center = GPoint(health_orbit_middle_x(display, inner, middle + TODAY_PILL_Y),
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
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y),
                         middle + CARD_TITLE_Y + dy);

  prv_draw_sleep_duration(ctx, sleep,
                          health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_VALUE_Y),
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
                           health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_SUB_Y),
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
      GPoint(health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_PILL_Y),
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
// Workout

const ActivitySessionType health_orbit_workout_kinds[3] = {
  ActivitySessionType_Run,
  ActivitySessionType_Walk,
  ActivitySessionType_Open,
};

GColor health_orbit_workout_color(ActivitySessionType type) {
  switch (type) {
    case ActivitySessionType_Run:
      return GColorChromeYellow;
    case ActivitySessionType_Walk:
      return GColorPictonBlue;
    default:
      break;
  }
  return GColorScreaminGreen;
}

GColor health_orbit_workout_ink(ActivitySessionType type) {
  switch (type) {
    case ActivitySessionType_Run:
      return GColorOrange;
    case ActivitySessionType_Walk:
      return GColorCobaltBlue;
    default:
      break;
  }
  return GColorIslamicGreen;
}

const char *health_orbit_workout_name(ActivitySessionType type) {
  switch (type) {
    case ActivitySessionType_Run:
      /// A kind of workout of the round Health app
      return pbl_i18n_noop("Run");
    case ActivitySessionType_Walk:
      /// A kind of workout of the round Health app
      return pbl_i18n_noop("Walk");
    default:
      break;
  }
  /// The kind of workout that is neither a run nor a walk, in the round Health app
  return pbl_i18n_noop("Workout");
}

//! No workout going on: the Workout app's run shoe, large in the middle, over a line saying to
//! start one
static void prv_draw_workout_idle(GContext *ctx, GSize display, const HealthOrbitCard *card,
                                  void *i18n_owner) {
  const int16_t middle = display.h / 2;
  const int16_t dy = card->offset_y;

  /// The title of the Workout card of the round Health app
  health_orbit_draw_text(ctx, pbl_i18n_get("WORKOUT", i18n_owner),
                         &health_orbit_font_gothic_14_bold, GColorRajah,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y),
                         middle + CARD_TITLE_Y + dy);

  if (card->run_icon) {
    const GSize size = gdraw_command_image_get_bounds_size(card->run_icon);
    const int16_t y = middle + WORKOUT_SHOE_Y;
    gdraw_command_image_draw(
        ctx, card->run_icon,
        GPoint(health_orbit_middle_x(display, CARD_INNER_RADIUS, y) - size.w / 2,
               y - size.h / 2 + dy));
  }

  /// Below the run shoe on the Workout card of the round Health app, with no workout going on
  health_orbit_draw_text(ctx, pbl_i18n_get("Start a workout", i18n_owner),
                         &health_orbit_font_gothic_18_bold, GColorLightGray,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + WORKOUT_LINE_Y),
                         middle + WORKOUT_LINE_Y + dy);
}

void health_orbit_format_workout_time(char *buffer, size_t size, int32_t seconds) {
  const int s = (int)PBL_MAX(seconds, 0);
  if (s >= PBL_SEC_PER_HOUR) {
    snprintf(buffer, size, "%d:%02d:%02d", s / PBL_SEC_PER_HOUR,
             (s / PBL_SEC_PER_MIN) % PBL_MIN_PER_HOUR, s % PBL_SEC_PER_MIN);
  } else {
    snprintf(buffer, size, "%d:%02d", s / PBL_SEC_PER_MIN, s % PBL_SEC_PER_MIN);
  }
}

void health_orbit_format_workout_distance(char *buffer, size_t size, int32_t meters) {
  health_util_format_whole_and_decimal(buffer, size, meters, health_util_get_distance_factor());
}

void health_orbit_format_workout_pace(char *buffer, size_t size, int32_t duration_s,
                                      int32_t distance_m) {
  const int32_t pace = (duration_s > 0 && distance_m > 0)
                           ? (int32_t)health_util_get_pace(duration_s, distance_m)
                           : 0;
  if (pace <= 0 || pace >= PBL_SEC_PER_HOUR) {
    strncpy(buffer, EN_DASH, size);
    return;
  }
  snprintf(buffer, size, "%d:%02d", (int)(pace / PBL_SEC_PER_MIN), (int)(pace % PBL_SEC_PER_MIN));
}

int32_t health_orbit_workout_cadence(int32_t steps, int32_t duration_s) {
  return (duration_s > 0) ? ((steps * PBL_SEC_PER_MIN + duration_s / 2) / duration_s) : 0;
}

//! A workout going on, or the one the watch detected: its kind, its time, how it started or that it
//! is paused, and its measures
static void prv_draw_workout_going_on(GContext *ctx, GSize display, const HealthOrbitCard *card,
                                      const HealthWorkoutData *workout, void *i18n_owner) {
  const int16_t middle = display.h / 2;
  const int16_t dy = card->offset_y;
  const bool paused = (workout->state == HealthWorkoutStatePaused);
  const GColor color = health_orbit_workout_color(workout->type);

  char title[24];
  strncpy(title, pbl_i18n_get(health_orbit_workout_name(workout->type), i18n_owner),
          sizeof(title) - 1);
  title[sizeof(title) - 1] = '\0';
  for (char *c = title; *c; c++) {
    *c = toupper((unsigned char)*c);
  }
  health_orbit_draw_text(ctx, title, &health_orbit_font_gothic_14_bold, color,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y),
                         middle + CARD_TITLE_Y + dy);

  char time[16];
  health_orbit_format_workout_time(time, sizeof(time), workout->duration_s);
  health_orbit_draw_text(ctx, time, &health_orbit_font_leco_38_bold,
                         paused ? WORKOUT_PAUSED_COLOR : GColorWhite,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_VALUE_Y),
                         middle + CARD_VALUE_Y + dy);

  const char *how;
  if (paused) {
    /// Below a paused workout's time on the Workout card of the round Health app
    how = pbl_i18n_get("Paused", i18n_owner);
  } else if (workout->state == HealthWorkoutStateDetected || workout->auto_detected) {
    /// Below the time of a workout the watch detected on its own, in the round Health app
    how = pbl_i18n_get("Auto-detected", i18n_owner);
  } else {
    /// Below the time of a workout started from the round Health app or the Workout app
    how = pbl_i18n_get("Started manually", i18n_owner);
  }
  health_orbit_draw_text(ctx, how, &health_orbit_font_gothic_18_bold,
                         paused ? WORKOUT_PAUSED_COLOR : GColorLightGray,
                         health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_SUB_Y),
                         middle + CARD_SUB_Y + dy);

  // Runs and walks are paced, an open workout has its cadence
  const bool open = (workout->type == ActivitySessionType_Open);
  char measure[16];
  HealthOrbitRun pill_runs[3];
  if (open) {
    snprintf(measure, sizeof(measure), "%" PRId32,
             health_orbit_workout_cadence(workout->steps, workout->duration_s));
    pill_runs[0] = (HealthOrbitRun){
      /// Steps a minute of a workout, before its value
      pbl_i18n_get("CADENCE", i18n_owner), &health_orbit_font_gothic_14_bold, WORKOUT_STEPS_COLOR
    };
    /// Unit of the cadence, steps a minute
    pill_runs[2] = (HealthOrbitRun){
      pbl_i18n_get("spm", i18n_owner), &health_orbit_font_gothic_14, GColorLightGray
    };
  } else {
    health_orbit_format_workout_pace(measure, sizeof(measure), workout->duration_s,
                                     workout->distance_m);
    pill_runs[0] = (HealthOrbitRun){
      /// A workout's average time a mile or kilometer, before its value
      pbl_i18n_get("PACE", i18n_owner), &health_orbit_font_gothic_14_bold, WORKOUT_PACE_COLOR
    };
    pill_runs[2] = (HealthOrbitRun){
      pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("/mi"), pbl_i18n_noop("/km")),
                   i18n_owner),
      &health_orbit_font_gothic_14, GColorLightGray
    };
  }
  pill_runs[1] = (HealthOrbitRun){measure, &health_orbit_font_gothic_14_bold, GColorWhite};
  const GPoint pill_center =
      GPoint(health_orbit_middle_x(display, CARD_INNER_RADIUS, middle + CARD_PILL_Y),
             middle + CARD_PILL_Y + dy);
  prv_draw_pill(ctx, pill_center, CARD_PILL_W, CARD_PILL_H, prv_workout_track_color(workout->type),
                pill_runs, PBL_ARRAY_SIZE(pill_runs), 4);

  char distance[HEALTH_WHOLE_AND_DECIMAL_LENGTH];
  health_orbit_format_workout_distance(distance, sizeof(distance), workout->distance_m);
  char steps[16];
  health_orbit_format_count(steps, sizeof(steps), workout->steps);
  char kcal[12];
  snprintf(kcal, sizeof(kcal), "%" PRId32, workout->active_kcal);
  const OrbitBubble distance_bubble = {
    GColorDarkGray, distance,
    pbl_i18n_get(health_util_get_distance_string(pbl_i18n_noop("mi"), pbl_i18n_noop("km")),
                 i18n_owner),
    WORKOUT_DISTANCE_COLOR
  };
  /// Unit of a workout's steps in a bubble of the round Health app
  const OrbitBubble steps_bubble = {
    GColorDarkGray, steps, pbl_i18n_get("steps", i18n_owner), WORKOUT_STEPS_COLOR
  };
  const OrbitBubble kcal_bubble = {
    GColorDarkGray, kcal, pbl_i18n_get("kcal", i18n_owner), WORKOUT_KCAL_COLOR
  };
  const OrbitBubble bubbles[] = {
    open ? steps_bubble : distance_bubble,
    open ? kcal_bubble : steps_bubble,
    open ? distance_bubble : kcal_bubble,
  };
  prv_draw_bubbles(ctx, display, card, bubbles, PBL_ARRAY_SIZE(bubbles));
}

static void prv_draw_workout(GContext *ctx, GSize display, const HealthOrbitCard *card,
                             HealthData *health_data, void *i18n_owner) {
  const HealthWorkoutData *workout = health_data_workout_get(health_data);
  if (workout->state == HealthWorkoutStateNone) {
    prv_draw_workout_idle(ctx, display, card, i18n_owner);
  } else {
    prv_draw_workout_going_on(ctx, display, card, workout, i18n_owner);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions

GColor health_orbit_section_color(HealthOrbitSection section) {
  switch (section) {
    case HealthOrbitSectionSteps:
      return GColorIslamicGreen;
    case HealthOrbitSectionSleep:
      return GColorCobaltBlue;
    case HealthOrbitSectionWorkout:
      return GColorChromeYellow;
    case HealthOrbitSectionToday:
    case HealthOrbitSectionCount:
      break;
  }
  return GColorVividCerulean;
}

GColor health_orbit_section_bg_color(HealthOrbitSection section) {
  return (section == HealthOrbitSectionSleep) ? GColorOxfordBlue : GColorBlack;
}

int health_orbit_card_get_num_bubbles(const HealthOrbitCard *card) {
  return prv_is_day_card(card) ? 0 : HEALTH_ORBIT_CARD_MAX_BUBBLES;
}

void health_orbit_card_init(HealthOrbitCard *card, HealthOrbitSection section,
                            const HealthDayData *day) {
  *card = (HealthOrbitCard){.section = section, .day = day};
  for (int i = 0; i < HEALTH_ORBIT_CARD_MAX_RINGS; i++) {
    card->sweep[i] = HEALTH_ORBIT_CARD_SWEEP_REST;
    card->track[i] = HEALTH_ORBIT_CARD_SWEEP_REST;
  }
  for (int i = 0; i < HEALTH_ORBIT_CARD_MAX_BUBBLES; i++) {
    card->bubble_radius[i] = HEALTH_ORBIT_CARD_BUBBLE_RADIUS;
  }
}

int health_orbit_card_get_sweeps(const HealthOrbitCard *card, HealthData *health_data,
                                 uint32_t sweeps_out[HEALTH_ORBIT_CARD_MAX_RINGS],
                                 uint32_t spans_out[HEALTH_ORBIT_CARD_MAX_RINGS]) {
  HealthDayData today;
  OrbitRing rings[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = prv_get_rings(card, health_data, &today, rings);
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
  if (card->day) {
    prv_draw_today(ctx, display, card, health_data, i18n_owner);
    graphics_context_set_antialiased(ctx, false);
    return;
  }
  switch (card->section) {
    case HealthOrbitSectionSteps:
      prv_draw_steps(ctx, display, card, health_data, i18n_owner);
      break;
    case HealthOrbitSectionSleep:
      prv_draw_sleep(ctx, display, card, health_data, i18n_owner);
      break;
    case HealthOrbitSectionWorkout:
      prv_draw_workout(ctx, display, card, health_data, i18n_owner);
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
  HealthDayData today;
  OrbitRing rings[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = prv_get_rings(card, health_data, &today, rings);
  graphics_context_set_antialiased(ctx, true);
  for (int i = 0; i < num_rings; i++) {
    prv_draw_ring(ctx, display, &rings[i], card->sweep[i], card->track[i], tracks);
  }
  graphics_context_set_antialiased(ctx, false);
}

#endif
