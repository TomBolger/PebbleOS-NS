/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_view.h"

#include "orbit_card.h"
#include "orbit_draw.h"
#include "orbit_layout.h"

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
#include <applib/ui/app_window_stack.h>
#include <applib/ui/ui.h>
#include <resource/resource_ids.auto.h>

#if PBL_ROUND

//! The section bubbles: the selected one at the notch, white with the section's icon in its
//! color, the others black with their icon in their color
#define SELECTED_BUBBLE_RADIUS  17
#define NEIGHBOUR_BUBBLE_RADIUS 12
#define SELECTED_ICON_SIZE      21
#define NEIGHBOUR_ICON_SIZE     13

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
  //! Section of the card shown
  HealthOrbitSection section;
  HealthOrbitCard card;
  //! Whether Up past the first section and Down past the last one wrap round
  bool wrap;

  //! Rows the orbit has turned by, HEALTH_ORBIT_STEP per section from the first
  int16_t orbit_offset;
  //! Angle the sidebar is turned by, back to the top as the week shows
  int16_t turn;
  //! Radius of the bubbles of the sections away from the notch
  int16_t neighbour_radius;
};

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
//! timeline fills it, a row or a column at a time with its notch; on the way, its circle with the
//! notch cut out in the background color.
static void prv_fill_sidebar(GContext *ctx, GSize display, int32_t turn, GColor color, GColor bg) {
  graphics_context_set_fill_color(ctx, color);
  if (turn == 0 || turn == -QUARTER_TURN) {
    const TimelineListSidebar sidebar = health_orbit_sidebar(display, true);
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
  // The notch from a little outside the circle's edge to its tip, as it is at rest
  const TimelineListSidebar plain = health_orbit_sidebar(display, false);
  const int16_t half = HEALTH_ORBIT_NOTCH_HEIGHT / 2;
  const int16_t middle = display.h / 2;
  const int16_t base_x = display.w - timeline_list_sidebar_depth(&plain, middle + half) - 1;
  GPoint points[] = {
    health_orbit_turn(display, GPoint(base_x, middle - half), turn),
    health_orbit_notch_tip(display, turn),
    health_orbit_turn(display, GPoint(base_x, middle + half), turn),
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
  const GDrawState state = health_orbit_clip(
      ctx, GRect(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
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
        (SELECTED_BUBBLE_RADIUS - view->neighbour_radius) * closeness / HEALTH_ORBIT_STEP;
    const GPoint center = health_orbit_bubble_center(display, offset, view->turn);
    prv_draw_bubble(ctx, view, i, center, radius, 2 * closeness > HEALTH_ORBIT_STEP);
  }
  graphics_context_set_antialiased(ctx, false);
}

static void prv_layer_update_proc(Layer *layer, GContext *ctx) {
  HealthOrbitView *view = window_get_user_data(layer_get_window(layer));
  const GSize display = layer->bounds.size;
  const GColor bg = health_orbit_section_bg_color(view->section);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, &layer->bounds);

  health_orbit_card_draw_content(ctx, &view->card, view->health_data, view);
  health_orbit_card_draw_rings(ctx, &view->card, view->health_data, true /* tracks */);
  prv_fill_sidebar(ctx, display, view->turn, health_orbit_section_color(view->section), bg);
  prv_draw_orbit(ctx, view, display);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Navigation

static void prv_show_section(HealthOrbitView *view, HealthOrbitSection section) {
  view->section = section;
  health_orbit_card_init(&view->card, section);
  view->orbit_offset = section * HEALTH_ORBIT_STEP;
  layer_mark_dirty(&view->layer);
}

static void prv_up_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  HealthOrbitView *view = context;
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
  prv_show_section(view, next);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_up_down_click_handler);
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
  for (int i = 0; i < HealthOrbitSectionCount; i++) {
    gdraw_command_image_destroy(view->icons[i].selected);
    gdraw_command_image_destroy(view->icons[i].neighbour);
  }
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
