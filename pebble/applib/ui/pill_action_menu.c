/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pill_action_menu.h"

#include <stddef.h>
#include <string.h>

#include <pbl/services/timeline/notification_layout.h>
#include <pbl/util/math.h>

#include <applib/applib_malloc.auto.h>
#include <applib/fonts/fonts.h>
#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/gdraw_command_transforms.h>
#include <applib/graphics/graphics.h>
#include <applib/graphics/graphics_private.h>
#include <applib/graphics/text.h>
#include <applib/ui/action_menu_window_private.h>
#include <applib/ui/animation.h>
#include <applib/ui/layer.h>
#include <applib/ui/menu_layer_private.h>
#include <shell/prefs.h>

#ifdef CONFIG_TOUCH
#include <applib/ui/recognizer/pan.h>
#include <applib/ui/recognizer/touch_nav.h>
#include <kernel/pebble_tasks.h>
#include <process_state/app_state/app_state.h>
#endif

#if PBL_ROUND

//! An ease out with a small overshoot, as the round notification action menu opens, for time
//! \a t in thousandths; played backwards for closing
static int32_t prv_ease(int32_t t) {
  // 1 + (s + 1)(t - 1)^3 + s (t - 1)^2 with s = 1.2, in thousandths
  const int64_t u = t - 1000;
  const int64_t u2 = u * u / 1000;
  const int64_t u3 = u2 * u / 1000;
  return (int32_t)(1000 + (2200 * u3) / 1000 + (1200 * u2) / 1000);
}

#define CENTER_X      (PBL_DISPLAY_WIDTH / 2)
#define CENTER_Y      (PBL_DISPLAY_HEIGHT / 2)
#define SCREEN_RADIUS (PBL_DISPLAY_WIDTH / 2)

// The circle the pill settles into: the round notification banner's circle, turned a quarter
// round so it hangs off the left edge as deep as the banner hangs off the top, with the icon where
// the banner has it.
#define BUBBLE_RADIUS     BANNER_CIRCLE_RADIUS
#define BUBBLE_CENTER_X   LAYOUT_TOP_BANNER_CENTER_Y_ROUND
#define BUBBLE_ICON_INSET (LAYOUT_TOP_BANNER_ICON_CENTER_Y_ROUND - BUBBLE_CENTER_X)

//! The first row starts this many rows above the centre, and the last stops as many below it
#define EDGE_ROWS 2

#define ROW_HEIGHT      38
#define ROW_PILL_HEIGHT (ROW_HEIGHT - 6)
#define TEXT_LEFT       (BUBBLE_CENTER_X + BUBBLE_RADIUS + 8)
#define TEXT_RIGHT      (PBL_DISPLAY_WIDTH - 10)
#define ROW_EDGE_GAP    8
#define ROW_MIN_WIDTH   60
// A tap this far left of the text column still picks a row.
#define TAP_SLACK_PX 10

//! A shape with corners this round or more is filled a scanline at a time
#define LARGE_SHAPE_RADIUS 64
//! The row's content is clipped to the morphing shape in this many bands
#define CONTENT_BANDS 6

#define OPEN_MS  320
#define GLIDE_MS 150

#define CLICK_REPEAT_MS 100

#define MAX_DEPTH 4

typedef struct PillActionMenu {
  ActionMenu *action_menu;
  Window *window;
  MenuLayer *menu_layer;
  Layer layer;
  bool open;
  bool closing;

  GColor color;
  GColor text_color;
  GColor background;
  GColor foreground;
  ClickConfigProvider window_click_config;
  void *window_click_context;
  bool window_overrides_back;

  GDrawCommandImage *icon;
  GDrawCommandImage *scaled_icon;
  size_t icon_size;

  // The level shown and the ones it was reached from, each with its selection
  const ActionMenuLevel *levels[MAX_DEPTH];
  uint16_t selected[MAX_DEPTH];
  uint8_t depth;

  // The selected row of the menu, on screen with the menu at rest: its pill and its cell
  MenuRoundShape row_pill;
  GRect row_frame;

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
} PillActionMenu;

// ---------------------------------------------------------------------------------------------
// Helpers

static int32_t prv_lerp(int32_t from, int32_t to, int32_t e) {
  return from + (((to - from) * e) / 1000);
}

static const ActionMenuLevel *prv_level(PillActionMenu *menu) {
  return menu->levels[menu->depth - 1];
}

static uint16_t prv_count(PillActionMenu *menu) {
  return prv_level(menu)->num_items;
}

static uint16_t prv_selected(PillActionMenu *menu) {
  return menu->selected[menu->depth - 1];
}

static bool prv_accepts_input(PillActionMenu *menu) {
  return menu->open && !menu->closing && !action_menu_is_frozen(menu->action_menu);
}

static MenuRoundShape prv_bubble(void) {
  return (MenuRoundShape){
    .box = GRect(BUBBLE_CENTER_X - BUBBLE_RADIUS, CENTER_Y - BUBBLE_RADIUS, 2 * BUBBLE_RADIUS,
                 2 * BUBBLE_RADIUS),
    .radius = BUBBLE_RADIUS,
  };
}

//! Where the menu's selected row is now, on screen, with the window's content at rest
static void prv_capture_row(PillActionMenu *menu) {
  MenuLayer *menu_layer = menu->menu_layer;
  Layer *root = window_get_root_layer(menu->window);
  const GRect slid = root->bounds;
  root->bounds.origin.x = 0;
  GRect content;
  layer_get_global_frame(&menu_layer->scroll_layer.content_sublayer, &content);
  const GRect highlight = menu_layer->inverter.layer.frame;
  menu->row_frame =
      GRect(content.origin.x + highlight.origin.x, content.origin.y + highlight.origin.y,
            highlight.size.w, highlight.size.h);
  if (!menu_layer_get_round_highlight(menu_layer, &menu->row_pill)) {
    menu->row_pill = (MenuRoundShape){
      .box = grect_inset(menu->row_frame, GEdgeInsets(3)),
      .radius = (menu->row_frame.size.h - 6) / 2,
    };
  }
  root->bounds = slid;
}

// ---------------------------------------------------------------------------------------------
// Drawing

static void prv_draw_icon(PillActionMenu *menu, GContext *ctx, GPoint center, int32_t scale) {
  GDrawCommandImage *icon = menu->icon;
  if (!icon || (scale <= 0)) {
    return;
  }
  const GSize size = gdraw_command_image_get_bounds_size(icon);
  if ((scale >= 1000) || !menu->scaled_icon) {
    gdraw_command_image_draw(ctx, icon, GPoint(center.x - size.w / 2, center.y - size.h / 2));
    return;
  }
  const GSize scaled = GSize(size.w * scale / 1000, size.h * scale / 1000);
  if ((scaled.w <= 0) || (scaled.h <= 0)) {
    return;
  }
  memcpy(menu->scaled_icon, icon, menu->icon_size);
  gdraw_command_image_scale(menu->scaled_icon, scaled);
  gdraw_command_image_draw(ctx, menu->scaled_icon,
                           GPoint(center.x - scaled.w / 2, center.y - scaled.h / 2));
}

static void prv_draw_rows(PillActionMenu *menu, GContext *ctx, int16_t shift) {
  const ActionMenuLevel *level = prv_level(menu);
  const uint16_t selected = prv_selected(menu);
  for (uint16_t i = 0; i < level->num_items; i++) {
    const int32_t center_y = CENTER_Y + ((int32_t)i - EDGE_ROWS) * ROW_HEIGHT - menu->list_pos;
    if ((center_y + (ROW_HEIGHT / 2) < 0) || (center_y - (ROW_HEIGHT / 2) > PBL_DISPLAY_HEIGHT)) {
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
    int16_t points = 24;
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    const GSize size = graphics_text_layout_get_max_used_size(
        ctx, text, font, GRect(0, 0, 400, ROW_PILL_HEIGHT), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft, nullptr);
    if (size.w > right - left - 12) {
      // Too long: a step smaller
      points = 18;
      font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    }

    const GRect pill =
        GRect(left + shift, center_y - (ROW_PILL_HEIGHT / 2), right - left, ROW_PILL_HEIGHT);
    if (i == selected) {
      graphics_context_set_fill_color(ctx, menu->color);
      graphics_fill_round_rect(ctx, &pill, ROW_PILL_HEIGHT / 2, GCornersAll);
      graphics_context_set_text_color(ctx, menu->text_color);
    } else {
      graphics_context_set_text_color(ctx, menu->foreground);
    }
    // Gothic draws its letters about a quarter of its size below the top of the box
    graphics_draw_text(
        ctx, text, font,
        GRect(pill.origin.x + 8, center_y - (points * 7 / 10), pill.size.w - 16, points + 8),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, nullptr);
  }
}

//! The selected row's content, as the menu draws it highlighted, where the row is now, within
//! \a clip: it stays in the highlight colours inside the morphing shape as the menu slides away
static void prv_draw_row_content(PillActionMenu *menu, GContext *ctx, GRect clip, int16_t shift) {
  MenuLayer *menu_layer = menu->menu_layer;
  if (!menu_layer->callbacks.draw_row) {
    return;
  }
  const GDrawState prev_state = graphics_context_get_drawing_state(ctx);
  const GRect row = menu->row_frame;
  Layer cell = {
    .frame = GRect(0, 0, row.size.w, row.size.h),
    .bounds = GRect(0, 0, row.size.w, row.size.h),
    .is_highlighted = true,
  };
  GRect drawing_box = ctx->draw_state.drawing_box;
  drawing_box.origin.x += row.origin.x + shift;
  drawing_box.origin.y += row.origin.y;
  drawing_box.size = row.size;
  ctx->draw_state.drawing_box = drawing_box;
  clip.origin = gpoint_add(clip.origin, prev_state.drawing_box.origin);
  grect_clip(&clip, &prev_state.clip_box);
  ctx->draw_state.clip_box = clip;
  ctx->draw_state.text_color = menu->text_color;
  ctx->draw_state.tint_color = menu->text_color;
  ctx->draw_state.fill_color = menu->color;
  MenuIndex index = menu_layer->selection.index;
  menu_layer->callbacks.draw_row(ctx, &cell, &index, menu_layer->callback_context);
  graphics_context_set_drawing_state(ctx, prev_state);
}

//! Fills \a shape as the menu fills its pill while it is small. Bigger, the round rect and circle
//! fills leave steps along corners that size, so fill it a scanline at a time, from where its
//! round ends cross each one, with antialiased ends.
static void prv_fill_shape(GContext *ctx, const MenuRoundShape *shape) {
  const GRect box = shape->box;
  const int32_t r = shape->radius;
  if (r < LARGE_SHAPE_RADIUS) {
    graphics_fill_round_rect(ctx, &box, (uint16_t)r, GCornersAll);
    return;
  }
  GBitmap *framebuffer = graphics_capture_frame_buffer(ctx);
  if (!framebuffer) {
    return;
  }
  const GPoint origin = ctx->draw_state.drawing_box.origin;
  const GRect clip = ctx->draw_state.clip_box;
  const int32_t one = FIXED_S16_3_ONE.raw_value;
  const int32_t r_fixed = r * one;
  // The centres of the round ends' rows, in fixed point
  const int32_t top_center = (box.origin.y * one) + r_fixed;
  const int32_t bottom_center = (grect_get_max_y(&box) * one) - r_fixed;
  const int16_t first = PBL_MAX(box.origin.y, clip.origin.y - origin.y);
  const int16_t last = PBL_MIN(grect_get_max_y(&box), grect_get_max_y(&clip) - origin.y);
  for (int16_t y = first; y < last; y++) {
    const int32_t row_center = (y * one) + (one / 2);
    int32_t dy = 0;
    if (row_center < top_center) {
      dy = top_center - row_center;
    } else if (row_center > bottom_center) {
      dy = row_center - bottom_center;
    }
    if (dy >= r_fixed) {
      continue;
    }
    const int32_t inset =
        r_fixed - (int32_t)integer_sqrt(((int64_t)r_fixed * r_fixed) - ((int64_t)dy * dy));
    const Fixed_S16_3 left = {.raw_value = (int16_t)(((box.origin.x + origin.x) * one) + inset)};
    const Fixed_S16_3 right = {
      .raw_value = (int16_t)(((grect_get_max_x(&box) + origin.x) * one) - inset - one)
    };
    if (right.raw_value >= left.raw_value) {
      graphics_private_draw_horizontal_line_prepared(ctx, framebuffer, &ctx->draw_state.clip_box,
                                                     y + origin.y, left, right,
                                                     ctx->draw_state.fill_color);
    }
  }
  graphics_release_frame_buffer(ctx, framebuffer);
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  PillActionMenu *menu = (PillActionMenu *)((uint8_t *)layer - offsetof(PillActionMenu, layer));
  const GRect bounds = layer->bounds;
  const int32_t e = prv_ease(menu->progress);

  // The actions come in from the right behind a sweeping edge, as the menu slides out to the left
  graphics_context_set_fill_color(ctx, menu->background);
  const int32_t edge = PBL_MAX(PBL_DISPLAY_WIDTH - (PBL_DISPLAY_WIDTH * e / 1000), 0);
  if (edge < bounds.size.w) {
    graphics_fill_rect(ctx, &GRect(edge, 0, bounds.size.w - edge, bounds.size.h));
  }
  prv_draw_rows(menu, ctx, (int16_t)(PBL_DISPLAY_WIDTH * (1000 - e) / 1000));

  // The row's pill moves left and morphs into the circle, a single shape on every frame
  const MenuRoundShape bubble = prv_bubble();
  const MenuRoundShape shape = menu_round_morph(&menu->row_pill, &bubble, e);
  graphics_context_set_fill_color(ctx, menu->color);
  prv_fill_shape(ctx, &shape);
  // The row's content stays in the highlight colours inside the shape, in bands narrowed to its
  // round ends, so none of it shows over the background beside them
  const GRect box = shape.box;
  const int32_t r = shape.radius;
  const int16_t row_top = menu->row_frame.origin.y;
  const int16_t row_bottom = grect_get_max_y(&menu->row_frame);
  const int16_t band_h = (menu->row_frame.size.h + CONTENT_BANDS - 1) / CONTENT_BANDS;
  for (int16_t y0 = row_top; y0 < row_bottom; y0 += band_h) {
    const int16_t top = PBL_MAX(y0, box.origin.y);
    const int16_t bottom = PBL_MIN(PBL_MIN(y0 + band_h, row_bottom), grect_get_max_y(&box));
    if (bottom <= top) {
      continue;
    }
    const int32_t d =
        PBL_CLAMP(PBL_MAX((box.origin.y + r) - top, bottom - (grect_get_max_y(&box) - r)), 0, r);
    const int16_t inset = (int16_t)(r - integer_sqrt((r * r) - (d * d)));
    const GRect band = GRect(box.origin.x + inset, top, box.size.w - (2 * inset), bottom - top);
    if (band.size.w > 0) {
      prv_draw_row_content(menu, ctx, band, (int16_t)(-PBL_DISPLAY_WIDTH * e / 1000));
    }
  }

  // The icon grows in from the pill's left end to where the circle holds it
  const GPoint from = GPoint(menu->row_pill.box.origin.x + menu->row_pill.radius,
                             menu->row_pill.box.origin.y + (menu->row_pill.box.size.h / 2));
  const GPoint to = GPoint(BUBBLE_CENTER_X + BUBBLE_ICON_INSET, CENTER_Y);
  prv_draw_icon(menu, ctx, GPoint(prv_lerp(from.x, to.x, e), prv_lerp(from.y, to.y, e)),
                PBL_CLAMP(e, 0, 1000));
}

// ---------------------------------------------------------------------------------------------
// Touch

static void prv_move(PillActionMenu *menu, int16_t rows);
static void prv_activate(PillActionMenu *menu, uint16_t index);
static void prv_close(PillActionMenu *menu);

#ifdef CONFIG_TOUCH
struct TouchNavState *modal_manager_get_touch_nav_state(void);

static TouchNavState *prv_touch_nav_state(void) {
  return (pebble_task_get_current() == PebbleTask_App) ? app_state_get_touch_nav_state()
                                                       : modal_manager_get_touch_nav_state();
}

static bool prv_touch_can_start(void *w) {
  return prv_accepts_input(w);
}

static void prv_touch_reset(void *w) {
  ((PillActionMenu *)w)->drag_rows_applied = 0;
}

static GPointReturn prv_touch_base(void *w) {
  return GPointZero;
}

static void prv_touch_drag(PillActionMenu *menu) {
  if (!prv_accepts_input(menu)) {
    return;
  }
  const TouchNavState *state = prv_touch_nav_state();
  if (!state || !state->widget_pan) {
    return;
  }
  // Finger up moves down the list, a row per row height
  const int16_t dy = pan_recognizer_get_total_delta(state->widget_pan).y;
  const int16_t rows = -dy / ROW_HEIGHT;
  if (rows != menu->drag_rows_applied) {
    prv_move(menu, rows - menu->drag_rows_applied);
    menu->drag_rows_applied = rows;
  }
}

static void prv_touch_pan_update(void *w, GPoint base, GPoint delta) {
  prv_touch_drag(w);
}

static void prv_touch_pan_snap(void *w, GPoint base, GPoint delta, GPoint velocity) {
  prv_touch_drag(w);
  prv_touch_reset(w);
}

static void prv_touch_tap(void *w, GPoint point) {
  PillActionMenu *menu = w;
  if (!prv_accepts_input(menu)) {
    return;
  }
  const int32_t from_first =
      point.y - CENTER_Y + (EDGE_ROWS * ROW_HEIGHT) + menu->list_pos + (ROW_HEIGHT / 2);
  if ((from_first < 0) || (point.x < TEXT_LEFT - TAP_SLACK_PX)) {
    return;
  }
  const int32_t index = from_first / ROW_HEIGHT;
  if (index < prv_count(menu)) {
    prv_activate(menu, (uint16_t)index);
  }
}

static void prv_touch_swipe(void *w, SwipeDirection dir) {
  if ((dir == SwipeDirection_Right) && prv_accepts_input(w)) {
    prv_close(w);
  }
}

static const TouchNavWidgetOps s_touch_ops = {
  .can_start = prv_touch_can_start,
  .touchdown = prv_touch_reset,
  .pan_started = prv_touch_reset,
  .get_base_offset = prv_touch_base,
  .pan_update = prv_touch_pan_update,
  .pan_snap = prv_touch_pan_snap,
  .pan_cancel = prv_touch_reset,
  .tap = prv_touch_tap,
  .swipe = prv_touch_swipe,
};

static void prv_touch_register(PillActionMenu *menu) {
  TouchNavState *state = prv_touch_nav_state();
  if (menu->touch_registered || !state || !state->manager) {
    return;
  }
  touch_nav_registry_add(state, TouchNavWidgetType_Swap, &menu->touch_node, &menu->layer,
                         &s_touch_ops, menu);
  menu->touch_registered = true;
}

static void prv_touch_deregister(PillActionMenu *menu) {
  if (!menu->touch_registered) {
    return;
  }
  TouchNavState *state = prv_touch_nav_state();
  if (state) {
    const bool was_target = state->latched_target == &menu->touch_node;
    touch_nav_registry_remove(state, TouchNavWidgetType_Swap, &menu->touch_node);
    if (was_target && state->manager) {
      recognizer_manager_cancel_and_reset(state->manager);
    }
  }
  menu->touch_registered = false;
}
#else
static void prv_touch_register(PillActionMenu *menu) {
}

static void prv_touch_deregister(PillActionMenu *menu) {
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

//! Slides the window's content, the menu among it, left in step with the edge the actions come in
//! behind, keeping the action menu's own layer in place
static void prv_slide_window(PillActionMenu *menu) {
  const int32_t e = prv_ease(menu->progress);
  const int16_t offset = (int16_t)(PBL_DISPLAY_WIDTH * e / 1000);
  Layer *root = window_get_root_layer(menu->window);
  GRect bounds = root->bounds;
  bounds.origin.x = -offset;
  layer_set_bounds(root, &bounds);
  GRect frame = menu->layer.frame;
  frame.origin.x = offset;
  layer_set_frame(&menu->layer, &frame);
}

static void prv_destroy(PillActionMenu *menu) {
  prv_unschedule(&menu->glide);
  prv_unschedule(&menu->motion);
  menu->progress = 0;
  prv_slide_window(menu);
  menu_layer_set_round_pill_hidden(menu->menu_layer, false);
  prv_touch_deregister(menu);
  layer_remove_from_parent(&menu->layer);
  layer_deinit(&menu->layer);
  // Subscribing to Back marked the window as handling it; without that the window pops on Back
  window_set_overrides_back_button(menu->window, menu->window_overrides_back);
  window_set_click_config_provider_with_context(menu->window, menu->window_click_config,
                                                menu->window_click_context);
  gdraw_command_image_destroy(menu->scaled_icon);
  gdraw_command_image_destroy(menu->icon);
  ActionMenu *action_menu = menu->action_menu;
  applib_free(menu);
  action_menu_hosted_did_close(action_menu);
}

static void prv_motion_update(Animation *animation, const AnimationProgress p) {
  PillActionMenu *menu = animation_get_context(animation);
  menu->progress = menu->motion_from +
                   (menu->motion_to - menu->motion_from) * (int32_t)p / ANIMATION_NORMALIZED_MAX;
  prv_slide_window(menu);
  layer_mark_dirty(&menu->layer);
}

static void prv_motion_stopped(Animation *animation, bool finished, void *context) {
  PillActionMenu *menu = context;
  if (animation != menu->motion) {
    return;
  }
  menu->motion = nullptr;
  menu->progress = menu->motion_to;
  prv_slide_window(menu);
  if (menu->closing) {
    prv_destroy(menu);
  } else {
    layer_mark_dirty(&menu->layer);
  }
}

static void prv_run_motion(PillActionMenu *menu, int32_t to) {
  prv_unschedule(&menu->motion);
  static const AnimationImplementation s_implementation = {
    .update = prv_motion_update,
  };
  menu->motion_from = menu->progress;
  menu->motion_to = to;
  const int32_t distance = PBL_ABS(to - menu->progress);
  Animation *motion = animation_create();
  if (motion) {
    menu->motion = motion;
    animation_set_implementation(motion, &s_implementation);
    animation_set_duration(motion, (uint32_t)(OPEN_MS * distance / 1000) + 1);
    animation_set_curve(motion, AnimationCurveLinear);
    animation_set_handlers(motion, (AnimationHandlers){.stopped = prv_motion_stopped}, menu);
    if (animation_schedule(motion)) {
      return;
    }
    menu->motion = nullptr;
    animation_destroy(motion);
  }
  // No animation: jump to the end
  menu->progress = to;
  prv_slide_window(menu);
  if (menu->closing) {
    prv_destroy(menu);
  }
}

static void prv_glide_update(Animation *animation, const AnimationProgress p) {
  PillActionMenu *menu = animation_get_context(animation);
  menu->list_pos = menu->glide_from +
                   (menu->glide_to - menu->glide_from) * (int32_t)p / ANIMATION_NORMALIZED_MAX;
  layer_mark_dirty(&menu->layer);
}

static void prv_glide_stopped(Animation *animation, bool finished, void *context) {
  PillActionMenu *menu = context;
  if (animation == menu->glide) {
    menu->glide = nullptr;
    menu->list_pos = menu->glide_to;
  }
}

//! Tells the level shown which of its items is selected, nullptr as it is left, as an ActionMenu
//! does
static void prv_notify_level(PillActionMenu *menu, const ActionMenuItem *item) {
  const ActionMenuLevel *level = prv_level(menu);
  if (level->selection_changed) {
    level->selection_changed(item, action_menu_get_context(menu->action_menu));
  }
}

//! Glides the rows to show the selection, and lets the level know it changed
static void prv_selection_changed(PillActionMenu *menu, bool animated) {
  prv_notify_level(menu, &prv_level(menu)->items[prv_selected(menu)]);
  const int32_t target =
      menu_round_list_scroll(prv_selected(menu), prv_count(menu), ROW_HEIGHT, EDGE_ROWS);
  prv_unschedule(&menu->glide);
  layer_mark_dirty(&menu->layer);
  if (animated && (target != menu->list_pos)) {
    static const AnimationImplementation s_implementation = {
      .update = prv_glide_update,
    };
    Animation *glide = animation_create();
    if (glide) {
      menu->glide = glide;
      menu->glide_from = menu->list_pos;
      menu->glide_to = target;
      animation_set_implementation(glide, &s_implementation);
      animation_set_duration(glide, GLIDE_MS);
      animation_set_curve(glide, AnimationCurveEaseOut);
      animation_set_handlers(glide, (AnimationHandlers){.stopped = prv_glide_stopped}, menu);
      if (animation_schedule(glide)) {
        return;
      }
      menu->glide = nullptr;
      animation_destroy(glide);
    }
  }
  menu->list_pos = target;
}

// ---------------------------------------------------------------------------------------------
// Navigation

static void prv_move(PillActionMenu *menu, int16_t rows) {
  uint16_t *selected = &menu->selected[menu->depth - 1];
  const int32_t target = (int32_t)*selected + rows;
  if (((target < 0) || (target >= prv_count(menu))) &&
      (shell_prefs_get_menu_scroll_vibe_behavior() == MenuScrollVibeOnLocked)) {
    // Already at the end: a pulse, as menus give there if asked to
    menu_layer_play_scroll_vibe();
  }
  const uint16_t clamped = (uint16_t)PBL_CLAMP(target, 0, (int32_t)prv_count(menu) - 1);
  if (clamped != *selected) {
    *selected = clamped;
    prv_selection_changed(menu, true);
  }
}

static void prv_enter_level(PillActionMenu *menu, const ActionMenuLevel *level) {
  if ((menu->depth >= MAX_DEPTH) || (level->num_items == 0)) {
    return;
  }
  prv_notify_level(menu, nullptr);
  menu->levels[menu->depth] = level;
  menu->selected[menu->depth] =
      (uint16_t)PBL_MIN(level->default_selected_item, (unsigned)level->num_items - 1);
  menu->depth++;
  prv_selection_changed(menu, false);
}

static void prv_activate(PillActionMenu *menu, uint16_t index) {
  const ActionMenuLevel *level = prv_level(menu);
  if (index >= level->num_items) {
    return;
  }
  menu->selected[menu->depth - 1] = index;
  prv_selection_changed(menu, true);
  const ActionMenuItem *item = &level->items[index];
  if (item->is_leaf) {
    action_menu_hosted_perform(menu->action_menu, item);
  } else if (item->next_level) {
    // Submenus replace the rows in place
    prv_enter_level(menu, item->next_level);
  }
}

static void prv_close(PillActionMenu *menu) {
  action_menu_close(menu->action_menu, true /* animated */);
}

static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_accepts_input(context)) {
    prv_move(context, -1);
  }
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_accepts_input(context)) {
    prv_move(context, 1);
  }
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_accepts_input(context)) {
    prv_activate(context, prv_selected(context));
  }
}

static void prv_back_click(ClickRecognizerRef recognizer, void *context) {
  PillActionMenu *menu = context;
  if (!prv_accepts_input(menu)) {
    return;
  }
  if (menu->depth > 1) {
    prv_notify_level(menu, nullptr);
    menu->depth--;
    prv_selection_changed(menu, false);
  } else {
    prv_close(menu);
  }
}

static void prv_click_config_provider(void *context) {
  // Held, they keep moving, as in an ActionMenu
  window_single_repeating_click_subscribe(BUTTON_ID_UP, CLICK_REPEAT_MS, prv_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, CLICK_REPEAT_MS, prv_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);
}

// ---------------------------------------------------------------------------------------------
// Open / close

//! The action menu is closing: play the opening backwards, into the pill of the row the menu has
//! selected now, which may have changed with what the action did
static void prv_host_close(ActionMenu *action_menu, bool animated, void *context) {
  PillActionMenu *menu = context;
  menu->closing = true;
  if (!animated) {
    prv_destroy(menu);
    return;
  }
  prv_capture_row(menu);
  prv_run_motion(menu, 0);
}

static const ActionMenuHost s_host = {
  .close = prv_host_close,
};

ActionMenu *pill_action_menu_open(const PillActionMenuConfig *config,
                                  ActionMenuConfig *menu_config) {
  const ActionMenuLevel *root = menu_config->root_level;
  Window *window = config->window;
  if (!root || (root->num_items == 0) || !window) {
    return nullptr;
  }
  PillActionMenu *menu = applib_zalloc(sizeof(PillActionMenu));
  if (!menu) {
    return nullptr;
  }
  ActionMenu *action_menu =
      action_menu_open_hosted(window->parent_window_stack, menu_config, &s_host, menu);
  if (!action_menu) {
    applib_free(menu);
    return nullptr;
  }
  MenuLayer *menu_layer = config->menu_layer;
  *menu = (PillActionMenu){
    .action_menu = action_menu,
    .window = window,
    .menu_layer = menu_layer,
    .open = true,
    .color = menu_layer->highlight_colors[MenuLayerColorBackground],
    .text_color = menu_layer->highlight_colors[MenuLayerColorForeground],
    .background = menu_layer->normal_colors[MenuLayerColorBackground],
    .foreground = menu_layer->normal_colors[MenuLayerColorForeground],
    .window_click_config = window->click_config_provider,
    .window_click_context = window->click_config_context,
    .window_overrides_back = window->overrides_back_button,
  };
  menu->icon = gdraw_command_image_create_with_resource(config->icon_resource_id);
  if (menu->icon) {
    menu->icon_size = gdraw_command_image_get_data_size(menu->icon);
    menu->scaled_icon = gdraw_command_image_clone(menu->icon);
  }
  menu->levels[0] = root;
  menu->selected[0] = (uint16_t)PBL_MIN(root->default_selected_item, (unsigned)root->num_items - 1);
  menu->depth = 1;

  prv_capture_row(menu);
  // The menu's own pill would show under the one that morphs
  menu_layer_set_round_pill_hidden(menu_layer, true);

  Layer *root_layer = window_get_root_layer(window);
  layer_init(&menu->layer, &GRect(0, 0, root_layer->frame.size.w, root_layer->frame.size.h));
  layer_set_update_proc(&menu->layer, prv_update_proc);
  layer_add_child(root_layer, &menu->layer);
  prv_selection_changed(menu, false /* animated */);
  window_set_click_config_provider_with_context(window, prv_click_config_provider, menu);
  prv_touch_register(menu);
  prv_run_motion(menu, 1000);
  return action_menu;
}

#endif // PBL_ROUND
