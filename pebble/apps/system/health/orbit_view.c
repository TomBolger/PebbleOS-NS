/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_view.h"

#include "orbit_card.h"
#include "orbit_draw.h"
#include "orbit_layout.h"
#include "orbit_week.h"

#include <pbl/i18n/i18n.h>
#include <pbl/services/timeline/health_layout.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/trig.h>

#include <applib/app_launch_button.h>
#include <applib/app_launch_reason.h>
#include <applib/app_malloc.h>
#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/gdraw_command_transforms.h>
#include <applib/graphics/gpath.h>
#include <applib/graphics/graphics.h>
#include <applib/ui/animation_interpolate.h>
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
  //! Section of the card shown, or coming in
  HealthOrbitSection section;
  HealthOrbitCard card;
  //! The card going out as another comes in
  HealthOrbitCard leaving;
  bool leaving_shown;
  //! Section whose colors the background and the sidebar have
  HealthOrbitSection color_section;
  Animation *animation;
  //! Whether Up past the first section and Down past the last one wrap round
  bool wrap;

  //! Rows the orbit has turned by, HEALTH_ORBIT_STEP per section from the first
  int16_t orbit_offset;
  //! Angle the sidebar is turned by, back to the top as the week shows
  int16_t turn;
  //! Radius of the bubbles of the sections away from the notch
  int16_t neighbour_radius;

  //! Whether the week of the section shows, in place of its card, or is coming in
  bool week;
  //! Whether the card and the week are drawn, both as one turns into the other
  bool card_shown;
  bool week_shown;
  //! How far each row of the week has come from where it leaves the notch, or ROW_AT_REST
  int16_t row_travel[HEALTH_ORBIT_WEEK_NUM_ROWS];
  GPoint row_start[HEALTH_ORBIT_WEEK_NUM_ROWS];
  //! How far the line above the rows is below where it rests
  int16_t week_header_offset_y;
};

//! A week row where it rests
#define ROW_AT_REST INT16_MAX

////////////////////////////////////////////////////////////////////////////////////////////////////
// The sidebar and the orbit

static uint32_t prv_icon_resource(HealthOrbitSection section) {
  switch (section) {
    case HealthOrbitSectionSteps:
      return RESOURCE_ID_ACTIVITY_TINY;
    case HealthOrbitSectionSleep:
      return RESOURCE_ID_MOON_TINY;
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

//! Fills the sidebar turned by \a turn. At rest on the right and at the top it is filled as the
//! timeline fills it, a row or a column at a time, notched on the right; on the way, its circle with
//! the notch, closing as it turns up, cut out in the background color.
static void prv_fill_sidebar(GContext *ctx, GSize display, int32_t turn, GColor color, GColor bg) {
  graphics_context_set_fill_color(ctx, color);
  const int16_t notch_width = health_orbit_notch_width(turn);
  if (turn == 0 || turn == -QUARTER_TURN) {
    const TimelineListSidebar sidebar = health_orbit_sidebar(display, notch_width);
    for (int16_t i = 0; i < display.h; i++) {
      const int16_t depth = timeline_list_sidebar_depth(&sidebar, i);
      if (depth <= 0) {
        continue;
      }
      const GRect span =
          (turn == 0) ? GRect(display.w - depth, i, depth, 1) : GRect(i, 0, 1, depth);
      graphics_fill_rect(ctx, &span);
    }
    return;
  }
  graphics_fill_circle(ctx, health_orbit_center(display, turn), HEALTH_ORBIT_RADIUS);
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

static void prv_draw_bubble(GContext *ctx, HealthOrbitView *view, HealthOrbitSection section,
                            GPoint center, int16_t radius, bool selected) {
  if (radius <= 0) {
    return;
  }
  graphics_context_set_fill_color(ctx, selected ? GColorWhite : GColorBlack);
  graphics_fill_circle(ctx, center, radius);
  const GColor color = health_orbit_section_color(section);
  if (section == HealthOrbitSectionToday) {
    prv_draw_today_icon(ctx, center, radius * 2 - radius / 10, color);
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

//! The section bubbles on the orbit, each growing as it nears the notch
static void prv_draw_orbit(GContext *ctx, HealthOrbitView *view, GSize display) {
  graphics_context_set_antialiased(ctx, true);
  for (int i = 0; i < HealthOrbitSectionCount; i++) {
    const int16_t offset = i * HEALTH_ORBIT_STEP - view->orbit_offset;
    const int16_t closeness = PBL_MAX(HEALTH_ORBIT_STEP - PBL_ABS(offset), 0);
    const int16_t radius =
        view->neighbour_radius +
        (HEALTH_ORBIT_SELECTED_RADIUS - view->neighbour_radius) * closeness / HEALTH_ORBIT_STEP;
    const GPoint center = health_orbit_bubble_center(display, offset, view->turn);
    prv_draw_bubble(ctx, view, i, center, radius, 2 * closeness > HEALTH_ORBIT_STEP);
  }
  graphics_context_set_antialiased(ctx, false);
}

//! Distance from where a week row leaves the notch to where it rests
static int32_t prv_row_path_length(HealthOrbitView *view, int index) {
  MenuRoundShape rest;
  health_orbit_week_row(view->layer.bounds.size, index, &rest);
  const int32_t dx = rest.box.origin.x + rest.box.size.w / 2 - view->row_start[index].x;
  const int32_t dy = rest.box.origin.y + rest.box.size.h / 2 - view->row_start[index].y;
  return integer_sqrt(dx * dx + dy * dy);
}

//! The pill each row of the week is in: a ball leaving the notch, stretching into its pill on the
//! way to its row
static void prv_get_week_rows(HealthOrbitView *view,
                              MenuRoundShape rows_out[HEALTH_ORBIT_WEEK_NUM_ROWS]) {
  health_orbit_week_get_rest_rows(rows_out);
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
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

//! Draws the cards, the leaving one's rings with their tracks as long as the colors are still its
static void prv_draw_cards(GContext *ctx, HealthOrbitView *view) {
  const bool leaving_tracks = view->leaving_shown && (view->color_section == view->leaving.section);
  if (view->leaving_shown) {
    health_orbit_card_draw_content(ctx, &view->leaving, view->health_data, view);
  }
  health_orbit_card_draw_content(ctx, &view->card, view->health_data, view);
  if (view->leaving_shown) {
    health_orbit_card_draw_rings(ctx, &view->leaving, view->health_data, leaving_tracks);
  }
  health_orbit_card_draw_rings(ctx, &view->card, view->health_data, !leaving_tracks);
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
    MenuRoundShape rows[HEALTH_ORBIT_WEEK_NUM_ROWS];
    prv_get_week_rows(view, rows);
    health_orbit_week_draw(ctx, view->section, view->health_data, rows, view->week_header_offset_y,
                           view->crown, view);
  }
  prv_fill_sidebar(ctx, display, view->turn, health_orbit_section_color(view->color_section), bg);
  prv_draw_orbit(ctx, view, display);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Animations

//! Delays and lengths in frames, as the Moook curves count them
#define FRAMES(n) ((n) * ANIMATION_TARGET_FRAME_INTERVAL_MS)

//! The orbit turns and the cards slide as the timeline scrolls
#define SLIDE_MS TIMELINE_UP_DOWN_ANIMATION_DURATION_MS

//! The colors change as the slide bounces back, as the rect cards do
#define COLOR_SWAP_MS (SLIDE_MS - interpolate_moook_out_duration())

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

//! The card drops away and comes back on the Moook soft curve with one middle frame, as the rect
//! cards slide
#define CARD_DROP_FRAMES_MID 1

static int64_t prv_sweep_back_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_sweep_back);
}

static int64_t prv_turn_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_turn);
}

static int64_t prv_turn_back_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_custom(normalized, from, to, &s_turn_back);
}

static int64_t prv_drop_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_soft(normalized, from, to, CARD_DROP_FRAMES_MID);
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

//! Sets everything where it rests for the section, and the week or its card
static void prv_rest(HealthOrbitView *view) {
  view->animation = nullptr;
  view->leaving_shown = false;
  view->color_section = view->section;
  health_orbit_card_init(&view->card, view->section);
  view->orbit_offset = view->section * HEALTH_ORBIT_STEP;
  view->turn = view->week ? -QUARTER_TURN : 0;
  view->neighbour_radius = view->week ? 0 : NEIGHBOUR_BUBBLE_RADIUS;
  view->card_shown = !view->week;
  view->week_shown = view->week;
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    view->row_travel[i] = ROW_AT_REST;
  }
  view->week_header_offset_y = 0;
  layer_mark_dirty(&view->layer);
}

//! Redraws every frame, and changes the colors at their time
static void prv_frame_update(Animation *animation, AnimationProgress progress) {
  HealthOrbitView *view = animation_get_context(animation);
  const uint32_t elapsed = (int64_t)progress * animation_get_duration(animation, false, false) /
                           ANIMATION_NORMALIZED_MAX;
  if (elapsed >= COLOR_SWAP_MS) {
    view->color_section = view->section;
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

//! Up or Down: the orbit turns a step, the card slides out the way the button goes as the next
//! slides in, the old rings snap back, the new ones sweep in and its bubbles pop one after another
static void prv_animate_section(HealthOrbitView *view, HealthOrbitSection section, bool down) {
  prv_finish(view);
  const int16_t height = view->layer.bounds.size.h;
  Transition transition = {};

  view->leaving = view->card;
  view->leaving_shown = true;
  uint32_t sweeps[HEALTH_ORBIT_CARD_MAX_RINGS];
  uint32_t spans[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings =
      health_orbit_card_get_sweeps(view->section, view->health_data, sweeps, spans);
  for (int i = 0; i < num_rings; i++) {
    view->leaving.sweep[i] = sweeps[i];
    prv_add_uint32(&transition, &view->leaving.sweep[i], 0, 0,
                   interpolate_moook_custom_duration(&s_retract), prv_retract_curve);
  }
  prv_add_int16(&transition, &view->leaving.offset_y, down ? -height : height, 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);

  view->section = section;
  health_orbit_card_init(&view->card, section);
  view->card.offset_y = down ? height : -height;
  prv_add_int16(&transition, &view->card.offset_y, 0, 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
  const int num_new_rings =
      health_orbit_card_get_sweeps(section, view->health_data, sweeps, nullptr);
  for (int i = 0; i < num_new_rings; i++) {
    view->card.sweep[i] = 0;
    prv_add_uint32(&transition, &view->card.sweep[i], sweeps[i], FRAMES(3),
                   interpolate_moook_custom_duration(&s_sweep), prv_sweep_curve);
  }
  for (int i = 0; i < health_orbit_card_get_num_bubbles(section); i++) {
    view->card.bubble_radius[i] = 0;
    prv_add_int16(&transition, &view->card.bubble_radius[i], HEALTH_ORBIT_CARD_BUBBLE_RADIUS,
                  FRAMES(5 + i), interpolate_moook_duration(), prv_moook_curve);
  }

  prv_add_int16(&transition, &view->orbit_offset, section * HEALTH_ORBIT_STEP, 0, SLIDE_MS,
                timeline_animation_interpolate_moook_soft);
  prv_play(view, &transition);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Navigation

//! How far the line above the week rows is moved up to be off the display, under the banner
#define HEADER_HIDDEN_OFFSET (-(HEALTH_WEEK_ROW_TOP + 2))

//! Each row leaves the notch where the notch is when it starts: the first two four frames into the
//! turn, then two more a frame
static uint32_t prv_row_out_delay(int index) {
  return FRAMES(4 + index / 2);
}

//! Select: the card's rings snap back and its bubbles pop out as it drops away, the sidebar turns a
//! quarter round back to the top into the banner, and the week's rows drop out of its notch, the
//! most recent first, each a ball stretching into its pill
static void prv_animate_week_in(HealthOrbitView *view) {
  prv_finish(view);
  const GSize display = view->layer.bounds.size;
  Transition transition = {};
  view->week = true;
  view->week_shown = true;

  uint32_t sweeps[HEALTH_ORBIT_CARD_MAX_RINGS];
  uint32_t spans[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings =
      health_orbit_card_get_sweeps(view->section, view->health_data, sweeps, spans);
  for (int i = 0; i < num_rings; i++) {
    view->card.sweep[i] = sweeps[i];
    prv_add_uint32(&transition, &view->card.sweep[i], 0, 0,
                   interpolate_moook_custom_duration(&s_retract), prv_retract_curve);
    // The track snaps back with it
    view->card.track[i] = spans[i];
    prv_add_uint32(&transition, &view->card.track[i], 0, 0,
                   interpolate_moook_custom_duration(&s_retract), prv_retract_curve);
  }
  for (int i = 0; i < health_orbit_card_get_num_bubbles(view->section); i++) {
    prv_add_int16(&transition, &view->card.bubble_radius[i], 0, 0, interpolate_moook_in_duration(),
                  prv_in_only_curve);
  }
  prv_add_int16(&transition, &view->card.offset_y, display.h, 0,
                interpolate_moook_soft_duration(CARD_DROP_FRAMES_MID), prv_drop_curve);
  prv_add_int16(&transition, &view->neighbour_radius, 0, 0, interpolate_moook_in_duration(),
                prv_in_only_curve);

  const uint32_t turn_duration = interpolate_moook_custom_duration(&s_turn);
  prv_add_int16(&transition, &view->turn, -QUARTER_TURN, 0, turn_duration, prv_turn_curve);
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
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
  view->week_header_offset_y = HEADER_HIDDEN_OFFSET;
  prv_add_int16(&transition, &view->week_header_offset_y, 0, prv_row_out_delay(0) + FRAMES(3),
                interpolate_moook_duration(), prv_moook_curve);
  prv_play(view, &transition);
}

//! Back: the rows fold into balls and slip back into the notch, the oldest first, the banner turns
//! back down into the sidebar, the card comes back up as its rings sweep in and its bubbles pop
static void prv_animate_week_out(HealthOrbitView *view) {
  prv_finish(view);
  const GSize display = view->layer.bounds.size;
  Transition transition = {};
  view->week = false;
  view->card_shown = true;

  const GPoint notch = health_orbit_notch_tip(display, -QUARTER_TURN);
  for (int i = 0; i < HEALTH_ORBIT_WEEK_NUM_ROWS; i++) {
    view->row_start[i] = notch;
    view->row_travel[i] = prv_row_path_length(view, i);
    // Three rows a frame
    const uint32_t delay = FRAMES((HEALTH_ORBIT_WEEK_NUM_ROWS - 1 - i) / 3);
    prv_add_int16(&transition, &view->row_travel[i], 0, delay, interpolate_moook_duration(),
                  prv_moook_curve);
  }
  prv_add_int16(&transition, &view->week_header_offset_y, HEADER_HIDDEN_OFFSET, 0,
                interpolate_moook_in_duration(), prv_in_only_curve);

  const uint32_t turn_delay = FRAMES(3);
  prv_add_int16(&transition, &view->turn, 0, turn_delay,
                interpolate_moook_custom_duration(&s_turn_back), prv_turn_back_curve);

  health_orbit_card_init(&view->card, view->section);
  view->card.offset_y = display.h;
  prv_add_int16(&transition, &view->card.offset_y, 0, turn_delay,
                interpolate_moook_soft_duration(CARD_DROP_FRAMES_MID), prv_drop_curve);
  uint32_t sweeps[HEALTH_ORBIT_CARD_MAX_RINGS];
  uint32_t spans[HEALTH_ORBIT_CARD_MAX_RINGS];
  const int num_rings =
      health_orbit_card_get_sweeps(view->section, view->health_data, sweeps, spans);
  for (int i = 0; i < num_rings; i++) {
    view->card.sweep[i] = 0;
    prv_add_uint32(&transition, &view->card.sweep[i], sweeps[i], FRAMES(5),
                   interpolate_moook_custom_duration(&s_sweep_back), prv_sweep_back_curve);
    // The track sweeps in with it
    view->card.track[i] = 0;
    prv_add_uint32(&transition, &view->card.track[i], spans[i], FRAMES(5),
                   interpolate_moook_custom_duration(&s_sweep_back), prv_sweep_back_curve);
  }
  for (int i = 0; i < health_orbit_card_get_num_bubbles(view->section); i++) {
    view->card.bubble_radius[i] = 0;
    prv_add_int16(&transition, &view->card.bubble_radius[i], HEALTH_ORBIT_CARD_BUBBLE_RADIUS,
                  FRAMES(6 + i), interpolate_moook_duration(), prv_moook_curve);
  }
  prv_add_int16(&transition, &view->neighbour_radius, NEIGHBOUR_BUBBLE_RADIUS, FRAMES(6),
                interpolate_moook_duration(), prv_moook_curve);
  prv_play(view, &transition);
}

static void prv_show_section(HealthOrbitView *view, HealthOrbitSection section) {
  prv_finish(view);
  view->section = section;
  prv_rest(view);
}

static void prv_up_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  if (view->week) {
    return;
  }
  const bool down = (click_recognizer_get_button_id(recognizer) == BUTTON_ID_DOWN);
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

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  if (!view->week && health_orbit_week_has_section(view->section)) {
    prv_animate_week_in(view);
  }
}

static void prv_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
  if (view->week) {
    prv_animate_week_out(view);
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
  prv_show_section(view, (launch_args.card_type == HealthCardType_Sleep) ? HealthOrbitSectionSleep
                                                                         : HealthOrbitSectionToday);
  return view;
}

void health_orbit_view_destroy(HealthOrbitView *view) {
  prv_finish(view);
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
