/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_card.h"

#include "orbit_layout.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <pbl/i18n/i18n.h>
#include <pbl/services/activity/health_util.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/trig.h>

#include <applib/fonts/codepoint.h>
#include <applib/fonts/fonts.h>
#include <applib/graphics/graphics_circle.h>
#include <applib/graphics/text.h>

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

//! Space between the text and the ends of a pill
#define PILL_PADDING 6

//! The typical tick across a ring
#define TICK_WIDTH 3

//! The bubbles below the numbers, from the middle of the display
static const GPoint s_bubble_offsets[HEALTH_ORBIT_CARD_MAX_BUBBLES] = {
  {-64, 56},
  {-16, 78},
  {32, 56},
};

#define TYPICAL_COLOR GColorYellow

////////////////////////////////////////////////////////////////////////////////////////////////////
// Text

//! A font with the rows of its cap tops and baselines in its line, to line text up by its capitals
typedef struct OrbitFont {
  const char *key;
  int8_t cap_top;
  int8_t baseline;
} OrbitFont;

static const OrbitFont s_gothic_14 = {FONT_KEY_GOTHIC_14, 5, 14};
static const OrbitFont s_gothic_14_bold = {FONT_KEY_GOTHIC_14_BOLD, 5, 14};
static const OrbitFont s_gothic_18_bold = {FONT_KEY_GOTHIC_18_BOLD, 7, 18};
static const OrbitFont s_leco_38_bold = {FONT_KEY_LECO_38_BOLD_NUMBERS, 11, 38};

//! A run of text in one font and color
typedef struct OrbitRun {
  const char *text;
  const OrbitFont *font;
  GColor color;
} OrbitRun;

static int16_t prv_run_width(GContext *ctx, const OrbitRun *run) {
  GFont font = fonts_get_system_font(run->font->key);
  return graphics_text_layout_get_max_used_size(
             ctx, run->text, font, GRect(0, 0, PBL_DISPLAY_WIDTH, 2 * run->font->baseline),
             GTextOverflowModeFill, GTextAlignmentLeft, nullptr)
      .w;
}

static int16_t prv_runs_width(GContext *ctx, const OrbitRun *runs, int num_runs, int16_t gap) {
  int16_t width = 0;
  for (int i = 0; i < num_runs; i++) {
    width += prv_run_width(ctx, &runs[i]) + ((i > 0) ? gap : 0);
  }
  return width;
}

//! Draws runs side by side \a gap apart on one baseline, centered on \a center_x, the middle of the
//! tallest capitals on \a mid_y
static void prv_draw_runs(GContext *ctx, const OrbitRun *runs, int num_runs, int16_t gap,
                          int16_t center_x, int16_t mid_y) {
  int16_t cap = 0;
  for (int i = 0; i < num_runs; i++) {
    cap = PBL_MAX(cap, runs[i].font->baseline - runs[i].font->cap_top);
  }
  const int16_t baseline = mid_y + (cap + 1) / 2;
  int16_t x = center_x - prv_runs_width(ctx, runs, num_runs, gap) / 2;
  for (int i = 0; i < num_runs; i++) {
    const OrbitFont *font = runs[i].font;
    const int16_t width = prv_run_width(ctx, &runs[i]);
    graphics_context_set_text_color(ctx, runs[i].color);
    graphics_draw_text(ctx, runs[i].text, fonts_get_system_font(font->key),
                       GRect(x, baseline - font->baseline, width + 1, 2 * font->baseline),
                       GTextOverflowModeFill, GTextAlignmentLeft, nullptr);
    x += width + gap;
  }
}

static void prv_draw_text(GContext *ctx, const char *text, const OrbitFont *font, GColor color,
                          int16_t center_x, int16_t mid_y) {
  prv_draw_runs(ctx, &(OrbitRun){text, font, color}, 1, 0, center_x, mid_y);
}

//! Middle of where a line \a height tall runs on row \a y inside the ring
static int16_t prv_line_center_x(GSize display, int16_t inner_radius, int16_t y, int16_t height) {
  return health_orbit_text_center_x(display, inner_radius, y - height / 2, y + height / 2);
}

//! A count with its thousands apart, as "7,842"
static void prv_format_count(char *buffer, size_t size, int32_t count) {
  if (count >= 1000) {
    snprintf(buffer, size, "%" PRId32 ",%03" PRId32, count / 1000, count % 1000);
  } else {
    snprintf(buffer, size, "%" PRId32, PBL_MAX(count, 0));
  }
}

//! A count's difference from another, signed, as "+932"
static void prv_format_count_delta(char *buffer, size_t size, int32_t delta) {
  buffer[0] = (delta < 0) ? '-' : '+';
  prv_format_count(buffer + 1, size - 1, PBL_ABS(delta));
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
                          const OrbitRun *runs, int num_runs, int16_t gap) {
  const int16_t needed = prv_runs_width(ctx, runs, num_runs, gap) + height + 2 * PILL_PADDING;
  prv_fill_pill(ctx, center, PBL_MAX(width, needed), height, color);
  prv_draw_runs(ctx, runs, num_runs, gap, center.x, center.y);
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
  const GDrawState state = graphics_context_get_drawing_state(ctx);
  GRect clip = GRect(center.x - radius, center.y - radius, 2 * radius, 2 * radius);
  clip.origin = gpoint_add(clip.origin, state.drawing_box.origin);
  grect_clip(&clip, &state.clip_box);
  ctx->draw_state.clip_box = clip;
  prv_draw_text(ctx, bubble->value, &s_gothic_18_bold, GColorWhite, center.x, center.y - 5);
  prv_draw_text(ctx, bubble->unit, &s_gothic_14, bubble->unit_color, center.x, center.y + 11);
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

static int prv_get_rings(HealthOrbitSection section, HealthData *health_data,
                         OrbitRing rings_out[HEALTH_ORBIT_CARD_MAX_RINGS]) {
  switch (section) {
    case HealthOrbitSectionSteps:
      rings_out[0] = prv_steps_ring(health_data, CARD_RING_OUTER, CARD_RING_THICKNESS);
      return 1;
    case HealthOrbitSectionToday:
    case HealthOrbitSectionSleep:
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

static void prv_draw_ring(GContext *ctx, GSize display, const OrbitRing *ring, uint32_t sweep_state,
                          bool track) {
  int32_t start;
  int32_t end;
  health_orbit_ring_angles(display, ring->outer_radius, ring->thickness, &start, &end);
  const GPoint center = prv_display_center(display);
  if (track) {
    graphics_context_set_fill_color(ctx, ring->track_color);
    prv_fill_band(ctx, center, ring, start, end);
  }
  const int32_t sweep = (sweep_state == HEALTH_ORBIT_CARD_SWEEP_REST)
                            ? health_orbit_ring_sweep(start, end, ring->value, ring->full)
                            : (int32_t)sweep_state;
  if (sweep > 0) {
    graphics_context_set_fill_color(ctx, ring->color);
    prv_fill_band(ctx, center, ring, start, start + sweep);
    const int32_t inner = health_orbit_ring_sweep(start, end, ring->inner_value, ring->full);
    if (inner > 0) {
      graphics_context_set_fill_color(ctx, ring->inner_color);
      prv_fill_band(ctx, center, ring, start, start + PBL_MIN(inner, sweep));
    }
  }
  if (track && ring->typical > 0) {
    const int32_t angle = start + health_orbit_ring_sweep(start, end, ring->typical, ring->full);
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
  prv_draw_text(ctx, pbl_i18n_get("STEPS TODAY", i18n_owner), &s_gothic_14_bold, GColorLightGray,
                prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_TITLE_Y, 12),
                middle + CARD_TITLE_Y + dy);

  char count[16];
  prv_format_count(count, sizeof(count), steps);
  prv_draw_text(ctx, count, &s_leco_38_bold, GColorScreaminGreen,
                prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_VALUE_Y, 28),
                middle + CARD_VALUE_Y + dy);

  // There is no step goal, so the ring fills toward the typical steps of a whole day
  char typical_day_text[16];
  prv_format_count(typical_day_text, sizeof(typical_day_text), typical_day);
  char of_text[32];
  /// Below the step count: the typical steps of a whole day, as "of 9,120"
  snprintf(of_text, sizeof(of_text), pbl_i18n_get("of %s", i18n_owner),
           (typical_day > 0) ? typical_day_text : EM_DASH);
  prv_draw_text(ctx, of_text, &s_gothic_18_bold, GColorLightGray,
                prv_line_center_x(display, CARD_INNER_RADIUS, middle + CARD_SUB_Y, 14),
                middle + CARD_SUB_Y + dy);

  char typical_text[16];
  char delta_text[16];
  prv_format_count(typical_text, sizeof(typical_text), typical_now);
  prv_format_count_delta(delta_text, sizeof(delta_text), steps - typical_now);
  const OrbitRun pill_runs[] = {
    /// Short for typical, before the typical steps by now
    {pbl_i18n_get("TYP", i18n_owner), &s_gothic_14_bold, TYPICAL_COLOR},
    {(typical_now > 0) ? typical_text : EM_DASH, &s_gothic_14_bold, GColorWhite},
    {delta_text, &s_gothic_14_bold, (steps >= typical_now) ? GColorScreaminGreen : GColorMelon},
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
  return (section == HealthOrbitSectionSteps) ? HEALTH_ORBIT_CARD_MAX_BUBBLES : 0;
}

void health_orbit_card_init(HealthOrbitCard *card, HealthOrbitSection section) {
  *card = (HealthOrbitCard){.section = section};
  for (int i = 0; i < HEALTH_ORBIT_CARD_MAX_RINGS; i++) {
    card->sweep[i] = HEALTH_ORBIT_CARD_SWEEP_REST;
  }
  for (int i = 0; i < HEALTH_ORBIT_CARD_MAX_BUBBLES; i++) {
    card->bubble_radius[i] = HEALTH_ORBIT_CARD_BUBBLE_RADIUS;
  }
}

int health_orbit_card_get_sweeps(HealthOrbitSection section, HealthData *health_data,
                                 uint32_t sweeps_out[HEALTH_ORBIT_CARD_MAX_RINGS]) {
  OrbitRing rings[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = prv_get_rings(section, health_data, rings);
  const GSize display = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  for (int i = 0; i < num_rings; i++) {
    int32_t start;
    int32_t end;
    health_orbit_ring_angles(display, rings[i].outer_radius, rings[i].thickness, &start, &end);
    sweeps_out[i] = health_orbit_ring_sweep(start, end, rings[i].value, rings[i].full);
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
    case HealthOrbitSectionToday:
    case HealthOrbitSectionSleep:
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
    prv_draw_ring(ctx, display, &rings[i], card->sweep[i], tracks);
  }
  graphics_context_set_antialiased(ctx, false);
}

#endif
