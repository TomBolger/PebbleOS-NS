/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_view.h"

#include "orbit_card.h"
#include "orbit_draw.h"
#include "orbit_layout.h"
#include "orbit_week.h"
#include "orbit_workout.h"

#include <pbl/i18n/i18n.h>
#include <pbl/services/activity/workout_service.h>
#include <pbl/services/timeline/health_layout.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/trig.h>

#include <applib/app_launch_button.h>
#include <applib/app_launch_reason.h>
#include <applib/app_malloc.h>
#include <applib/app_timer.h>
#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/gdraw_command_transforms.h>
#include <applib/graphics/gpath.h>
#include <applib/graphics/graphics.h>
#include <applib/ui/animation_interpolate.h>
#include <applib/ui/animation_timing.h>
#include <applib/ui/app_window_stack.h>
#include <applib/ui/property_animation.h>
#include <applib/ui/ui.h>
#include <apps/system/timeline/animations.h>
#include <resource/resource_ids.auto.h>

#if PBL_ROUND

//! The section bubbles: the selected one at the notch, HEALTH_ORBIT_SELECTED_RADIUS big and white
//! with the section's icon in its color, the others black with their icon in their color
#define NEIGHBOUR_BUBBLE_RADIUS 12
#define SELECTED_ICON_SIZE      21
#define NEIGHBOUR_ICON_SIZE     13

//! The kinds of workout's icons, on the rows to start one
#define KIND_ICON_SIZE 28

//! The crown by the best day of the week
#define CROWN_SIZE GSize(13, 11)

//! A quarter turn of the sidebar, back from the right to the top
#define QUARTER_TURN (TRIG_MAX_ANGLE / 4)

typedef struct HealthOrbitIcon {
  GDrawCommandImage *selected;
  GDrawCommandImage *neighbour;
} HealthOrbitIcon;

struct HealthOrbitView {
  Window window;
  Layer layer;
  HealthData *health_data;
  HealthOrbitIcon icons[HealthOrbitSectionCount];
  GDrawCommandImage *crown;
  //! Section of the card shown, or coming in; with the days, the section they were opened from
  HealthOrbitSection section;
  HealthOrbitCard card;
  //! The card going out as another comes in
  HealthOrbitCard leaving;
  bool leaving_shown;
  //! Section whose colors the background has
  HealthOrbitSection color_section;
  //! Section whose color the sidebar had, as the section's color sweeps in over it
  HealthOrbitSection sidebar_from;
  //! When in the transition the background takes the new colors, the new card's rings their tracks
  //! from the leaving card's, and whether they have
  uint32_t color_swap_ms;
  bool colors_swapped;
  Animation *animation;
  //! Whether Up past the first section and Down past the last one wrap round
  bool wrap;

  //! How far along the orbit the sections have turned, HEALTH_ORBIT_STEP per section from the
  //! first, which is past a full turn when it goes on round to wrap
  int16_t orbit_offset;
  //! The turn of the orbit the sidebar's color sweeps in with: the sections', the days', or the
  //! swap between them, where it turns from and to, and whether as for Down, the bubbles coming up
  //! from below
  int16_t *orbit_motion;
  int16_t orbit_from;
  int16_t orbit_target;
  bool orbit_down;
  //! Angle the sidebar is turned by, back to the top as the week shows
  int16_t turn;
  //! Radius of the bubbles of the sections away from the notch
  int16_t neighbour_radius;

  //! Whether the overview of a day shows, chosen on the orbit of days in place of the sections, or
  //! is coming in
  bool days;
  //! Day shown, how many days before today, or coming in
  int16_t day;
  //! What a day before today shows, the card's and the leaving card's in turn, and which is the
  //! card's
  HealthDayData day_data[2];
  uint8_t day_slot;
  //! How far along the orbit the days have turned, HEALTH_ORBIT_STEP per day from today
  int16_t day_offset;
  //! How far the orbit has turned clockwise from the sections to the days, to HEALTH_ORBIT_SWAP
  int16_t swap;

  //! Whether the week of the section shows, in place of its card, or is coming in, or one of the
  //! workout's screens below the banner as the week is, and which
  bool week;
  HealthOrbitList list;
  //! What the workout screens show, and the icons of the kinds of workout
  HealthOrbitWorkoutScreen workout;
  GDrawCommandImage *kind_icons[HEALTH_WORKOUT_NUM_KINDS];
  //! The Workout card's large run shoe
  GDrawCommandImage *run_icon;
  //! Ticks every second the Workout section shows, to read the workout going on, and to count down
  //! to one
  AppTimer *workout_timer;
  //! Row of the week selected, the one it was on as the selection moves, and how far it has
  //! moved, down the rows from it
  int16_t week_selection;
  int16_t selection_from;
  int16_t selection_travel;
  //! Whether the card and the week are drawn, both as one turns into the other
  bool card_shown;
  bool week_shown;
  //! How far each row of the week has come from where it leaves the notch, or ROW_AT_REST
  int16_t row_travel[HEALTH_ORBIT_LIST_MAX_ROWS];
  GPoint row_start[HEALTH_ORBIT_LIST_MAX_ROWS];
  //! How far the line above the rows is below where it rests
  int16_t week_header_offset_y;
};

//! A week row where it rests
#define ROW_AT_REST INT16_MAX

_Static_assert(HealthOrbitSectionCount == HEALTH_ORBIT_NUM_SECTIONS, "orbit sections");
_Static_assert(HEALTH_ORBIT_WEEK_NUM_ROWS <= HEALTH_ORBIT_LIST_MAX_ROWS, "week rows");

//! A section's card at rest
static void prv_init_section_card(HealthOrbitView *view, HealthOrbitCard *card,
                                  HealthOrbitSection section) {
  health_orbit_card_init(card, section, nullptr);
  card->run_icon = view->run_icon;
}

//! Section whose colors show: the days have Today's, as their card is laid out as it
static HealthOrbitSection prv_color_section(const HealthOrbitView *view) {
  return view->days ? HealthOrbitSectionToday : view->section;
}

//! Whether the days were opened from the section's week, to go back to
static bool prv_days_from_week(const HealthOrbitView *view) {
  return view->days && health_orbit_week_has_section(view->section);
}

//! The card of the day: today's is the Today card, a day before it its overview of the day read
static void prv_init_day_card(HealthOrbitView *view, HealthOrbitCard *card) {
  health_orbit_card_init(card, HealthOrbitSectionToday,
                         (view->day > 0) ? &view->day_data[view->day_slot] : nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// The sidebar and the orbit

static uint32_t prv_icon_resource(HealthOrbitSection section) {
  switch (section) {
    case HealthOrbitSectionSteps:
      return RESOURCE_ID_ACTIVITY_TINY;
    case HealthOrbitSectionSleep:
      return RESOURCE_ID_MOON_TINY;
    case HealthOrbitSectionWorkout:
    case HealthOrbitSectionToday:
    case HealthOrbitSectionCount:
      break;
  }
  return RESOURCE_ID_INVALID;
}

//! The section's icon, as big as \a size: its lines in the section's color on a selected bubble,
//! all of it in that color on a neighbour's
//! A resource's image in RAM, to recolor and scale: a system resource may be read in place from
//! flash, which cannot be written
static GDrawCommandImage *prv_create_editable_image(uint32_t resource) {
  GDrawCommandImage *mapped = gdraw_command_image_create_with_resource(resource);
  if (!mapped) {
    return nullptr;
  }
  GDrawCommandImage *image = gdraw_command_image_clone(mapped);
  gdraw_command_image_destroy(mapped);
  return image;
}

//! Scales \a icon to fit a square \a size big, keeping its shape
static void prv_scale_to_fit(GDrawCommandImage *icon, int16_t size) {
  const GSize bounds = gdraw_command_image_get_bounds_size(icon);
  if (bounds.w <= 0 || bounds.h <= 0) {
    return;
  }
  const int16_t longest = PBL_MAX(bounds.w, bounds.h);
  gdraw_command_image_scale(icon, GSize((bounds.w * size + longest / 2) / longest,
                                        (bounds.h * size + longest / 2) / longest));
}

static GDrawCommandImage *prv_create_icon(HealthOrbitSection section, int16_t size, bool selected) {
  const uint32_t resource = prv_icon_resource(section);
  if (resource == RESOURCE_ID_INVALID) {
    return nullptr;
  }
  GDrawCommandImage *icon = prv_create_editable_image(resource);
  if (!icon) {
    return nullptr;
  }
  GDrawCommandList *list = gdraw_command_image_get_command_list(icon);
  const GColor color = health_orbit_section_color(section);
  gdraw_command_list_replace_color(list, GColorBlack, color);
  if (!selected) {
    gdraw_command_list_replace_color(list, GColorWhite, color);
  }
  gdraw_command_image_scale(icon, GSize(size, size));
  return icon;
}

//! A kind of workout's icon \a width wide, its lines in \a color, the rest clear
static GDrawCommandImage *prv_create_kind_icon(ActivitySessionType type, int16_t width,
                                               GColor color) {
  const uint32_t resource = (type == ActivitySessionType_Run)    ? RESOURCE_ID_WORKOUT_APP_RUN
                            : (type == ActivitySessionType_Walk) ? RESOURCE_ID_WORKOUT_APP_WALK
                                                                 : RESOURCE_ID_WORKOUT_APP_WORKOUT;
  GDrawCommandImage *icon = prv_create_editable_image(resource);
  if (!icon) {
    return nullptr;
  }
  GDrawCommandList *list = gdraw_command_image_get_command_list(icon);
  gdraw_command_list_replace_color(list, GColorWhite, GColorClear);
  gdraw_command_list_replace_color(list, GColorBlack, color);
  prv_scale_to_fit(icon, width);
  return icon;
}

//! The section's color sweeping in over the sidebar's as the orbit turns: a line from the sidebar's
//! center turns across it from the side the bubbles come in from, the new color behind it
typedef struct SidebarSweep {
  GColor from;
  GColor to;
  //! Angle of the line from the sidebar's middle row, down positive
  int32_t angle;
  //! Whether the new color is above the line, as the bubbles come in from above
  bool from_above;
} SidebarSweep;

//! Fills columns \a x0 to \a x1, in eighths of a pixel, of row \a y in the colors of the sweep's
//! two sides of the line, the pixel it crosses in a blend of them
static void prv_fill_swept_span(GContext *ctx, int16_t y, int16_t x0, int16_t x1, int32_t edge8,
                                GColor left, GColor right) {
  const int16_t edge = PBL_CLAMP(edge8 >> 3, x0, x1);
  graphics_context_set_fill_color(ctx, left);
  graphics_fill_rect(ctx, &GRect(x0, y, edge - x0, 1));
  graphics_context_set_fill_color(ctx, right);
  graphics_fill_rect(ctx, &GRect(edge, y, x1 - edge, 1));
  if ((edge8 >> 3) >= x0 && (edge8 >> 3) < x1) {
    // How much of the pixel is right of the line, in quarters, as antialiasing blends
    const uint8_t right_share = ((8 - (edge8 & 7)) * 3 + 4) / 8;
    GColor blend = right;
    blend.a = right_share;
    graphics_context_set_fill_color(ctx, gcolor_alpha_blend(blend, left));
    graphics_fill_rect(ctx, &GRect(edge, y, 1, 1));
  }
}

//! Fills row \a y of the sidebar, from column \a x0 to the display's edge, as the sweep has it
static void prv_fill_swept_row(GContext *ctx, GSize display, int16_t y, int16_t x0,
                               const SidebarSweep *sweep) {
  const GPoint center = health_orbit_center(display, 0);
  const int32_t dy = y - center.y;
  const int32_t angle = sweep->angle;
  // The part of the row above the line, all, none, or one side of where the line crosses it
  bool all_above;
  bool none_above;
  if (dy == 0) {
    all_above = (angle > 0);
    none_above = !all_above;
  } else {
    all_above = (dy < 0 && angle >= 0);
    none_above = (dy > 0 && angle <= 0);
  }
  const GColor above = sweep->from_above ? sweep->to : sweep->from;
  const GColor below = sweep->from_above ? sweep->from : sweep->to;
  if (all_above || none_above) {
    graphics_context_set_fill_color(ctx, all_above ? above : below);
    graphics_fill_rect(ctx, &GRect(x0, y, display.w - x0, 1));
    return;
  }
  // Where the line crosses the row, in eighths of a pixel, as far left as it can usefully be
  const int32_t abs_angle = PBL_ABS(angle);
  const int32_t sin = sin_lookup(abs_angle);
  const int64_t run8 = ((int64_t)PBL_ABS(dy) * 8 * cos_lookup(abs_angle)) / PBL_MAX(sin, 1);
  const int32_t edge8 = 8 * center.x + 4 - (int32_t)PBL_MIN(run8, 8 * 2 * display.w);
  if (dy < 0) {
    prv_fill_swept_span(ctx, y, x0, display.w, edge8, below, above);
  } else {
    prv_fill_swept_span(ctx, y, x0, display.w, edge8, above, below);
  }
}

//! Fills the sidebar's circle turned by \a turn as the sweep has it: the new color in the half of
//! it on the new color's side of the line, turned with it, the old in the other, both halves filled
//! alike for their edges to meet the circle's
static void prv_fill_swept_circle(GContext *ctx, GSize display, int32_t turn,
                                  const SidebarSweep *sweep) {
  const GPoint center = health_orbit_center(display, turn);
  if (gcolor_equal(sweep->from, sweep->to)) {
    graphics_context_set_fill_color(ctx, sweep->to);
    graphics_fill_circle(ctx, center, HEALTH_ORBIT_RADIUS);
    return;
  }
  // Clockwise from the top: the line points left at no angle, and is above it as it turns up
  const int32_t half = TRIG_MAX_ANGLE / 2;
  const int32_t line =
      positive_modulo(3 * TRIG_MAX_ANGLE / 4 - sweep->angle + turn, TRIG_MAX_ANGLE);
  const int32_t start = sweep->from_above ? line : (line + half);
  graphics_context_set_fill_color(ctx, sweep->to);
  graphics_fill_radial_internal(ctx, center, 0, HEALTH_ORBIT_RADIUS, start, start + half);
  graphics_context_set_fill_color(ctx, sweep->from);
  graphics_fill_radial_internal(ctx, center, 0, HEALTH_ORBIT_RADIUS, start + half,
                                start + TRIG_MAX_ANGLE);
}

//! Fills the sidebar turned by \a turn. At rest on the right and at the top it is filled as the
//! timeline fills it, a row or a column at a time, notched on the right, where the section's color
//! can sweep in; on the way, its circle, the color sweeping in over it as well, with the notch,
//! closing as it turns up, cut out in the background color.
static void prv_fill_sidebar(GContext *ctx, GSize display, int32_t turn, const SidebarSweep *sweep,
                             GColor bg) {
  graphics_context_set_fill_color(ctx, sweep->to);
  const int16_t notch_width = health_orbit_notch_width(turn);
  const bool swept = !gcolor_equal(sweep->from, sweep->to);
  if (turn == 0 || (turn == -QUARTER_TURN && !swept)) {
    const TimelineListSidebar sidebar = health_orbit_sidebar(display, notch_width);
    for (int16_t i = 0; i < display.h; i++) {
      const int16_t depth = timeline_list_sidebar_depth(&sidebar, i);
      if (depth <= 0) {
        continue;
      }
      if (swept) {
        prv_fill_swept_row(ctx, display, i, display.w - depth, sweep);
        continue;
      }
      const GRect span =
          (turn == 0) ? GRect(display.w - depth, i, depth, 1) : GRect(i, 0, 1, depth);
      graphics_fill_rect(ctx, &span);
    }
    return;
  }
  prv_fill_swept_circle(ctx, display, turn, sweep);
  if (notch_width <= 0) {
    return;
  }
  // The notch as it is at rest, its sides from where they meet the circle's edge to its tip, and
  // out past where the edge bulges between them
  const TimelineListSidebar plain = health_orbit_sidebar(display, 0);
  const int16_t half = HEALTH_ORBIT_NOTCH_HEIGHT / 2;
  const int16_t middle = display.h / 2;
  const int16_t base_x = display.w - timeline_list_sidebar_depth(&plain, middle + half);
  const int16_t out_x = display.w - timeline_list_sidebar_depth(&plain, middle) - 2;
  GPoint points[] = {
    health_orbit_turn(display, GPoint(out_x, middle - half), turn),
    health_orbit_turn(display, GPoint(base_x, middle - half), turn),
    health_orbit_notch_tip(display, turn),
    health_orbit_turn(display, GPoint(base_x, middle + half), turn),
    health_orbit_turn(display, GPoint(out_x, middle + half), turn),
  };
  GPath notch = {.num_points = PBL_ARRAY_SIZE(points), .points = points};
  graphics_context_set_fill_color(ctx, bg);
  graphics_context_set_antialiased(ctx, true);
  gpath_draw_filled(ctx, &notch);
  graphics_context_set_antialiased(ctx, false);
}

//! The Today icon: its two rings, small
static void prv_draw_today_icon(GContext *ctx, GPoint center, int16_t radius, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  const int16_t outer = radius / 2;
  const int16_t thickness = 3;
  graphics_fill_radial_capped_internal(ctx, center, outer - thickness, outer, 0,
                                       TRIG_MAX_ANGLE * 78 / 100);
  const int16_t inner = outer - thickness - 2;
  if (inner > thickness) {
    graphics_fill_radial_capped_internal(ctx, center, inner - thickness, inner, 0,
                                         TRIG_MAX_ANGLE * 86 / 100);
  }
}

//! The Today icon on a neighbour's bubble \a radius big: two open rings on the bubble's center,
//! as far apart as they are thick, open alike
static void prv_draw_small_today_icon(GContext *ctx, GPoint center, int16_t radius, GColor color) {
  const int16_t thickness = PBL_MAX(radius / 6, 1);
  const int16_t outer = (radius * 5) / 6;
  const int16_t inner = outer - 2 * thickness;
  const int32_t sweep = (TRIG_MAX_ANGLE * 3) / 4;
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_radial_capped_internal(ctx, center, outer - thickness, outer, 0, sweep);
  if (inner > thickness) {
    graphics_fill_radial_capped_internal(ctx, center, inner - thickness, inner, 0, sweep);
  }
}

//! The Sleep icon on a neighbour's bubble \a radius big: a crescent moon, a disc with a disc of
//! the bubble's color taken out of its top right
static void prv_draw_small_moon_icon(GContext *ctx, GPoint center, int16_t radius, GColor color,
                                     GColor bubble) {
  const int16_t moon = (radius * 7) / 12;
  const int16_t shift = (radius + 6) / 12;
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(center.x - shift, center.y + shift), moon);
  graphics_context_set_fill_color(ctx, bubble);
  graphics_fill_circle(ctx, GPoint(center.x + 2 * shift, center.y - 2 * shift), moon - shift);
}

//! The Workout icon on a bubble \a radius big: a stopwatch, a ring with its button on top and its
//! hand. The Workout app's own icon is the Steps shoe, and its run shoe is a blur this small.
static void prv_draw_stopwatch_icon(GContext *ctx, GPoint center, int16_t radius, GColor color) {
  const int16_t thickness = PBL_MAX(radius / 6, 2);
  const int16_t outer = (radius * 7) / 12;
  // A little below the middle, to make room for the button
  const GPoint watch = GPoint(center.x, center.y + thickness / 2);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_radial_internal(ctx, watch, outer - thickness, outer, 0, TRIG_MAX_ANGLE);
  const int16_t button_w = thickness + 2;
  graphics_fill_rect(
      ctx, &GRect(watch.x - button_w / 2, watch.y - outer - thickness - 1, button_w, thickness));
  const int16_t hand = outer - thickness - 1;
  const int32_t angle = TRIG_MAX_ANGLE / 8;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, thickness);
  graphics_draw_line(ctx, watch,
                     GPoint(watch.x + (hand * sin_lookup(angle)) / TRIG_MAX_RATIO,
                            watch.y - (hand * cos_lookup(angle)) / TRIG_MAX_RATIO));
  graphics_context_set_stroke_width(ctx, 1);
}

static void prv_draw_bubble(GContext *ctx, HealthOrbitView *view, HealthOrbitSection section,
                            GPoint center, int16_t radius, bool selected) {
  if (radius <= 0) {
    return;
  }
  graphics_context_set_fill_color(ctx, selected ? GColorWhite : GColorBlack);
  graphics_fill_circle(ctx, center, radius);
  const GColor color = health_orbit_section_color(section);
  if (section == HealthOrbitSectionToday) {
    if (selected) {
      prv_draw_today_icon(ctx, center, radius * 2 - radius / 10, color);
    } else {
      prv_draw_small_today_icon(ctx, center, radius, color);
    }
    return;
  }
  if (section == HealthOrbitSectionSleep && !selected) {
    prv_draw_small_moon_icon(ctx, center, radius, color, GColorBlack);
    return;
  }
  if (section == HealthOrbitSectionWorkout) {
    prv_draw_stopwatch_icon(ctx, center, radius, color);
    return;
  }
  GDrawCommandImage *icon =
      selected ? view->icons[section].selected : view->icons[section].neighbour;
  if (!icon) {
    return;
  }
  // A shrinking bubble shrinks its icon's view with it
  const GDrawState state =
      health_orbit_clip(ctx, GRect(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
  const GSize size = gdraw_command_image_get_bounds_size(icon);
  gdraw_command_image_draw(ctx, icon, GPoint(center.x - size.w / 2, center.y - size.h / 2));
  graphics_context_set_drawing_state(ctx, state);
}

//! A day's bubble: its weekday in two letters, or a dot for today
static void prv_draw_day_bubble(GContext *ctx, HealthOrbitView *view, int day, GPoint center,
                                int16_t radius, bool selected) {
  graphics_context_set_fill_color(ctx, selected ? GColorWhite : GColorBlack);
  graphics_fill_circle(ctx, center, radius);
  const GColor color = health_orbit_section_color(HealthOrbitSectionToday);
  if (day == 0) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, center, radius / 3);
    return;
  }
  struct pbl_tm local_tm;
  health_orbit_day_time(day, &local_tm);
  // A shrinking bubble shrinks its letters' view with it
  const GDrawState state =
      health_orbit_clip(ctx, GRect(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
  health_orbit_draw_text(
      ctx, health_orbit_short_weekday(local_tm.tm_wday, view),
      selected ? &health_orbit_font_gothic_18_bold : &health_orbit_font_gothic_14_bold, color,
      center.x, center.y);
  graphics_context_set_drawing_state(ctx, state);
}

//! How close a bubble \a offset along the orbit from the notch is to it, from none a step away to
//! HEALTH_ORBIT_STEP on it
static int16_t prv_closeness(int16_t offset) {
  return PBL_MAX(HEALTH_ORBIT_STEP - PBL_ABS(offset), 0);
}

//! Radius of a bubble as close to the notch as \a closeness, from \a neighbour_radius to the
//! selected one's
static int16_t prv_bubble_radius(int16_t closeness, int16_t neighbour_radius) {
  return neighbour_radius +
         (HEALTH_ORBIT_SELECTED_RADIUS - neighbour_radius) * closeness / HEALTH_ORBIT_STEP;
}

//! The bubbles on the orbit, each growing as it nears the notch: the sections', and the days'
//! HEALTH_ORBIT_SWAP along from them. As the orbit swaps one set for the other, both turn as they
//! are, the swap not taking either nearer the notch.
static void prv_draw_orbit(GContext *ctx, HealthOrbitView *view, GSize display) {
  graphics_context_set_antialiased(ctx, true);
  for (int i = 0; view->swap < HEALTH_ORBIT_SWAP && i < HealthOrbitSectionCount; i++) {
    const int16_t offset = health_orbit_wrap_offset(i * HEALTH_ORBIT_STEP - view->orbit_offset);
    const int16_t closeness = prv_closeness(offset);
    const GPoint center = health_orbit_bubble_center(
        display, health_orbit_wrap_offset(offset - view->swap), view->turn);
    prv_draw_bubble(ctx, view, i, center, prv_bubble_radius(closeness, view->neighbour_radius),
                    2 * closeness > HEALTH_ORBIT_STEP);
  }
  for (int day = 0; view->swap > 0 && day < HEALTH_DATA_NUM_DAYS; day++) {
    const int16_t offset = day * HEALTH_ORBIT_STEP - view->day_offset;
    if (PBL_ABS(offset) > HEALTH_ORBIT_DAY_REACH) {
      continue;
    }
    const int16_t closeness = prv_closeness(offset);
    const GPoint center = health_orbit_bubble_center(
        display, health_orbit_wrap_offset(offset + HEALTH_ORBIT_SWAP - view->swap), view->turn);
    prv_draw_day_bubble(ctx, view, day, center,
                        prv_bubble_radius(closeness, NEIGHBOUR_BUBBLE_RADIUS),
                        2 * closeness > HEALTH_ORBIT_STEP);
  }
  graphics_context_set_antialiased(ctx, false);
}

//! Distance from where a week row leaves the notch to where it rests
//! Distance between the rows of the list shown, as the selection moves between them
static int16_t prv_list_pitch(HealthOrbitView *view) {
  return (view->list == HealthOrbitListWorkoutKinds) ? HEALTH_WORKOUT_KIND_ROW_PITCH
                                                     : HEALTH_WEEK_ROW_PITCH;
}

static int32_t prv_row_path_length(HealthOrbitView *view, int index) {
  MenuRoundShape rest;
  health_orbit_list_row(view->layer.bounds.size, view->list, index, &rest);
  const int32_t dx = rest.box.origin.x + rest.box.size.w / 2 - view->row_start[index].x;
  const int32_t dy = rest.box.origin.y + rest.box.size.h / 2 - view->row_start[index].y;
  return integer_sqrt(dx * dx + dy * dy);
}

//! The pill each row of the week is in: a ball leaving the notch, stretching into its pill on the
//! way to its row
static void prv_get_week_rows(HealthOrbitView *view,
                              MenuRoundShape rows_out[HEALTH_ORBIT_LIST_MAX_ROWS]) {
  const GSize display = view->layer.bounds.size;
  for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
    health_orbit_list_row(display, view->list, i, &rows_out[i]);
  }
  for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
    const int16_t travel = view->row_travel[i];
    if (travel == ROW_AT_REST) {
      continue;
    }
    if (travel <= 0) {
      rows_out[i] = (MenuRoundShape){};
      continue;
    }
    const GPoint start = view->row_start[i];
    const int16_t h = rows_out[i].box.size.h;
    const MenuRoundShape ball = {GRect(start.x - h / 2, start.y - h / 2, h, h), h / 2};
    const int32_t length = prv_row_path_length(view, i);
    rows_out[i] = menu_round_morph(&ball, &rows_out[i], (travel * 1000) / PBL_MAX(length, 1));
  }
}

//! Draws the cards, the leaving one's rings with their tracks until the colors swap
static void prv_draw_cards(GContext *ctx, HealthOrbitView *view) {
  const bool leaving_tracks = view->leaving_shown && !view->colors_swapped;
  if (view->leaving_shown) {
    health_orbit_card_draw_content(ctx, &view->leaving, view->health_data, view);
  }
  health_orbit_card_draw_content(ctx, &view->card, view->health_data, view);
  if (view->leaving_shown) {
    health_orbit_card_draw_rings(ctx, &view->leaving, view->health_data, leaving_tracks);
  }
  health_orbit_card_draw_rings(ctx, &view->card, view->health_data, !leaving_tracks);
}

//! The sidebar's colors as the orbit turns: the section's sweeping in from the side its bubble
//! comes in from as the orbit turns, all of it by the time the bubble is at the notch; the days
//! have Today's
static SidebarSweep prv_get_sidebar_sweep(HealthOrbitView *view, GSize display) {
  const int32_t travel = PBL_ABS(view->orbit_target - view->orbit_from);
  const int32_t at = *view->orbit_motion;
  const int32_t remaining =
      view->orbit_down ? (view->orbit_target - at) : (at - view->orbit_target);
  const int32_t progress = (travel > 0)
                               ? PBL_CLAMP((travel - remaining) * ANIMATION_NORMALIZED_MAX / travel,
                                           0, ANIMATION_NORMALIZED_MAX)
                               : ANIMATION_NORMALIZED_MAX;
  // Once it has swept all of the sidebar, the sidebar is in the section's color
  const GColor to = health_orbit_section_color(prv_color_section(view));
  if (progress == ANIMATION_NORMALIZED_MAX) {
    return (SidebarSweep){.from = to, .to = to};
  }
  // The line starts and ends a little past where the sidebar leaves the display
  const int32_t reach = health_orbit_sidebar_reach(display) + TRIG_MAX_ANGLE / 64;
  const int32_t angle = -reach + (2 * reach * progress) / ANIMATION_NORMALIZED_MAX;
  return (SidebarSweep){
    .from = health_orbit_section_color(view->sidebar_from),
    .to = to,
    .angle = view->orbit_down ? -angle : angle,
    .from_above = !view->orbit_down,
  };
}

static void prv_layer_update_proc(Layer *layer, GContext *ctx) {
  HealthOrbitView *view = window_get_user_data(layer_get_window(layer));
  const GSize display = layer->bounds.size;
  const GColor bg = health_orbit_section_bg_color(view->color_section);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, &layer->bounds);

  if (view->card_shown) {
    prv_draw_cards(ctx, view);
  }
  if (view->week_shown) {
    MenuRoundShape rows[HEALTH_ORBIT_LIST_MAX_ROWS];
    prv_get_week_rows(view, rows);
    // The selection's outline goes round its row as it comes and goes, and moves between rows
    const int16_t distance = (view->week_selection - view->selection_from) * prv_list_pitch(view);
    const int32_t progress = distance ? ((int32_t)view->selection_travel * 1000 / distance) : 1000;
    const MenuRoundShape highlight =
        menu_round_morph(&rows[view->selection_from], &rows[view->week_selection], progress);
    switch (view->list) {
      case HealthOrbitListWeek:
        health_orbit_week_draw(ctx, view->section, view->health_data, rows,
                               view->week_header_offset_y, &highlight, view->crown, view);
        break;
      case HealthOrbitListWorkoutKinds:
        health_orbit_workout_draw_kinds(ctx, &view->workout, rows, view->week_header_offset_y,
                                        &highlight, view->kind_icons, view);
        break;
      case HealthOrbitListWorkout:
        health_orbit_workout_draw(ctx, &view->workout, rows, view->week_header_offset_y, view);
        break;
    }
  }
  const SidebarSweep sweep = prv_get_sidebar_sweep(view, display);
  prv_fill_sidebar(ctx, display, view->turn, &sweep, bg);
  prv_draw_orbit(ctx, view, display);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Animations

//! Delays and lengths in frames, as the Moook curves count them
#define FRAMES(n) ((n) * ANIMATION_TARGET_FRAME_INTERVAL_MS)

//! The orbit turns and the cards slide as the timeline scrolls
#define SLIDE_MS TIMELINE_UP_DOWN_ANIMATION_DURATION_MS

//! The background's color changes as the slide bounces back, as the rect cards do
#define COLOR_SWAP_MS (SLIDE_MS - interpolate_moook_out_duration())

//! From the week, the orbit swaps the section's bubble for the days' as the banner nears the right
#define DAYS_FROM_WEEK_DELAY FRAMES(5)

//! A ring sweeps in with a Moook curve in degrees, overshooting by 12
static const int32_t s_sweep_in[] = {0, PBL_DEG_TO_TRIGANGLE(4), PBL_DEG_TO_TRIGANGLE(30)};
static const int32_t s_sweep_out[] = {
  PBL_DEG_TO_TRIGANGLE(12), PBL_DEG_TO_TRIGANGLE(6), PBL_DEG_TO_TRIGANGLE(2), 0
};
static const MoookConfig s_sweep = {
  .frames_in = s_sweep_in,
  .num_frames_in = PBL_ARRAY_SIZE(s_sweep_in),
  .frames_out = s_sweep_out,
  .num_frames_out = PBL_ARRAY_SIZE(s_sweep_out),
  .num_frames_mid = TIMELINE_NUM_MOOOK_FRAMES_MID,
};

//! A ring snaps back from the same anticipation, straight to its start
static const int32_t s_retract_out[] = {0};
static const MoookConfig s_retract = {
  .frames_in = s_sweep_in,
  .num_frames_in = PBL_ARRAY_SIZE(s_sweep_in),
  .frames_out = s_retract_out,
  .num_frames_out = PBL_ARRAY_SIZE(s_retract_out),
  .num_frames_mid = 1,
};

//! Coming back from the week, the rings sweep in with one middle frame
static const MoookConfig s_sweep_back = {
  .frames_in = s_sweep_in,
  .num_frames_in = PBL_ARRAY_SIZE(s_sweep_in),
  .frames_out = s_sweep_out,
  .num_frames_out = PBL_ARRAY_SIZE(s_sweep_out),
  .num_frames_mid = 1,
};

//! The sidebar turns to the top and back on the Moook soft curve in degrees, overshooting by 4
static const int32_t s_turn_in[] = {0, PBL_DEG_TO_TRIGANGLE(1), PBL_DEG_TO_TRIGANGLE(20)};
static const int32_t s_turn_out[] = {
  PBL_DEG_TO_TRIGANGLE(4), PBL_DEG_TO_TRIGANGLE(2), PBL_DEG_TO_TRIGANGLE(1), 0
};
static const MoookConfig s_turn = {
  .frames_in = s_turn_in,
  .num_frames_in = PBL_ARRAY_SIZE(s_turn_in),
  .frames_out = s_turn_out,
  .num_frames_out = PBL_ARRAY_SIZE(s_turn_out),
  .num_frames_mid = TIMELINE_NUM_MOOOK_FRAMES_MID,
};
static const MoookConfig s_turn_back = {
  .frames_in = s_turn_in,
  .num_frames_in = PBL_ARRAY_SIZE(s_turn_in),
  .frames_out = s_turn_out,
  .num_frames_out = PBL_ARRAY_SIZE(s_turn_out),
  .num_frames_mid = 1,
};

static int64_t prv_sweep_back_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_sweep_back);
}

static int64_t prv_turn_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_turn);
}

static int64_t prv_turn_back_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_turn_back);
}

static int64_t prv_in_only_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_in_only(normalized, from, to);
}

static int64_t prv_sweep_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_sweep);
}

static int64_t prv_retract_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_retract);
}

//! A bubble shrinks away easing in, all the way: Moook's anticipation would take it past nothing
//! in one frame
static int64_t prv_shrink_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_int64_linear(animation_timing_curve(normalized, AnimationCurveEaseIn), from,
                                  to);
}

static int64_t prv_moook_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook(normalized, from, to);
}

static void prv_set_int16(void *subject, int16_t value) {
  *(int16_t *)subject = value;
}

static int16_t prv_get_int16(void *subject) {
  return *(int16_t *)subject;
}

static void prv_set_uint32(void *subject, uint32_t value) {
  *(uint32_t *)subject = value;
}

static uint32_t prv_get_uint32(void *subject) {
  return *(uint32_t *)subject;
}

static const PropertyAnimationImplementation s_int16_implementation = {
  .base.update = (AnimationUpdateImplementation)property_animation_update_int16,
  .accessors = {.setter.int16 = prv_set_int16, .getter.int16 = prv_get_int16},
};

static const PropertyAnimationImplementation s_uint32_implementation = {
  .base.update = (AnimationUpdateImplementation)property_animation_update_uint32,
  .accessors = {.setter.uint32 = prv_set_uint32, .getter.uint32 = prv_get_uint32},
};

//! The animations of a transition, played together
typedef struct Transition {
  Animation *animations[24];
  int count;
  uint32_t duration;
} Transition;

static void prv_add(Transition *transition, Animation *animation, uint32_t delay, uint32_t duration,
                    InterpolateInt64Function curve) {
  // The last place is the frame animation's
  if (!animation || transition->count >= (int)PBL_ARRAY_SIZE(transition->animations) - 1) {
    animation_destroy(animation);
    return;
  }
  animation_set_delay(animation, delay);
  animation_set_duration(animation, duration);
  animation_set_custom_interpolation(animation, curve);
  transition->animations[transition->count++] = animation;
  transition->duration = PBL_MAX(transition->duration, delay + duration);
}

//! Animates \a field from what it is to \a to
static void prv_add_int16(Transition *transition, int16_t *field, int16_t to, uint32_t delay,
                          uint32_t duration, InterpolateInt64Function curve) {
  int16_t from = *field;
  PropertyAnimation *animation =
      property_animation_create(&s_int16_implementation, field, &from, &to);
  prv_add(transition, property_animation_get_animation(animation), delay, duration, curve);
}

static void prv_add_uint32(Transition *transition, uint32_t *field, uint32_t to, uint32_t delay,
                           uint32_t duration, InterpolateInt64Function curve) {
  uint32_t from = *field;
  PropertyAnimation *animation =
      property_animation_create(&s_uint32_implementation, field, &from, &to);
  prv_add(transition, property_animation_get_animation(animation), delay, duration, curve);
}

static void prv_schedule_workout_tick(HealthOrbitView *view);

//! Whether the bubbles of the buttons show: on the workout going on, after its countdown
static bool prv_buttons_shown(const HealthOrbitView *view) {
  return view->week && (view->list == HealthOrbitListWorkout) && (view->workout.countdown <= 0) &&
         (view->workout.status != HealthOrbitWorkoutStatusComplete);
}

//! Sets everything where it rests for the section, and the week or its card, or the day
static void prv_rest(HealthOrbitView *view) {
  view->animation = nullptr;
  view->leaving_shown = false;
  view->color_section = prv_color_section(view);
  view->sidebar_from = view->color_section;
  view->color_swap_ms = COLOR_SWAP_MS;
  view->colors_swapped = true;
  if (view->days) {
    prv_init_day_card(view, &view->card);
  } else {
    prv_init_section_card(view, &view->card, view->section);
  }
  view->orbit_offset = view->section * HEALTH_ORBIT_STEP;
  view->day_offset = view->day * HEALTH_ORBIT_STEP;
  view->swap = view->days ? HEALTH_ORBIT_SWAP : 0;
  view->orbit_motion = &view->orbit_offset;
  view->orbit_from = view->orbit_offset;
  view->orbit_target = view->orbit_offset;
  view->turn = view->week ? -QUARTER_TURN : 0;
  // The week, and the days opened from it, keep the section's bubble alone
  view->neighbour_radius = (view->week || prv_days_from_week(view)) ? 0 : NEIGHBOUR_BUBBLE_RADIUS;
  view->card_shown = !view->week;
  view->week_shown = view->week;
  // Counting down to a workout, its rows are yet to come out of the notch
  const bool rows_hidden = (view->workout.countdown > 0);
  for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
    view->row_travel[i] = rows_hidden ? 0 : ROW_AT_REST;
  }
  view->week_header_offset_y = 0;
  view->selection_from = view->week_selection;
  view->selection_travel = 0;
  view->workout.status_from = view->workout.status;
  view->workout.status_roll = 0;
  view->workout.countdown_from = view->workout.countdown;
  view->workout.time_roll = 0;
  view->workout.button_radius = prv_buttons_shown(view) ? HEALTH_WORKOUT_BUTTON_RADIUS : 0;
  prv_schedule_workout_tick(view);
  layer_mark_dirty(&view->layer);
}

//! Redraws every frame, and changes the colors at their time
static void prv_frame_update(Animation *animation, AnimationProgress progress) {
  HealthOrbitView *view = animation_get_context(animation);
  const uint32_t elapsed = (int64_t)progress * animation_get_duration(animation, false, false) /
                           ANIMATION_NORMALIZED_MAX;
  if (elapsed >= view->color_swap_ms) {
    view->color_section = prv_color_section(view);
    view->colors_swapped = true;
  }
  layer_mark_dirty(&view->layer);
}

static const AnimationImplementation s_frame_implementation = {
  .update = prv_frame_update,
};

static void prv_transition_stopped(Animation *animation, bool finished, void *context) {
  prv_rest(context);
}

static void prv_play(HealthOrbitView *view, Transition *transition) {
  view->colors_swapped = false;
  Animation *frame = animation_create();
  animation_set_implementation(frame, &s_frame_implementation);
  animation_set_handlers(frame, (AnimationHandlers){}, view);
  animation_set_duration(frame, transition->duration);
  transition->animations[transition->count++] = frame;
  view->animation = animation_spawn_create_from_array(transition->animations, transition->count);
  animation_set_handlers(view->animation, (AnimationHandlers){.stopped = prv_transition_stopped},
                         view);
  animation_schedule(view->animation);
}

//! Ends a transition at once, where it was going
static void prv_finish(HealthOrbitView *view) {
  if (view->animation) {
    animation_unschedule(view->animation);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Transitions

//! The card slides out the way the button goes as \a card slides in: the old rings snap back, the
//! new ones sweep in and its bubbles pop one after another
static void prv_add_card_slide(HealthOrbitView *view, Transition *transition,
                               const HealthOrbitCard *card, bool down) {
  const int16_t height = view->layer.bounds.size.h;
  view->leaving = view->card;
  view->leaving_shown = true;
  uint32_t sweeps[HEALTH_ORBIT_CARD_MAX_RINGS];
  uint32_t spans[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings =
      health_orbit_card_get_sweeps(&view->leaving, view->health_data, sweeps, spans);
  for (int i = 0; i < num_rings; i++) {
    view->leaving.sweep[i] = sweeps[i];
    prv_add_uint32(transition, &view->leaving.sweep[i], 0, 0,
                   interpolate_moook_custom_duration(&s_retract), prv_retract_curve);
  }
  prv_add_int16(transition, &view->leaving.offset_y, down ? -height : height, 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);

  view->card = *card;
  view->card.offset_y = down ? height : -height;
  prv_add_int16(transition, &view->card.offset_y, 0, 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
  const int num_new_rings =
      health_orbit_card_get_sweeps(&view->card, view->health_data, sweeps, nullptr);
  for (int i = 0; i < num_new_rings; i++) {
    view->card.sweep[i] = 0;
    prv_add_uint32(transition, &view->card.sweep[i], sweeps[i], FRAMES(3),
                   interpolate_moook_custom_duration(&s_sweep), prv_sweep_curve);
  }
  for (int i = 0; i < health_orbit_card_get_num_bubbles(&view->card); i++) {
    view->card.bubble_radius[i] = 0;
    prv_add_int16(transition, &view->card.bubble_radius[i], HEALTH_ORBIT_CARD_BUBBLE_RADIUS,
                  FRAMES(5 + i), interpolate_moook_duration(), prv_moook_curve);
  }
}

//! Turns \a motion, the sections', the days' or the swap between them, to \a target after \a delay
//! as the timeline scrolls, the colors of the section now showing sweeping in over the sidebar with
//! it from the side its bubble comes in from, from \a from_color's
static void prv_add_orbit_turn(HealthOrbitView *view, Transition *transition, int16_t *motion,
                               int16_t target, uint32_t delay, HealthOrbitSection from_color) {
  view->sidebar_from = from_color;
  view->orbit_motion = motion;
  view->orbit_from = *motion;
  view->orbit_target = target;
  view->orbit_down = (target > *motion);
  view->color_swap_ms = delay + COLOR_SWAP_MS;
  prv_add_int16(transition, motion, target, delay, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
}

//! Up or Down: the orbit turns a step, Up counterclockwise and Down clockwise, going on round the
//! way it turns to wrap past the ends, as the section's color sweeps in over the sidebar from the
//! side its bubble comes in from, and the cards slide
static void prv_animate_section(HealthOrbitView *view, HealthOrbitSection section, bool down) {
  prv_finish(view);
  Transition transition = {};
  const HealthOrbitSection from_color = view->section;
  view->section = section;
  if (section == HealthOrbitSectionWorkout) {
    // Its card shows the workout going on from its first frame
    health_data_update_workout(view->health_data);
  }
  HealthOrbitCard card;
  prv_init_section_card(view, &card, section);
  prv_add_card_slide(view, &transition, &card, down);

  // The orbit keeps turning the way the button turns it, on round past the ends to wrap
  int16_t target = section * HEALTH_ORBIT_STEP;
  if (down && target < view->orbit_offset) {
    target += HEALTH_ORBIT_LENGTH;
  } else if (!down && target > view->orbit_offset) {
    target -= HEALTH_ORBIT_LENGTH;
  }
  prv_add_orbit_turn(view, &transition, &view->orbit_offset, target, 0, from_color);
  prv_play(view, &transition);
}

//! Reads the day the days are to show, for the card to have it all before its first frame
static void prv_load_day(HealthOrbitView *view, int day) {
  view->day = day;
  if (day > 0) {
    // Into the other slot, the leaving card's day keeping its own
    view->day_slot = !view->day_slot;
    health_data_day_get(view->health_data, day, &view->day_data[view->day_slot]);
  }
}

//! Up or Down on a day: the orbit of days turns a step, Down clockwise to the day before, Up
//! counterclockwise to the day after, and the cards slide, as between sections
static void prv_animate_day(HealthOrbitView *view, int day, bool down) {
  prv_finish(view);
  Transition transition = {};
  prv_load_day(view, day);
  HealthOrbitCard card;
  prv_init_day_card(view, &card);
  prv_add_card_slide(view, &transition, &card, down);
  prv_add_orbit_turn(view, &transition, &view->day_offset, day * HEALTH_ORBIT_STEP, 0,
                     HealthOrbitSectionToday);
  prv_play(view, &transition);
}

//! Select on the Today card: the orbit turns clockwise, the sections' bubbles going out
//! at the top as the days' come in from the bottom, today's to the notch. The card is today's
//! either way.
static void prv_animate_days_in(HealthOrbitView *view) {
  prv_finish(view);
  Transition transition = {};
  view->days = true;
  prv_load_day(view, 0);
  prv_init_day_card(view, &view->card);
  view->day_offset = 0;
  prv_add_orbit_turn(view, &transition, &view->swap, HEALTH_ORBIT_SWAP, 0, view->color_section);
  prv_play(view, &transition);
}

//! Back on a day opened from the Today card: the orbit turns back counterclockwise, the
//! days' bubbles going out at the bottom as the sections' come back from the top, and the Today
//! card slides back in from above unless the day is today, as for Up
static void prv_animate_days_out_to_today(HealthOrbitView *view) {
  prv_finish(view);
  Transition transition = {};
  view->days = false;
  if (view->day > 0) {
    HealthOrbitCard card;
    prv_init_section_card(view, &card, view->section);
    prv_add_card_slide(view, &transition, &card, false /* down */);
  } else {
    prv_init_section_card(view, &view->card, view->section);
  }
  prv_add_orbit_turn(view, &transition, &view->swap, 0, 0, HealthOrbitSectionToday);
  prv_play(view, &transition);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Navigation

//! How far the line above the week rows is moved up to be off the display, under the banner
#define HEADER_HIDDEN_OFFSET (-(HEALTH_WEEK_ROW_TOP + 2))

//! How far up the line under the banner goes to be off the display: with the workout's time below
//! it, the time off the top as well, as the banner turns away from it
static int16_t prv_header_hidden_offset(HealthOrbitView *view) {
  return (view->list == HealthOrbitListWorkout)
             ? -(HEALTH_WORKOUT_TIME_Y + HEALTH_WORKOUT_TIME_ROLL / 2 + 4)
             : HEADER_HIDDEN_OFFSET;
}

//! On the workout going on, the bubbles of its buttons pop up after \a delay, as a card's do
static void prv_add_buttons_in(HealthOrbitView *view, Transition *transition, uint32_t delay) {
  if (view->list != HealthOrbitListWorkout || view->workout.countdown > 0 ||
      view->workout.status == HealthOrbitWorkoutStatusComplete) {
    return;
  }
  view->workout.button_radius = 0;
  prv_add_int16(transition, &view->workout.button_radius, HEALTH_WORKOUT_BUTTON_RADIUS, delay,
                interpolate_moook_duration(), prv_moook_curve);
}

//! The line under the banner, with the workout's time below it, slides out from under the banner
//! and back as cards slide, through the middle frames, rather than coming or leaving at once
static void prv_add_header_slide(HealthOrbitView *view, Transition *transition, int16_t to,
                                 uint32_t delay) {
  prv_add_int16(transition, &view->week_header_offset_y, to, delay, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
}

//! Each row leaves the notch where the notch is when it starts: the first two four frames into the
//! turn, then two more a frame
static uint32_t prv_row_out_delay(int index) {
  return FRAMES(4 + index / 2);
}

//! The row of the week for a day: its own, or the oldest for a day before the week
static int prv_week_row(int day) {
  return PBL_MIN(day, HEALTH_ORBIT_WEEK_NUM_ROWS - 1);
}

//! Select, or Back from the days opened from the week: the card's rings snap back and its bubbles
//! pop out as it drops away, the sidebar turns a quarter round back to the top into the banner, and
//! the week's rows drop out of its notch, the most recent first, each a ball stretching into its
//! pill, the selected one's outline round it. From the days, the orbit turns back as it
//! turns up, the days' bubbles going out as the section's comes back, alone, its color sweeping in.
static void prv_animate_week_in(HealthOrbitView *view) {
  prv_finish(view);
  if (view->section != HealthOrbitSectionWorkout) {
    view->list = HealthOrbitListWeek;
  }
  const GSize display = view->layer.bounds.size;
  Transition transition = {};
  if (view->days) {
    // The week comes back with the day selected, or the row nearest it
    view->week_selection = prv_week_row(view->day);
    view->days = false;
    prv_add_orbit_turn(view, &transition, &view->swap, 0, 0, HealthOrbitSectionToday);
  } else {
    // From the card, on today
    view->week_selection = 0;
  }
  view->selection_from = view->week_selection;
  view->week = true;
  view->week_shown = true;

  uint32_t sweeps[HEALTH_ORBIT_CARD_MAX_RINGS];
  uint32_t spans[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = health_orbit_card_get_sweeps(&view->card, view->health_data, sweeps, spans);
  for (int i = 0; i < num_rings; i++) {
    view->card.sweep[i] = sweeps[i];
    prv_add_uint32(&transition, &view->card.sweep[i], 0, 0,
                   interpolate_moook_custom_duration(&s_retract), prv_retract_curve);
    // The track snaps back with it
    view->card.track[i] = spans[i];
    prv_add_uint32(&transition, &view->card.track[i], 0, 0,
                   interpolate_moook_custom_duration(&s_retract), prv_retract_curve);
  }
  for (int i = 0; i < health_orbit_card_get_num_bubbles(&view->card); i++) {
    prv_add_int16(&transition, &view->card.bubble_radius[i], 0, 0, interpolate_moook_in_duration(),
                  prv_in_only_curve);
  }
  // The card drops away as cards slide, through the middle frames, rather than leaving at once
  prv_add_int16(&transition, &view->card.offset_y, display.h, 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
  prv_add_int16(&transition, &view->neighbour_radius, 0, 0, interpolate_moook_in_duration(),
                prv_in_only_curve);

  const uint32_t turn_duration = interpolate_moook_custom_duration(&s_turn);
  prv_add_int16(&transition, &view->turn, -QUARTER_TURN, 0, turn_duration, prv_turn_curve);
  for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
    const uint32_t delay = prv_row_out_delay(i);
    const int32_t turn = prv_turn_curve(
        (int64_t)PBL_MIN(delay, turn_duration) * ANIMATION_NORMALIZED_MAX / turn_duration, 0,
        -QUARTER_TURN);
    view->row_start[i] = health_orbit_notch_tip(display, turn);
    view->row_travel[i] = 0;
    prv_add_int16(&transition, &view->row_travel[i], prv_row_path_length(view, i), delay,
                  interpolate_moook_duration(), prv_moook_curve);
  }
  // The line above the rows comes out from under the banner as the first rows land
  view->week_header_offset_y = prv_header_hidden_offset(view);
  prv_add_header_slide(view, &transition, 0, prv_row_out_delay(0) + FRAMES(3));
  prv_add_buttons_in(view, &transition, prv_row_out_delay(0) + FRAMES(5));
  prv_play(view, &transition);
}

//! Back, or Select on a day: the rows fold into balls and slip back into the notch, the oldest
//! first, the banner turns back down into the sidebar, and the section's card, or the overview of
//! the day selected, comes back up as its rings sweep in and its bubbles pop. To the day, the orbit
//! then turns clockwise, the section's bubble going out at the top as the days' come in
//! from the bottom, the day's to the notch, Today's colors sweeping in with them.
static void prv_animate_week_out(HealthOrbitView *view, bool to_day) {
  prv_finish(view);
  const GSize display = view->layer.bounds.size;
  Transition transition = {};
  view->week = false;
  view->card_shown = true;

  const GPoint notch = health_orbit_notch_tip(display, -QUARTER_TURN);
  for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
    view->row_start[i] = notch;
    // From where it is, if it is yet to come out
    if (view->row_travel[i] == ROW_AT_REST) {
      view->row_travel[i] = prv_row_path_length(view, i);
    }
    // Three rows a frame
    const uint32_t delay = FRAMES((HEALTH_ORBIT_LIST_MAX_ROWS - 1 - i) / 3);
    prv_add_int16(&transition, &view->row_travel[i], 0, delay, interpolate_moook_duration(),
                  prv_moook_curve);
  }
  prv_add_header_slide(view, &transition, prv_header_hidden_offset(view), 0);
  // The bubbles of the buttons shrink away
  prv_add_int16(&transition, &view->workout.button_radius, 0, 0, interpolate_moook_duration(),
                prv_shrink_curve);

  const uint32_t turn_delay = FRAMES(3);
  prv_add_int16(&transition, &view->turn, 0, turn_delay,
                interpolate_moook_custom_duration(&s_turn_back), prv_turn_back_curve);

  if (to_day) {
    const HealthOrbitSection from_color = view->color_section;
    view->days = true;
    prv_load_day(view, view->week_selection);
    prv_init_day_card(view, &view->card);
    view->day_offset = view->day * HEALTH_ORBIT_STEP;
    prv_add_orbit_turn(view, &transition, &view->swap, HEALTH_ORBIT_SWAP, DAYS_FROM_WEEK_DELAY,
                       from_color);
  } else {
    prv_init_section_card(view, &view->card, view->section);
  }
  view->card.offset_y = display.h;
  // It rises as cards slide, its text coming up through the middle frames rather than at once
  prv_add_int16(&transition, &view->card.offset_y, 0, turn_delay, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
  uint32_t sweeps[HEALTH_ORBIT_CARD_MAX_RINGS];
  uint32_t spans[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings = health_orbit_card_get_sweeps(&view->card, view->health_data, sweeps, spans);
  for (int i = 0; i < num_rings; i++) {
    view->card.sweep[i] = 0;
    prv_add_uint32(&transition, &view->card.sweep[i], sweeps[i], FRAMES(5),
                   interpolate_moook_custom_duration(&s_sweep_back), prv_sweep_back_curve);
    // The track sweeps in with it
    view->card.track[i] = 0;
    prv_add_uint32(&transition, &view->card.track[i], spans[i], FRAMES(5),
                   interpolate_moook_custom_duration(&s_sweep_back), prv_sweep_back_curve);
  }
  for (int i = 0; i < health_orbit_card_get_num_bubbles(&view->card); i++) {
    view->card.bubble_radius[i] = 0;
    prv_add_int16(&transition, &view->card.bubble_radius[i], HEALTH_ORBIT_CARD_BUBBLE_RADIUS,
                  FRAMES(6 + i), interpolate_moook_duration(), prv_moook_curve);
  }
  if (!to_day) {
    prv_add_int16(&transition, &view->neighbour_radius, NEIGHBOUR_BUBBLE_RADIUS, FRAMES(6),
                  interpolate_moook_duration(), prv_moook_curve);
  }
  prv_play(view, &transition);
}

static void prv_show_section(HealthOrbitView *view, HealthOrbitSection section) {
  prv_finish(view);
  view->section = section;
  if (section == HealthOrbitSectionWorkout) {
    health_data_update_workout(view->health_data);
  }
  prv_rest(view);
}

//! Up or Down in the week: the outline moves to the day above or below as the timeline's selection
//! moves, stopping at the ends, where all the days show
static void prv_move_selection(HealthOrbitView *view, bool down) {
  const int next = view->week_selection + (down ? 1 : -1);
  if (next < 0 || next >= health_orbit_list_num_rows(view->list)) {
    return;
  }
  prv_finish(view);
  Transition transition = {};
  view->selection_from = view->week_selection;
  view->week_selection = next;
  view->selection_travel = 0;
  prv_add_int16(&transition, &view->selection_travel,
                down ? prv_list_pitch(view) : -prv_list_pitch(view), 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
  prv_play(view, &transition);
}

//! Up or Down on a day: Down to the day before, Up to the day after, stopping at today and at the
//! oldest day the watch keeps, as time does not wrap
static void prv_move_day(HealthOrbitView *view, bool down) {
  const int next = view->day + (down ? 1 : -1);
  if (next < 0 || next >= HEALTH_DATA_NUM_DAYS) {
    return;
  }
  prv_animate_day(view, next, down);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Workout

//! Reads the workout going on every second while the Workout section shows
#define WORKOUT_TICK_MS 1000

static void prv_workout_tick(void *context);

static void prv_schedule_workout_tick(HealthOrbitView *view) {
  if (!view->workout_timer && view->section == HealthOrbitSectionWorkout && !view->days) {
    view->workout_timer = app_timer_register(WORKOUT_TICK_MS, prv_workout_tick, view);
  }
}

static void prv_cancel_workout_tick(HealthOrbitView *view) {
  if (view->workout_timer) {
    app_timer_cancel(view->workout_timer);
    view->workout_timer = nullptr;
  }
}

//! The line under the banner rolls up to say \a status, the time with it when its color changes
static void prv_roll_status(HealthOrbitView *view, HealthOrbitWorkoutStatus status) {
  if (view->workout.status == status) {
    return;
  }
  prv_finish(view);
  Transition transition = {};
  view->workout.status_from = view->workout.status;
  view->workout.status = status;
  view->workout.status_roll = HEALTH_WORKOUT_STATUS_ROLL;
  prv_add_int16(&transition, &view->workout.status_roll, 0, 0, interpolate_moook_duration(),
                prv_moook_curve);
  if (status == HealthOrbitWorkoutStatusComplete) {
    // Its buttons are done with: their bubbles shrink away
    prv_add_int16(&transition, &view->workout.button_radius, 0, 0, interpolate_moook_duration(),
                  prv_shrink_curve);
  }
  prv_play(view, &transition);
}

//! What the line under the banner says for the workout going on
static HealthOrbitWorkoutStatus prv_workout_status(const HealthWorkoutData *workout) {
  return (workout->state == HealthWorkoutStatePaused) ? HealthOrbitWorkoutStatusPaused
                                                      : HealthOrbitWorkoutStatusRunning;
}

//! Shows the workout going on as the health data has it
static void prv_show_workout_data(HealthOrbitView *view) {
  const HealthWorkoutData *workout = health_data_workout_get(view->health_data);
  view->workout.workout = *workout;
  view->workout.type = workout->type;
}

//! Every second: the workout going on as it is now, or the next second of the countdown
static void prv_count_down(HealthOrbitView *view);

static void prv_workout_tick(void *context) {
  HealthOrbitView *view = context;
  view->workout_timer = nullptr;
  if (view->section != HealthOrbitSectionWorkout || view->days) {
    return;
  }
  if (view->workout.countdown > 0) {
    prv_count_down(view);
    prv_schedule_workout_tick(view);
    return;
  }
  health_data_update_workout(view->health_data);
  const HealthWorkoutData *workout = health_data_workout_get(view->health_data);
  const HealthOrbitWorkoutStatus status = view->workout.status;
  if (view->week && view->list == HealthOrbitListWorkout &&
      status != HealthOrbitWorkoutStatusComplete) {
    if (workout->state == HealthWorkoutStateRunning || workout->state == HealthWorkoutStatePaused) {
      prv_show_workout_data(view);
      // Paused or resumed elsewhere
      const HealthOrbitWorkoutStatus now = prv_workout_status(workout);
      if (now != status && status != HealthOrbitWorkoutStatusEnd) {
        prv_roll_status(view, now);
      }
    } else {
      // Ended elsewhere: its summary, as it was last
      prv_roll_status(view, HealthOrbitWorkoutStatusComplete);
    }
  }
  layer_mark_dirty(&view->layer);
  prv_schedule_workout_tick(view);
}

//! Starts the workout counted down to, or shows the one another client started meanwhile
static void prv_start_workout(HealthOrbitView *view) {
  workout_service_start_workout(view->workout.type);
  health_data_update_workout(view->health_data);
  if (health_data_workout_get(view->health_data)->state != HealthWorkoutStateNone) {
    prv_show_workout_data(view);
  }
}

//! The next second of the countdown rolls up into place; after the last, the workout starts, its
//! time rolling up in its place as its rows drop out of the notch
static void prv_count_down(HealthOrbitView *view) {
  prv_finish(view);
  const GSize display = view->layer.bounds.size;
  Transition transition = {};
  HealthOrbitWorkoutScreen *screen = &view->workout;
  view->list = HealthOrbitListWorkout;
  screen->countdown_from = screen->countdown;
  screen->countdown--;
  screen->time_roll = HEALTH_WORKOUT_TIME_ROLL;
  prv_add_int16(&transition, &screen->time_roll, 0, 0, interpolate_moook_duration(),
                prv_moook_curve);
  if (screen->countdown == 0) {
    prv_start_workout(view);
    const GPoint notch = health_orbit_notch_tip(display, -QUARTER_TURN);
    for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
      view->row_start[i] = notch;
      view->row_travel[i] = 0;
      prv_add_int16(&transition, &view->row_travel[i], prv_row_path_length(view, i),
                    FRAMES(2 + i / 2), interpolate_moook_duration(), prv_moook_curve);
    }
    prv_add_buttons_in(view, &transition, FRAMES(6));
  }
  prv_play(view, &transition);
}

//! Select on a kind of workout: the rows fold into the notch as the line under the banner rolls up
//! to the kind's name, and the countdown to it rolls up below it
static void prv_animate_workout_countdown(HealthOrbitView *view) {
  prv_finish(view);
  const GSize display = view->layer.bounds.size;
  Transition transition = {};
  HealthOrbitWorkoutScreen *screen = &view->workout;
  screen->type = health_orbit_workout_kinds[view->week_selection];
  screen->workout = (HealthWorkoutData){.type = screen->type};
  const GPoint notch = health_orbit_notch_tip(display, -QUARTER_TURN);
  for (int i = 0; i < HEALTH_ORBIT_LIST_MAX_ROWS; i++) {
    view->row_start[i] = notch;
    view->row_travel[i] = prv_row_path_length(view, i);
    prv_add_int16(&transition, &view->row_travel[i], 0, FRAMES(HEALTH_WORKOUT_NUM_KINDS - 1 - i),
                  interpolate_moook_duration(), prv_moook_curve);
  }
  screen->status_from = screen->status;
  screen->status = HealthOrbitWorkoutStatusRunning;
  screen->status_roll = HEALTH_WORKOUT_STATUS_ROLL;
  prv_add_int16(&transition, &screen->status_roll, 0, 0, interpolate_moook_duration(),
                prv_moook_curve);
  screen->countdown_from = -1;
  screen->countdown = HEALTH_WORKOUT_COUNTDOWN;
  screen->time_roll = HEALTH_WORKOUT_TIME_ROLL;
  prv_add_int16(&transition, &screen->time_roll, 0, FRAMES(2), interpolate_moook_duration(),
                prv_moook_curve);
  prv_play(view, &transition);
  // Each second from now
  prv_cancel_workout_tick(view);
  prv_schedule_workout_tick(view);
}

//! Select on the Workout card: the kinds of workout to start, or the workout going on, turning the
//! one the watch detected into a workout, come out below the banner as the week does
static void prv_open_workout(HealthOrbitView *view) {
  health_data_update_workout(view->health_data);
  const HealthWorkoutData *workout = health_data_workout_get(view->health_data);
  if (workout->state == HealthWorkoutStateDetected) {
    ActivitySession session = workout->session;
    workout_service_takeover_activity_session(&session);
    health_data_update_workout(view->health_data);
  }
  HealthOrbitWorkoutScreen *screen = &view->workout;
  if (workout->state == HealthWorkoutStateNone) {
    view->list = HealthOrbitListWorkoutKinds;
    *screen = (HealthOrbitWorkoutScreen){
      .status = HealthOrbitWorkoutStatusKinds,
      .countdown = -1,
      .countdown_from = -1
    };
  } else {
    view->list = HealthOrbitListWorkout;
    *screen = (HealthOrbitWorkoutScreen){.status = prv_workout_status(workout)};
    prv_show_workout_data(view);
  }
  prv_animate_week_in(view);
}

//! Select on a kind of workout counts down to it; on the workout, pause or resume it, end it once
//! asked, or after its summary, back to the card
static void prv_workout_select(HealthOrbitView *view) {
  if (view->workout.countdown > 0) {
    return;
  }
  if (view->list == HealthOrbitListWorkoutKinds) {
    prv_animate_workout_countdown(view);
    return;
  }
  switch (view->workout.status) {
    case HealthOrbitWorkoutStatusRunning:
    case HealthOrbitWorkoutStatusPaused:
      workout_service_pause_workout(!workout_service_is_paused());
      health_data_update_workout(view->health_data);
      prv_show_workout_data(view);
      prv_roll_status(view, prv_workout_status(health_data_workout_get(view->health_data)));
      return;
    case HealthOrbitWorkoutStatusEnd:
      workout_service_stop_workout();
      health_data_update_workout(view->health_data);
      prv_roll_status(view, HealthOrbitWorkoutStatusComplete);
      return;
    case HealthOrbitWorkoutStatusComplete:
      prv_animate_week_out(view, false /* to_day */);
      return;
    case HealthOrbitWorkoutStatusKinds:
      break;
  }
}

//! Up or Down on the kinds of workout moves the selection; on the workout, Down, by the End bubble,
//! asks whether to end it, paused or not, and Up or Down takes that back
//! Takes back asking whether to end the workout: the line rolls back to how it is going
static void prv_cancel_end(HealthOrbitView *view) {
  health_data_update_workout(view->health_data);
  prv_roll_status(view, prv_workout_status(health_data_workout_get(view->health_data)));
}

static void prv_workout_up_down(HealthOrbitView *view, bool down) {
  if (view->workout.countdown > 0) {
    return;
  }
  if (view->list == HealthOrbitListWorkoutKinds) {
    prv_move_selection(view, down);
    return;
  }
  const HealthOrbitWorkoutStatus status = view->workout.status;
  if (status == HealthOrbitWorkoutStatusEnd) {
    prv_cancel_end(view);
  } else if (down && (status == HealthOrbitWorkoutStatusRunning ||
                      status == HealthOrbitWorkoutStatusPaused)) {
    prv_roll_status(view, HealthOrbitWorkoutStatusEnd);
  }
}

//! Back on the workout: takes back asking to end it, or goes back to the card, the countdown
//! called off
static void prv_workout_back(HealthOrbitView *view) {
  if (view->workout.status == HealthOrbitWorkoutStatusEnd) {
    prv_cancel_end(view);
    return;
  }
  if (view->workout.countdown > 0) {
    prv_finish(view);
    view->workout.countdown = -1;
  }
  prv_animate_week_out(view, false /* to_day */);
}

//! Opened on the Workout section, as the Workout app does: the workout going on if there is one,
//! ended first and its summary shown if asked, or else the Workout card
static void prv_show_workout(HealthOrbitView *view, bool end) {
  prv_finish(view);
  view->section = HealthOrbitSectionWorkout;
  health_data_update_workout(view->health_data);
  const HealthWorkoutData *workout = health_data_workout_get(view->health_data);
  if (workout->state == HealthWorkoutStateRunning || workout->state == HealthWorkoutStatePaused) {
    view->week = true;
    view->list = HealthOrbitListWorkout;
    view->workout = (HealthOrbitWorkoutScreen){.status = prv_workout_status(workout)};
    prv_show_workout_data(view);
    if (end) {
      workout_service_stop_workout();
      health_data_update_workout(view->health_data);
      view->workout.status = HealthOrbitWorkoutStatusComplete;
    }
  }
  prv_rest(view);
}

static void prv_up_down(HealthOrbitView *view, bool down) {
  if (view->week && view->section == HealthOrbitSectionWorkout) {
    prv_workout_up_down(view, down);
    return;
  }
  if (view->week) {
    prv_move_selection(view, down);
    return;
  }
  if (view->days) {
    prv_move_day(view, down);
    return;
  }
  int next = view->section + (down ? 1 : -1);
  if (next < 0 || next >= HealthOrbitSectionCount) {
    if (!view->wrap) {
      if (next < 0) {
        app_window_stack_pop_all(true);
      }
      return;
    }
    next = (next < 0) ? (HealthOrbitSectionCount - 1) : 0;
  }
  prv_animate_section(view, next, down);
}

static void prv_up_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_up_down(context, click_recognizer_get_button_id(recognizer) == BUTTON_ID_DOWN);
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  if (view->week && view->section == HealthOrbitSectionWorkout) {
    prv_workout_select(view);
  } else if (view->week) {
    prv_animate_week_out(view, true /* to_day */);
  } else if (view->days) {
    return;
  } else if (view->section == HealthOrbitSectionWorkout) {
    prv_open_workout(view);
  } else if (view->section == HealthOrbitSectionToday) {
    prv_animate_days_in(view);
  } else if (health_orbit_week_has_section(view->section)) {
    prv_animate_week_in(view);
  }
}

static void prv_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  // Back to where the days were opened from, as it was
  if (view->week && view->section == HealthOrbitSectionWorkout) {
    prv_workout_back(view);
  } else if (prv_days_from_week(view)) {
    prv_animate_week_in(view);
  } else if (view->days) {
    prv_animate_days_out_to_today(view);
  } else if (view->week) {
    prv_animate_week_out(view, false /* to_day */);
  } else {
    app_window_stack_pop(true);
  }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_up_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click_handler);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// API Functions

HealthOrbitView *health_orbit_view_create(HealthData *health_data) {
  // Opened with Up or Down, as a quick launch, the sections stop at either end
  bool directional_quick_launch = false;
  if (app_launch_reason() == APP_LAUNCH_QUICK_LAUNCH) {
    const ButtonId button = app_launch_button();
    const AppQuickLaunchAction action = app_launch_get_quick_launch_action();
    directional_quick_launch =
        (button == BUTTON_ID_UP || button == BUTTON_ID_DOWN) &&
        (action == APP_QUICK_LAUNCH_ACTION_TAP || action == APP_QUICK_LAUNCH_ACTION_HOLD);
  }

  HealthOrbitView *view = app_zalloc_check(sizeof(HealthOrbitView));
  *view = (HealthOrbitView){
    .health_data = health_data,
    .wrap = !directional_quick_launch,
    .neighbour_radius = NEIGHBOUR_BUBBLE_RADIUS,
  };
  for (int i = 0; i < HealthOrbitSectionCount; i++) {
    view->icons[i] = (HealthOrbitIcon){
      .selected = prv_create_icon(i, SELECTED_ICON_SIZE, true),
      .neighbour = prv_create_icon(i, NEIGHBOUR_ICON_SIZE, false),
    };
  }

  for (int i = 0; i < HEALTH_WORKOUT_NUM_KINDS; i++) {
    const ActivitySessionType type = health_orbit_workout_kinds[i];
    view->kind_icons[i] =
        prv_create_kind_icon(type, KIND_ICON_SIZE, health_orbit_workout_ink(type));
  }
  view->run_icon = prv_create_kind_icon(ActivitySessionType_Run, HEALTH_ORBIT_WORKOUT_SHOE_WIDTH,
                                        health_orbit_workout_color(ActivitySessionType_Run));

  view->crown = prv_create_editable_image(RESOURCE_ID_HEALTH_APP_CROWN);
  if (view->crown) {
    gdraw_command_image_scale(view->crown, CROWN_SIZE);
  }

  window_init(&view->window, WINDOW_NAME("Health Orbit"));
  window_set_user_data(&view->window, view);
  window_set_click_config_provider_with_context(&view->window, prv_click_config_provider, view);
  Layer *root = window_get_root_layer(&view->window);
  layer_init(&view->layer, &root->bounds);
  layer_set_update_proc(&view->layer, prv_layer_update_proc);
  layer_add_child(root, &view->layer);

  const HealthLaunchArgs launch_args = {.args = app_launch_get_args()};
  switch (launch_args.card_type) {
    case HealthCardType_Sleep:
      prv_show_section(view, HealthOrbitSectionSleep);
      break;
    case HealthCardType_Workout:
      prv_show_workout(view, launch_args.end_workout);
      break;
    default:
      prv_show_section(view, HealthOrbitSectionToday);
      break;
  }
  return view;
}

void health_orbit_view_destroy(HealthOrbitView *view) {
  prv_finish(view);
  prv_cancel_workout_tick(view);
  for (int i = 0; i < HEALTH_WORKOUT_NUM_KINDS; i++) {
    gdraw_command_image_destroy(view->kind_icons[i]);
  }
  gdraw_command_image_destroy(view->run_icon);
  for (int i = 0; i < HealthOrbitSectionCount; i++) {
    gdraw_command_image_destroy(view->icons[i].selected);
    gdraw_command_image_destroy(view->icons[i].neighbour);
  }
  gdraw_command_image_destroy(view->crown);
  layer_deinit(&view->layer);
  window_deinit(&view->window);
  pbl_i18n_free_all(view);
  app_free(view);
}

void health_orbit_view_push(HealthOrbitView *view) {
  app_window_stack_push(&view->window, true);
}

void health_orbit_view_mark_dirty(HealthOrbitView *view) {
  layer_mark_dirty(&view->layer);
}

#endif
