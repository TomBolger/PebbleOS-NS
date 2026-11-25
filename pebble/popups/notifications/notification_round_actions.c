/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "notification_round_actions.h"

#if PBL_ROUND

#include <string.h>

#include <pbl/i18n/i18n.h>
#include <pbl/services/notifications/notification_style.h>
#include <pbl/services/timeline/notification_layout.h>
#include <pbl/services/timeline/timeline_actions.h>
#include <pbl/util/math.h>
#include <pbl/util/trig.h>

#include <applib/applib_malloc.auto.h>
#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/gdraw_command_transforms.h>
#include <applib/graphics/graphics.h>
#include <applib/graphics/graphics_circle.h>
#include <applib/graphics/text.h>
#include <applib/ui/action_menu_hierarchy.h>
#include <applib/ui/action_menu_window_private.h>
#include <applib/ui/animation.h>
#include <applib/ui/layer.h>
#include <applib/ui/status_bar_layer.h>
#include <applib/ui/vibes.h>
#include <applib/ui/window.h>

#ifdef CONFIG_TOUCH
#include <applib/ui/recognizer/pan.h>
#include <applib/ui/recognizer/touch_nav.h>
#include <kernel/pebble_tasks.h>
#include <process_state/app_state/app_state.h>
#endif

#define CENTER_X      (PBL_DISPLAY_WIDTH / 2)
#define CENTER_Y      (PBL_DISPLAY_HEIGHT / 2)
#define SCREEN_RADIUS (PBL_DISPLAY_WIDTH / 2)

// Where the banner settles: the same shape turned a quarter round, so it hangs off the left edge
// exactly like it hangs off the top (same circle, same depth, the icon in the same spot).
#define BUBBLE_RADIUS     BANNER_CIRCLE_RADIUS
#define BUBBLE_CENTER_X   LAYOUT_TOP_BANNER_CENTER_Y_ROUND
#define BUBBLE_ICON_INSET (LAYOUT_TOP_BANNER_ICON_CENTER_Y_ROUND - BUBBLE_CENTER_X)
// A banner scrolled out of sight starts from a small circle just above the screen.
#define HIDDEN_START_RADIUS  14
#define HIDDEN_START_Y       (-HIDDEN_START_RADIUS - 2)
#define HIDDEN_BANNER_BOTTOM 4

// Rows follow the text size (38 tall at the default size).
#define ROW_HEIGHT      (notification_style_scaled_points(24) + 14)
#define ROW_PILL_HEIGHT (ROW_HEIGHT - 6)
#define EDGE_ROWS       2
#define TEXT_LEFT       (BUBBLE_CENTER_X + BUBBLE_RADIUS + 8)
#define TEXT_RIGHT      (PBL_DISPLAY_WIDTH - 10)
#define ROW_EDGE_GAP    8
#define ROW_MIN_WIDTH   60
// A tap this far left of the text column still picks a row.
#define TAP_SLACK_PX 10

#define OPEN_MS  320
#define GLIDE_MS 150

#define MAX_DEPTH 4

typedef struct {
  ActionMenu *action_menu;
  NotificationRoundActionsClosedCb closed;
  void *context;
  Window *window;
  Layer layer;
  bool open;
  bool closing;

  GColor color;
  GDrawCommandImage *icon;
  GDrawCommandImage *scaled_icon;
  size_t icon_size;

  // The level shown and the ones it was reached from, each with its selection
  const ActionMenuLevel *levels[MAX_DEPTH];
  uint16_t selected[MAX_DEPTH];
  uint8_t depth;

  // Starting circle (the banner as it was on screen) in polar form around the screen centre
  int32_t start_angle;
  int32_t start_distance;
  int32_t start_radius;
  int32_t start_icon_inset;
  int16_t banner_cover_bottom; // rows 0..this are the old banner, covered as the motion starts
  Layer *card;                 // slid left in step with the actions' edge
  int16_t card_x;              // the card's resting x

  Animation *motion;
  int32_t progress; // 0..1000, linear in time; eased when drawing
  int32_t motion_from;
  int32_t motion_to;

  Animation *glide;
  int32_t list_pos;
  int32_t glide_from;
  int32_t glide_to;

#ifdef CONFIG_TOUCH
  TouchNavWidgetNode touch_node;
  bool touch_registered;
  int16_t drag_rows_applied;
#endif
} RoundActions;

static RoundActions s_menu;

// ---------------------------------------------------------------------------------------------
// Pure helpers

int32_t notification_round_actions_ease(int32_t t) {
  // 1 + (s + 1)(t - 1)^3 + s (t - 1)^2 with s = 1.2, in thousandths
  const int64_t u = t - 1000;
  const int64_t u2 = u * u / 1000;
  const int64_t u3 = u2 * u / 1000;
  return (int32_t)(1000 + (2200 * u3) / 1000 + (1200 * u2) / 1000);
}

int32_t notification_round_actions_scroll_for(uint16_t selected, uint16_t count,
                                              int16_t row_height) {
  const int32_t max_scroll = PBL_MAX(((int32_t)count - 1 - 2 * EDGE_ROWS) * row_height, 0);
  const int32_t target = PBL_MAX(((int32_t)selected - EDGE_ROWS) * row_height, 0);
  return PBL_MIN(target, max_scroll);
}

static int32_t prv_lerp(int32_t from, int32_t to, int32_t e) {
  return from + (to - from) * e / 1000;
}

static const ActionMenuLevel *prv_level(void) {
  return s_menu.levels[s_menu.depth - 1];
}

static uint16_t prv_count(void) {
  return prv_level()->num_items;
}

static uint16_t prv_selected(void) {
  return s_menu.selected[s_menu.depth - 1];
}

static bool prv_accepts_input(void) {
  return s_menu.open && !s_menu.closing && !action_menu_is_frozen(s_menu.action_menu);
}

// ---------------------------------------------------------------------------------------------
// Drawing

static void prv_draw_icon(GContext *ctx, GPoint center, int32_t scale) {
  GDrawCommandImage *icon = s_menu.icon;
  if (!icon || scale <= 0) {
    return;
  }
  const GSize size = gdraw_command_image_get_bounds_size(icon);
  if (scale >= 1000 || !s_menu.scaled_icon) {
    gdraw_command_image_draw(ctx, icon, GPoint(center.x - size.w / 2, center.y - size.h / 2));
    return;
  }
  const GSize scaled = GSize(size.w * scale / 1000, size.h * scale / 1000);
  if (scaled.w <= 0 || scaled.h <= 0) {
    return;
  }
  memcpy(s_menu.scaled_icon, icon, s_menu.icon_size);
  gdraw_command_image_scale(s_menu.scaled_icon, scaled);
  gdraw_command_image_draw(ctx, s_menu.scaled_icon,
                           GPoint(center.x - scaled.w / 2, center.y - scaled.h / 2));
}

static void prv_draw_rows(GContext *ctx, int16_t shift) {
  const ActionMenuLevel *level = prv_level();
  const uint16_t selected = prv_selected();
  const int16_t row_height = ROW_HEIGHT;
  const int16_t pill_height = ROW_PILL_HEIGHT;
  for (uint16_t i = 0; i < level->num_items; i++) {
    const int32_t center_y = CENTER_Y + ((int32_t)i - EDGE_ROWS) * row_height - s_menu.list_pos;
    if (center_y + row_height / 2 < 0 || center_y - row_height / 2 > PBL_DISPLAY_HEIGHT) {
      continue;
    }
    // Keep each row inside the round screen at its height
    const int32_t dy = center_y - CENTER_Y;
    const int32_t half_chord =
        integer_sqrt(PBL_MAX((int64_t)SCREEN_RADIUS * SCREEN_RADIUS - dy * dy, 0));
    const int32_t left = PBL_MAX(CENTER_X - half_chord + ROW_EDGE_GAP, TEXT_LEFT);
    const int32_t right = PBL_MIN(CENTER_X + half_chord - ROW_EDGE_GAP, TEXT_RIGHT);
    if (right - left < ROW_MIN_WIDTH) {
      continue;
    }

    const char *text = level->items[i].label ?: "";
    int16_t points = notification_style_scaled_points(24);
    GFont font = notification_style_scaled_font(24, true);
    const GSize size = graphics_text_layout_get_max_used_size(
        ctx, text, font, GRect(0, 0, 400, pill_height), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft, nullptr);
    if (size.w > right - left - 12) {
      // Too long: a step smaller
      points = notification_style_scaled_points(18);
      font = notification_style_scaled_font(18, true);
    }

    const GRect pill = GRect(left + shift, center_y - pill_height / 2, right - left, pill_height);
    if (i == selected) {
      graphics_context_set_fill_color(ctx, s_menu.color);
      graphics_fill_round_rect(ctx, &pill, pill_height / 2, GCornersAll);
      graphics_context_set_text_color(ctx, GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, notification_style_fg());
    }
    // Gothic draws its letters about a quarter of its size below the top of the box
    graphics_draw_text(
        ctx, text, font,
        GRect(pill.origin.x + 8, center_y - points * 7 / 10, pill.size.w - 16, points + 8),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, nullptr);
  }
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer->bounds;
  const int32_t e = notification_round_actions_ease(s_menu.progress);

  graphics_context_set_fill_color(ctx, notification_style_bg());
  if (s_menu.progress >= 1000) {
    graphics_fill_rect(ctx, &bounds);
  } else {
    // The actions come in from the right behind a sweeping edge; the card stays visible to its
    // left, without its banner, which is now the circle drawn below.
    if (s_menu.banner_cover_bottom >= 0) {
      graphics_fill_rect(ctx, &GRect(0, 0, bounds.size.w, s_menu.banner_cover_bottom + 1));
    }
    const int32_t edge = PBL_MAX(PBL_DISPLAY_WIDTH - PBL_DISPLAY_WIDTH * e / 1000, 0);
    if (edge < bounds.size.w) {
      graphics_fill_rect(ctx, &GRect(edge, 0, bounds.size.w - edge, bounds.size.h));
    }
  }

  prv_draw_rows(ctx, (int16_t)(PBL_DISPLAY_WIDTH * (1000 - e) / 1000));

  // The banner, swinging around the screen centre from the top to the left edge
  const int32_t angle = prv_lerp(s_menu.start_angle, TRIG_MAX_ANGLE / 2, e);
  const int32_t distance = prv_lerp(s_menu.start_distance, CENTER_X - BUBBLE_CENTER_X, e);
  const int32_t radius = PBL_MAX(prv_lerp(s_menu.start_radius, BUBBLE_RADIUS, e), 1);
  const int32_t cos_a = cos_lookup(angle);
  const int32_t sin_a = sin_lookup(angle);
  const GPoint center = GPoint(CENTER_X + cos_a * distance / TRIG_MAX_RATIO,
                               CENTER_Y + sin_a * distance / TRIG_MAX_RATIO);
  graphics_context_set_fill_color(ctx, s_menu.color);
  graphics_fill_circle(ctx, center, (uint16_t)radius);

  // The icon sits on the side of the circle facing the screen centre, so it turns with it
  const int32_t inset = prv_lerp(s_menu.start_icon_inset, BUBBLE_ICON_INSET, e);
  const GPoint icon_center =
      GPoint(center.x - cos_a * inset / TRIG_MAX_RATIO, center.y - sin_a * inset / TRIG_MAX_RATIO);
  prv_draw_icon(ctx, icon_center, PBL_MIN((radius - 6) * 1000 / 24, 1000));
}

// ---------------------------------------------------------------------------------------------
// Touch

#ifdef CONFIG_TOUCH
struct TouchNavState *modal_manager_get_touch_nav_state(void);

static TouchNavState *prv_touch_nav_state(void) {
  return (pebble_task_get_current() == PebbleTask_App) ? app_state_get_touch_nav_state()
                                                       : modal_manager_get_touch_nav_state();
}

static void prv_move(int16_t rows);
static void prv_activate(uint16_t index);

static bool prv_touch_can_start(void *w) {
  return prv_accepts_input();
}

static void prv_touch_down(void *w) {
  s_menu.drag_rows_applied = 0;
}

static void prv_touch_pan_started(void *w) {
  s_menu.drag_rows_applied = 0;
}

static GPointReturn prv_touch_base(void *w) {
  return GPointZero;
}

static void prv_touch_drag(void) {
  if (!prv_accepts_input()) {
    return;
  }
  const TouchNavState *state = prv_touch_nav_state();
  if (!state || !state->widget_pan) {
    return;
  }
  // Finger up moves down the list, a row per row height
  const int16_t dy = pan_recognizer_get_total_delta(state->widget_pan).y;
  const int16_t rows = -dy / ROW_HEIGHT;
  if (rows != s_menu.drag_rows_applied) {
    prv_move(rows - s_menu.drag_rows_applied);
    s_menu.drag_rows_applied = rows;
  }
}

static void prv_touch_pan_update(void *w, GPoint base, GPoint delta) {
  prv_touch_drag();
}

static void prv_touch_pan_snap(void *w, GPoint base, GPoint delta, GPoint velocity) {
  prv_touch_drag();
  s_menu.drag_rows_applied = 0;
}

static void prv_touch_pan_cancel(void *w) {
  s_menu.drag_rows_applied = 0;
}

static void prv_touch_tap(void *w, GPoint point) {
  if (!prv_accepts_input()) {
    return;
  }
  const int16_t row_height = ROW_HEIGHT;
  const int32_t from_first =
      point.y - CENTER_Y + EDGE_ROWS * row_height + s_menu.list_pos + row_height / 2;
  if (from_first < 0 || point.x < TEXT_LEFT - TAP_SLACK_PX) {
    return;
  }
  const int32_t index = from_first / row_height;
  if (index < prv_count()) {
    prv_activate((uint16_t)index);
  }
}

static void prv_touch_swipe(void *w, SwipeDirection dir) {
  if (dir == SwipeDirection_Right && prv_accepts_input()) {
    notification_round_actions_close(true);
  }
}

static const TouchNavWidgetOps s_touch_ops = {
  .can_start = prv_touch_can_start,
  .touchdown = prv_touch_down,
  .pan_started = prv_touch_pan_started,
  .get_base_offset = prv_touch_base,
  .pan_update = prv_touch_pan_update,
  .pan_snap = prv_touch_pan_snap,
  .pan_cancel = prv_touch_pan_cancel,
  .tap = prv_touch_tap,
  .swipe = prv_touch_swipe,
};

static void prv_touch_register(void) {
  TouchNavState *state = prv_touch_nav_state();
  if (s_menu.touch_registered || !state || !state->manager) {
    return;
  }
  touch_nav_registry_add(state, TouchNavWidgetType_Swap, &s_menu.touch_node, &s_menu.layer,
                         &s_touch_ops, &s_menu);
  s_menu.touch_registered = true;
}

static void prv_touch_deregister(void) {
  if (!s_menu.touch_registered) {
    return;
  }
  TouchNavState *state = prv_touch_nav_state();
  if (state) {
    const bool was_target = state->latched_target == &s_menu.touch_node;
    touch_nav_registry_remove(state, TouchNavWidgetType_Swap, &s_menu.touch_node);
    if (was_target && state->manager) {
      recognizer_manager_cancel_and_reset(state->manager);
    }
  }
  s_menu.touch_registered = false;
}
#else
static void prv_touch_register(void) {
}

static void prv_touch_deregister(void) {
}
#endif

// ---------------------------------------------------------------------------------------------
// Motion

static void prv_unschedule(Animation **animation) {
  Animation *old = *animation;
  *animation = nullptr;
  if (old) {
    animation_unschedule(old);
  }
}

//! Moves the card left in step with the edge the actions come in behind, so its text slides
//! out instead of being covered.
static void prv_slide_card(void) {
  if (!s_menu.card) {
    return;
  }
  const int32_t e = notification_round_actions_ease(s_menu.progress);
  GRect frame = s_menu.card->frame;
  frame.origin.x = s_menu.card_x - (int16_t)(PBL_DISPLAY_WIDTH * e / 1000);
  layer_set_frame(s_menu.card, &frame);
}

static void prv_destroy(void) {
  prv_unschedule(&s_menu.glide);
  prv_unschedule(&s_menu.motion);
  if (s_menu.card) {
    GRect frame = s_menu.card->frame;
    frame.origin.x = s_menu.card_x;
    layer_set_frame(s_menu.card, &frame);
  }
  prv_touch_deregister();
  layer_remove_from_parent(&s_menu.layer);
  layer_deinit(&s_menu.layer);
  gdraw_command_image_destroy(s_menu.scaled_icon);
  gdraw_command_image_destroy(s_menu.icon);
  ActionMenu *action_menu = s_menu.action_menu;
  const NotificationRoundActionsClosedCb closed = s_menu.closed;
  void *context = s_menu.context;
  s_menu = (RoundActions){};
  if (closed) {
    closed(action_menu, context);
  }
}

static void prv_motion_update(Animation *animation, const AnimationProgress p) {
  s_menu.progress = s_menu.motion_from +
                    (s_menu.motion_to - s_menu.motion_from) * (int32_t)p / ANIMATION_NORMALIZED_MAX;
  prv_slide_card();
  layer_mark_dirty(&s_menu.layer);
}

static void prv_motion_stopped(Animation *animation, bool finished, void *context) {
  if (animation != s_menu.motion) {
    return;
  }
  s_menu.motion = nullptr;
  s_menu.progress = s_menu.motion_to;
  prv_slide_card();
  if (s_menu.closing) {
    prv_destroy();
  } else {
    layer_mark_dirty(&s_menu.layer);
  }
}

static void prv_run_motion(int32_t to) {
  prv_unschedule(&s_menu.motion);
  static const AnimationImplementation s_implementation = {
    .update = prv_motion_update,
  };
  s_menu.motion_from = s_menu.progress;
  s_menu.motion_to = to;
  const int32_t distance = PBL_ABS(to - s_menu.progress);
  Animation *motion = animation_create();
  if (motion) {
    s_menu.motion = motion;
    animation_set_implementation(motion, &s_implementation);
    animation_set_duration(motion, (uint32_t)(OPEN_MS * distance / 1000) + 1);
    animation_set_curve(motion, AnimationCurveLinear);
    animation_set_handlers(motion,
                           (AnimationHandlers){
                             .stopped = prv_motion_stopped,
                           },
                           nullptr);
    if (animation_schedule(motion)) {
      return;
    }
    s_menu.motion = nullptr;
  }
  // No animation: jump to the end
  s_menu.progress = to;
  prv_slide_card();
  if (s_menu.closing) {
    prv_destroy();
  }
}

static void prv_glide_update(Animation *animation, const AnimationProgress p) {
  s_menu.list_pos = s_menu.glide_from +
                    (s_menu.glide_to - s_menu.glide_from) * (int32_t)p / ANIMATION_NORMALIZED_MAX;
  layer_mark_dirty(&s_menu.layer);
}

static void prv_glide_stopped(Animation *animation, bool finished, void *context) {
  if (animation == s_menu.glide) {
    s_menu.glide = nullptr;
    s_menu.list_pos = s_menu.glide_to;
  }
}

//! Glide the rows to show the selection.
static void prv_selection_changed(bool animated) {
  const int32_t target =
      notification_round_actions_scroll_for(prv_selected(), prv_count(), ROW_HEIGHT);
  prv_unschedule(&s_menu.glide);
  layer_mark_dirty(&s_menu.layer);
  if (animated && target != s_menu.list_pos) {
    static const AnimationImplementation s_implementation = {
      .update = prv_glide_update,
    };
    Animation *glide = animation_create();
    if (glide) {
      s_menu.glide = glide;
      s_menu.glide_from = s_menu.list_pos;
      s_menu.glide_to = target;
      animation_set_implementation(glide, &s_implementation);
      animation_set_duration(glide, GLIDE_MS);
      animation_set_curve(glide, AnimationCurveEaseOut);
      animation_set_handlers(glide,
                             (AnimationHandlers){
                               .stopped = prv_glide_stopped,
                             },
                             nullptr);
      if (animation_schedule(glide)) {
        return;
      }
      s_menu.glide = nullptr;
    }
  }
  s_menu.list_pos = target;
}

// ---------------------------------------------------------------------------------------------
// Navigation

static void prv_move(int16_t rows) {
  uint16_t *selected = &s_menu.selected[s_menu.depth - 1];
  const int32_t target = (int32_t)*selected + rows;
  if (target < 0 || target >= prv_count()) {
    // Already at the end
    vibes_short_pulse();
  }
  const uint16_t clamped = (uint16_t)PBL_CLAMP(target, 0, (int32_t)prv_count() - 1);
  if (clamped != *selected) {
    *selected = clamped;
    prv_selection_changed(true);
  }
}

static void prv_enter_level(const ActionMenuLevel *level) {
  if (s_menu.depth >= MAX_DEPTH || level->num_items == 0) {
    return;
  }
  s_menu.levels[s_menu.depth] = level;
  s_menu.selected[s_menu.depth] =
      (uint16_t)PBL_MIN(level->default_selected_item, (unsigned)level->num_items - 1);
  s_menu.depth++;
  prv_selection_changed(false);
}

static void prv_activate(uint16_t index) {
  const ActionMenuLevel *level = prv_level();
  if (index >= level->num_items) {
    return;
  }
  s_menu.selected[s_menu.depth - 1] = index;
  prv_selection_changed(true);
  const ActionMenuItem *item = &level->items[index];
  if (item->is_leaf) {
    action_menu_hosted_perform(s_menu.action_menu, item);
  } else if (item->next_level) {
    // Submenus replace the rows in place
    prv_enter_level(item->next_level);
  }
}

static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_accepts_input()) {
    prv_move(-1);
  }
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_accepts_input()) {
    prv_move(1);
  }
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_accepts_input()) {
    prv_activate(prv_selected());
  }
}

static void prv_back_click(ClickRecognizerRef recognizer, void *context) {
  if (!prv_accepts_input()) {
    return;
  }
  if (s_menu.depth > 1) {
    s_menu.depth--;
    prv_selection_changed(false);
  } else {
    notification_round_actions_close(true);
  }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);
}

// ---------------------------------------------------------------------------------------------
// Open / close

static void prv_load_icon(const AppResourceInfo *icon) {
  s_menu.icon = gdraw_command_image_create_with_resource_system(icon->res_app_num, icon->res_id);
  if (s_menu.icon) {
    s_menu.icon_size = gdraw_command_image_get_data_size(s_menu.icon);
    s_menu.scaled_icon = gdraw_command_image_clone(s_menu.icon);
  }
}

bool notification_round_actions_open(Window *window, ActionMenu *action_menu,
                                     const NotificationRoundActionsBanner *banner,
                                     NotificationRoundActionsClosedCb closed, void *context) {
  const ActionMenuLevel *root = action_menu_get_root_level(action_menu);
  if (s_menu.open || !root || root->num_items == 0) {
    return false;
  }

  s_menu = (RoundActions){
    .action_menu = action_menu,
    .closed = closed,
    .context = context,
    .window = window,
    .open = true,
    .color = banner->color,
    .card = banner->card,
    .card_x = banner->card ? banner->card->frame.origin.x : 0,
  };
  prv_load_icon(&banner->icon);
  s_menu.levels[0] = root;
  s_menu.selected[0] =
      (uint16_t)PBL_MIN(root->default_selected_item, (unsigned)root->num_items - 1);
  s_menu.depth = 1;

  int32_t start_y;
  if (banner->bottom > HIDDEN_BANNER_BOTTOM) {
    // The banner as it is on screen: a big circle whose lower edge is the banner's curve
    s_menu.start_radius = BANNER_CIRCLE_RADIUS;
    start_y = banner->bottom - BANNER_CIRCLE_RADIUS;
    s_menu.start_icon_inset = banner->icon_center_y - start_y;
    s_menu.banner_cover_bottom = banner->bottom;
  } else {
    // Scrolled away: grow in from just above the top edge
    s_menu.start_radius = HIDDEN_START_RADIUS;
    start_y = HIDDEN_START_Y;
    s_menu.start_icon_inset = 0;
    s_menu.banner_cover_bottom = -1;
  }
  s_menu.start_distance = CENTER_Y - start_y;
  s_menu.start_angle = atan2_lookup((int16_t)(start_y - CENTER_Y), 0);
  if (s_menu.start_angle < TRIG_MAX_ANGLE / 2) {
    s_menu.start_angle += TRIG_MAX_ANGLE;
  }
  s_menu.list_pos = notification_round_actions_scroll_for(prv_selected(), prv_count(), ROW_HEIGHT);

  Layer *root_layer = window_get_root_layer(window);
  layer_init(&s_menu.layer, &root_layer->bounds);
  layer_set_update_proc(&s_menu.layer, prv_update_proc);
  layer_add_child(root_layer, &s_menu.layer);
  window_set_click_config_provider_with_context(window, prv_click_config_provider, &s_menu);
  prv_touch_register();
  prv_run_motion(1000);
  return true;
}

static void prv_timeline_menu_host_close(ActionMenu *action_menu, bool animated, void *context) {
  // Only the menu shown: one that could not open must not close another's
  if (s_menu.action_menu == action_menu) {
    notification_round_actions_close(animated);
  }
}

static const ActionMenuHost s_timeline_menu_host = {
  .close = prv_timeline_menu_host_close,
};

static void prv_timeline_menu_closed(ActionMenu *action_menu, void *context) {
  action_menu_hosted_did_close(action_menu);
}

ActionMenu *notification_round_actions_open_timeline_menu(
    Window *window, ActionMenuConfig *config, WindowStack *window_stack,
    const NotificationRoundActionsBanner *banner) {
  ActionMenu *action_menu = timeline_actions_open_hosted_action_menu(
      config, window_stack, &s_timeline_menu_host, nullptr);
  if (!action_menu) {
    action_menu_hierarchy_destroy(config->root_level, nullptr, nullptr);
    pbl_i18n_free_all(config->root_level);
    return nullptr;
  }
  if (!notification_round_actions_open(window, action_menu, banner, prv_timeline_menu_closed,
                                       nullptr)) {
    // Nothing on screen to close: finish it off right away
    action_menu_close(action_menu, false);
    action_menu_hosted_did_close(action_menu);
    return nullptr;
  }
  return action_menu;
}

void notification_round_actions_close(bool animated) {
  if (!s_menu.open) {
    return;
  }
  s_menu.closing = true;
  if (animated) {
    prv_run_motion(0);
    return;
  }
  prv_destroy();
}

#endif // PBL_ROUND
