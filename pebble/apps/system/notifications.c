/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "notifications.h"

#include "notifications_history.h"
#include "notifications_round_path.h"
#include "notifications_shade.h"

#include <stdio.h>
#include <time.h>

#include <pbl/i18n/i18n.h>
#include <pbl/kernel/fatal.h>
#include <pbl/kernel/time.h>
#include <pbl/pp/session.h>
#include <pbl/services/blob_db/pin_db.h>
#include <pbl/services/clock.h>
#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/notifications/notification_storage.h>
#include <pbl/services/notifications/notification_style.h>
#include <pbl/services/notifications/notification_sync.h>
#include <pbl/services/time.h>
#include <pbl/services/timeline/notification_layout.h>
#include <pbl/services/timeline/timeline_actions.h>
#include <pbl/util/list.h>
#include <pbl/util/size.h>
#include <pbl/util/string.h>
#include <pbl/util/units.h>

#include <applib/app.h>
#include <applib/app_exit_reason.h>
#include <applib/app_malloc.h>
#include <applib/fonts/fonts.h>
#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/gdraw_command_list.h>
#include <applib/preferred_content_size.h>
#include <applib/ui/app_window_stack.h>
#include <applib/ui/dialogs/actionable_dialog.h>
#include <applib/ui/dialogs/simple_dialog.h>
#include <applib/ui/menu_cell_layer.h>
#include <applib/ui/menu_layer_private.h>
#include <applib/ui/ui.h>
#include <comm/ble/gap_le_connect.h>
#include <kernel/ui/system_icons.h>
#include <popups/notifications/notification_window.h>
#include <process_state/app_state/app_state.h>
#include <resource/resource_ids.auto.h>
#include <shell/prefs.h>
#include <shell/system_theme.h>

typedef struct LoadedNotificationNode {
  ListNode node;
  TimelineItem notification;
  GDrawCommandImage *icon;
  bool icon_is_default;
} LoadedNotificationNode;

typedef struct NotificationsData {
  Window window;
  MenuLayer menu_layer;
  TextLayer text_layer;
  NotificationShade shade;
  LoadedNotificationNode *loaded_notification_list;
  EventServiceInfo notification_event_info;
  ActionableDialog *actionable_dialog;
  //! Opening or closing a header or stack: its rows grow or shrink in place
  Animation *fold_animation;
  int16_t fold_first;
  int16_t fold_count;
  //! How many of the folding rows move; those past them are off screen once open, so they stay
  //! closed until the rows open and close first, and are not loaded for each frame
  int16_t fold_moving;
  int16_t fold_header;
  bool fold_opening;
  //! How open the folding rows are, in thousandths; overshoots a little past FOLD_FULL
  int32_t fold_amount;
} NotificationsData;

static NotificationsData *s_data = nullptr;

// Rows on screen at once, plus a few for the rows sliding in.
static const unsigned int MAX_ACTIVE_NOTIFICATIONS = 10;

#define FOLD_OPEN_MS  280
#define FOLD_CLOSE_MS 220
#define FOLD_FULL     1000

#define HEADER_INDENT_STEP PBL_IF_ROUND_ELSE(14, 10)
#define STACK_LINE_EDGE    5
#define STACK_LINE_WIDTH   2
#define STACK_LINE_GAP     4
//! Space between the stack line and where the selection highlight starts
#define STACK_LINE_HIGHLIGHT_GAP PBL_IF_ROUND_ELSE(4, 2)

static bool prv_loaded_notification_list_filter_cb(ListNode *node, void *data) {
  LoadedNotificationNode *loaded_notification = (LoadedNotificationNode *)node;
  Uuid *id = data;
  return uuid_equal(&loaded_notification->notification.header.id, id);
}

static LoadedNotificationNode *prv_find_loaded_notification(LoadedNotificationNode *list,
                                                            Uuid *id) {
  return (LoadedNotificationNode *)list_find((ListNode *)list,
                                             prv_loaded_notification_list_filter_cb, id);
}

///////////////////
// Loading

static void prv_unload_loaded_notification(LoadedNotificationNode *loaded_notif) {
  timeline_item_free_allocated_buffer(&loaded_notif->notification);
  gdraw_command_image_destroy(loaded_notif->icon);
  app_free(loaded_notif);
}

static PBL_NOINLINE LoadedNotificationNode *prv_loaded_notification_list_load_item(
    LoadedNotificationNode **loaded_list, const Uuid *id) {
  if (id == nullptr) {
    return nullptr;
  }

  LoadedNotificationNode *loaded_node = prv_find_loaded_notification(*loaded_list, (Uuid *)id);
  if (loaded_node) {
    return loaded_node;
  }

  // unload old notifications
  if (list_count((ListNode *)*loaded_list) >= MAX_ACTIVE_NOTIFICATIONS) {
    LoadedNotificationNode *old_node =
        (LoadedNotificationNode *)list_get_tail((ListNode *)*loaded_list);
    list_remove((ListNode *)old_node, (ListNode **)loaded_list, nullptr);
    prv_unload_loaded_notification(old_node);
  }

  // load the notification
  TimelineItem notification;
  if (!notification_storage_get((Uuid *)id, &notification)) {
    return nullptr;
  }

  // track the loaded notification
  loaded_node = app_malloc_check(sizeof(LoadedNotificationNode));

  list_init((ListNode *)loaded_node);
  loaded_node->notification = notification;

  TimelineResourceId timeline_res_id =
      attribute_get_uint32(&notification.attr_list, AttributeIdIconTiny, NOTIF_FALLBACK_ICON);

  // Read the associated pin's app id
  TimelineItem pin;
  if (timeline_resources_is_system(timeline_res_id) ||
      pin_db_read_item_header(&pin, &notification.header.parent_id) != 0) {
    pin.header.parent_id = (Uuid)UUID_INVALID;
  }

  TimelineResourceInfo timeline_res = {
    .res_id = timeline_res_id,
    .app_id = &pin.header.parent_id,
    .fallback_id = NOTIF_FALLBACK_ICON
  };
  AppResourceInfo icon_res_info;
  timeline_resources_get_id(&timeline_res, TimelineResourceSizeTiny, &icon_res_info);
  loaded_node->icon = gdraw_command_image_create_with_resource_system(icon_res_info.res_app_num,
                                                                      icon_res_info.res_id);
  loaded_node->icon_is_default = (timeline_res_id == NOTIF_FALLBACK_ICON) ||
                                 (timeline_res_id == TIMELINE_RESOURCE_NOTIFICATION_GENERIC);

  *loaded_list =
      (LoadedNotificationNode *)list_prepend((ListNode *)*loaded_list, (ListNode *)loaded_node);

  return loaded_node;
}

static void prv_loaded_notification_list_deinit(LoadedNotificationNode *loaded_list) {
  while (loaded_list) {
    LoadedNotificationNode *node = loaded_list;
    loaded_list = (LoadedNotificationNode *)list_pop_head((ListNode *)loaded_list);
    prv_unload_loaded_notification(node);
  }
}

static LoadedNotificationNode *prv_load_row_item(NotificationsData *data,
                                                 const NotificationShadeRow *row) {
  if (!row || (row->kind == NotificationShadeRowHeader)) {
    return nullptr;
  }
  const NotificationShadeEntry *entry = notification_shade_item(&data->shade, row->item);
  return entry ? prv_loaded_notification_list_load_item(&data->loaded_notification_list, &entry->id)
               : nullptr;
}

// Return true if successful
static bool prv_push_notification_window(NotificationsData *data, const Uuid *focus_id) {
  notification_window_init_history();

  // Bail if a notification came in ahead of us and created a modal window
  // before we had a chance to react to the select button event.
  if (notification_window_is_modal()) {
    return false;
  }

  // The presented list prepends, so add the cards last first to page through them in list order.
  for (int i = notification_shade_count(&data->shade) - 1; i >= 0; i--) {
    notification_window_add_notification_by_id(
        (Uuid *)&notification_shade_item(&data->shade, i)->id);
  }

  notification_window_show();
  notification_window_focus_notification((Uuid *)focus_id, false /* animated */);
  return true;
}

///////////////////
// Confirm Dialog

static void prv_dialog_unloaded(void *context) {
  NotificationsData *data = context;
  data->actionable_dialog = nullptr;
}

// Whether a dismissal can reach the phone: over the Pebble protocol, or ANCS on an LE link
static bool prv_phone_is_connected(void) {
  return (pbl_pp_session_get_system() != nullptr) || gap_le_connect_is_connected_as_slave();
}

static void prv_confirmed_handler(ClickRecognizerRef recognizer, void *context) {
  NotificationsData *data = context;
  // The phone resends the notifications it still has, so dismiss them there too
  timeline_actions_dismiss_all_stored();
  actionable_dialog_pop(data->actionable_dialog);

  // Create and display DONE dialog
  SimpleDialog *confirmation_dialog = simple_dialog_create("Notifications Cleared");
  Dialog *dialog = simple_dialog_get_dialog(confirmation_dialog);
  dialog_set_text(dialog, pbl_i18n_get("Done", data));
  dialog_set_icon(dialog, RESOURCE_ID_RESULT_SHREDDED_LARGE);
  static const uint32_t DIALOG_TIMEOUT = 2000;
  dialog_set_timeout(dialog, DIALOG_TIMEOUT);

  // Set the app exit reason so we will go to the watchface upon exit
  app_exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);

  // Pop all windows so we'll soon exit the app
  app_window_stack_pop_all(true /* animated */);

  // Immediately push this result dialog so it's the last thing we see before exiting
  app_simple_dialog_push(confirmation_dialog);
}

static void prv_dialog_click_config(void *context) {
  NotificationsData *data = app_state_get_user_data();
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_confirmed_handler);
  window_set_click_context(BUTTON_ID_SELECT, data);
}

static void prv_settings_clear_history_window_push(NotificationsData *data) {
  ActionableDialog *actionable_dialog = actionable_dialog_create("Clear Notifications");
  actionable_dialog_set_click_config_provider(actionable_dialog, prv_dialog_click_config);
  actionable_dialog_set_action_bar_type(actionable_dialog, DialogActionBarConfirm, nullptr);
  Dialog *dialog = actionable_dialog_get_dialog(actionable_dialog);
  dialog_set_text(dialog,
                  prv_phone_is_connected()
                      /// Asked before dismissing every notification on the phone and the watch
                      ? pbl_i18n_get("Dismiss all notifications on your phone and watch?", data)
                      /// Asked before clearing the watch's notifications while the phone is away
                      : pbl_i18n_get("Phone not connected. Clear the watch only?", data));
  TimelineResourceInfo timeline_res = {
    .res_id = TIMELINE_RESOURCE_GENERIC_QUESTION,
  };
  AppResourceInfo icon_res_info;
  timeline_resources_get_id(&timeline_res, TimelineResourceSizeLarge, &icon_res_info);
  dialog_set_icon(dialog, icon_res_info.res_id);
  dialog_set_icon_animate_direction(dialog, DialogIconAnimationFromRight);
  dialog_set_callbacks(dialog,
                       &(DialogCallbacks){
                         .unload = prv_dialog_unloaded,
                       },
                       data);
  app_actionable_dialog_push(actionable_dialog);
  data->actionable_dialog = actionable_dialog;
}

static GColor prv_invert_bw_color(GColor color) {
  if (gcolor_equal(color, GColorBlack)) {
    return GColorWhite;
  } else if (gcolor_equal(color, GColorWhite)) {
    return GColorBlack;
  }
  return color;
}

static void prv_invert_pdc_colors(GDrawCommandProcessor *processor, GDrawCommand *processed_command,
                                  size_t processed_command_max_size, const GDrawCommandList *list,
                                  const GDrawCommand *command) {
  gdraw_command_set_stroke_color(
      processed_command,
      prv_invert_bw_color(gdraw_command_get_stroke_color((GDrawCommand *)command)));
  gdraw_command_set_fill_color(
      processed_command,
      prv_invert_bw_color(gdraw_command_get_fill_color((GDrawCommand *)command)));
}

static void prv_draw_pdc_bw_inverted(GContext *ctx, GDrawCommandImage *image, GPoint offset) {
  GDrawCommandProcessor processor = {
    .command = prv_invert_pdc_colors,
  };
  gdraw_command_image_draw_processed(ctx, image, offset, &processor);
}

static void prv_draw_notification_icon(GContext *ctx, const Layer *cell_layer,
                                       GDrawCommandImage *icon, GPoint origin) {
  void (*draw_func)(GContext *, GDrawCommandImage *, GPoint) = gdraw_command_image_draw;
  if (system_theme_is_dark_mode()) {
    if (!menu_cell_layer_is_highlighted(cell_layer)) {
      draw_func = prv_draw_pdc_bw_inverted;
    }
  }
#if PBL_BW
  else {
    if (menu_cell_layer_is_highlighted(cell_layer)) {
      draw_func = prv_draw_pdc_bw_inverted;
    }
  }
#endif
  draw_func(ctx, icon, origin);
}

///////////////////
// Row geometry

static int16_t prv_cell_height(void) {
  return notification_style_scaled_points(24) + notification_style_scaled_points(14) + 8;
}

static int16_t prv_header_height(void) {
  return PBL_IF_ROUND_ELSE(12, 8) + notification_style_scaled_points(14);
}

static bool prv_row_is_header(const NotificationShadeRow *row) {
  return row && ((row->kind == NotificationShadeRowHeader) ||
                 ((row->kind == NotificationShadeRowStack) && row->expanded));
}

static int16_t prv_full_row_height(const NotificationShadeRow *row) {
  return prv_row_is_header(row) ? prv_header_height() : prv_cell_height();
}

//! The last row, past every notification, dismisses them all after asking.
static bool prv_is_dismiss_all_row(const NotificationsData *data, int16_t index) {
  const uint16_t count = notification_shade_row_count(&data->shade);
  return (count > 0) && (index == count);
}

static int16_t prv_dismiss_all_row_height(void) {
  return notification_style_scaled_points(24) + 16;
}

static uint16_t prv_row_count(const NotificationsData *data) {
  const uint16_t count = notification_shade_row_count(&data->shade);
  return (count > 0) ? (count + 1) : 0;
}

static bool prv_row_is_folding(const NotificationsData *data, int16_t index) {
  return (data->fold_first >= 0) && (index >= data->fold_first) &&
         (index < data->fold_first + data->fold_count);
}

static int16_t prv_row_height_at(const NotificationsData *data, int16_t index) {
  if (prv_is_dismiss_all_row(data, index)) {
    return prv_dismiss_all_row_height();
  }
  const int16_t full = prv_full_row_height(notification_shade_row(&data->shade, index));
  if (prv_row_is_folding(data, index)) {
    if (index >= data->fold_first + data->fold_moving) {
      return 0;
    }
    const int32_t height = full * data->fold_amount / FOLD_FULL;
    return (height > 0) ? height : 0;
  }
  return full;
}

static int16_t prv_header_depth(const NotificationShadeRow *row) {
  return ((row->group >= NotificationShadeGroupSuggested) ? 1 : 0) +
         ((row->kind == NotificationShadeRowStack) ? 1 : 0);
}

#if PBL_ROUND
//! Distance from the left edge to the rim for a row at this height on screen, at its end nearer
//! the rim. Near the top and bottom the path eases off the rim and goes on past the screen's
//! edge, so a row leaving the screen travels along it until it is gone.
static int16_t prv_rim_for_screen_row(int16_t screen_top, int16_t height) {
  int16_t dy = screen_top + height / 2 - PBL_DISPLAY_HEIGHT / 2;
  dy += (dy > 0) ? height / 2 : -height / 2;
  return notifications_round_path_x(PBL_DISPLAY_WIDTH / 2, dy);
}

//! A round row is laid out with its inset from the rim, at most this much: nearer the ends the
//! laid-out row moves on along the rim as a whole instead of narrowing further.
#define ROUND_MAX_INSET 60

static int16_t prv_round_inset(int16_t rim) {
  return (rim < 12) ? 12 : ((rim > ROUND_MAX_INSET) ? ROUND_MAX_INSET : rim);
}

static int16_t prv_round_shift(int16_t rim) {
  return (rim > ROUND_MAX_INSET) ? (rim - ROUND_MAX_INSET) : 0;
}

//! Rows slide along the rim as the list scrolls, so a header always starts at least one step
//! right of its parent header, wherever each of them is on screen.
static int16_t prv_round_header_left(const NotificationsData *data, int16_t index,
                                     int16_t screen_top) {
  const NotificationShadeRow *row = notification_shade_row(&data->shade, index);
  const int16_t rim = prv_rim_for_screen_row(screen_top, prv_row_height_at(data, index)) + 4;
  const int16_t left = prv_round_inset(rim) + prv_round_shift(rim);
  const int16_t depth = prv_header_depth(row);
  if (depth == 0) {
    return left;
  }
  int16_t y = screen_top;
  for (int16_t i = index - 1; i >= 0; i--) {
    y -= prv_row_height_at(data, i);
    const NotificationShadeRow *above = notification_shade_row(&data->shade, i);
    if (prv_row_is_header(above) && (prv_header_depth(above) < depth)) {
      const int16_t parent = prv_round_header_left(data, i, y) + HEADER_INDENT_STEP;
      return (parent > left) ? parent : left;
    }
  }
  return left + depth * HEADER_INDENT_STEP;
}

static int16_t prv_round_rim_at_row(const Layer *cell_layer, GRect bounds) {
  const GPoint on_screen = layer_convert_point_to_screen(cell_layer, bounds.origin);
  return prv_rim_for_screen_row(on_screen.y, bounds.size.h);
}

//! An open stack's arc keeps this far in from the rim, eased off it near the ends like the rows.
static int16_t prv_stack_arc_x(int16_t inset, int16_t dy) {
  const int16_t radius = PBL_DISPLAY_WIDTH / 2;
  return inset + notifications_round_path_x(radius - inset, dy);
}

//! Where an open stack's arc ends beside this row's icon (centred, about 26 px tall).
static int16_t prv_stack_arc_clearance(const Layer *cell_layer, GRect bounds) {
  const GPoint on_screen = layer_convert_point_to_screen(cell_layer, bounds.origin);
  int16_t dy = on_screen.y + bounds.size.h / 2 - PBL_DISPLAY_HEIGHT / 2;
  dy += (dy > 0) ? 13 : -13;
  return prv_stack_arc_x(STACK_LINE_EDGE + STACK_LINE_WIDTH, dy) + STACK_LINE_GAP;
}

// Round lists slide like rectangular ones and stop the first and last rows one row from the
// middle instead of leaving half the screen empty: the space above is section 0's header, the
// space below content past the last row.
#define ROUND_MIN_TOP_SPACE 12

static int16_t prv_round_end_space(int16_t end_row_height) {
  const int16_t space = PBL_DISPLAY_HEIGHT / 2 - end_row_height / 2 - prv_cell_height();
  return (space > 0) ? space : 0;
}

static int16_t prv_round_space_above(const NotificationsData *data) {
  if (notification_shade_row_count(&data->shade) == 0) {
    return 0;
  }
  const int16_t space = prv_round_end_space(prv_row_height_at(data, 0)) - prv_cell_height();
  return (space > ROUND_MIN_TOP_SPACE) ? space : ROUND_MIN_TOP_SPACE;
}

static int16_t prv_round_space_below(const NotificationsData *data) {
  const uint16_t count = prv_row_count(data);
  return count ? prv_round_end_space(prv_row_height_at(data, count - 1)) : 0;
}
#endif

///////////////////
// Drawing

//! A small triangle, pointing down when open and right when closed, filled line by line.
static void prv_draw_caret(GContext *ctx, GPoint center, bool open) {
  for (int16_t i = 0; i <= 4; i++) {
    if (open) {
      graphics_fill_rect(ctx, &GRect(center.x - 4 + i, center.y - 2 + i, 9 - 2 * i, 1));
    } else {
      graphics_fill_rect(ctx, &GRect(center.x - 2 + i, center.y - 4 + i, 1, 9 - 2 * i));
    }
  }
}

static GColor prv_highlight_text_color(void) {
  return PBL_IF_COLOR_ELSE(GColorWhite, notification_style_bg());
}

static const char *prv_app_name(const TimelineItem *notification) {
  const char *app_name = attribute_get_string(&notification->attr_list, AttributeIdAppName, "");
  if (PBL_IS_EMPTY_STRING(app_name)) {
    app_name = attribute_get_string(&notification->attr_list, AttributeIdTitle, "");
  }
  return app_name;
}

//! Section, category or open stack: a small caption with a caret, and the count when closed.
static void prv_draw_header_row(GContext *ctx, const Layer *cell_layer, GRect bounds,
                                const NotificationShadeRow *row, int16_t row_index) {
  NotificationsData *data = s_data;
  const bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  const GColor color = highlighted ? prv_highlight_text_color() : notification_style_secondary();
  graphics_context_set_text_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);

  const char *name;
  if (row->kind == NotificationShadeRowStack) {
    LoadedNotificationNode *loaded = prv_load_row_item(data, row);
    name = loaded ? prv_app_name(&loaded->notification) : "";
  } else {
    name = pbl_i18n_get(notification_shade_group_name(row->group), data);
  }
  char text[ATTRIBUTE_TITLE_MAX_LEN + 8];
  if (row->expanded && (row->kind == NotificationShadeRowHeader)) {
    snprintf(text, sizeof(text), "%s", name);
  } else {
    snprintf(text, sizeof(text), "%s  %u", name, (unsigned int)row->count);
  }

#if PBL_ROUND
  const int16_t left = prv_round_header_left(
      data, row_index, layer_convert_point_to_screen(cell_layer, bounds.origin).y);
  // Moving on along the rim keeps the caption's width.
  const int16_t width = bounds.size.w - PBL_MIN(left, ROUND_MAX_INSET) - 20;
#else
  const int16_t left = 8 + prv_header_depth(row) * HEADER_INDENT_STEP;
  const int16_t width = bounds.size.w - left - 20;
#endif
  const int16_t font_height = notification_style_scaled_points(14);
  const int16_t text_top = bounds.origin.y + (bounds.size.h - font_height - 4) / 2;
  prv_draw_caret(ctx, GPoint(left + 4, bounds.origin.y + bounds.size.h / 2), row->expanded);
  graphics_draw_text(ctx, text, notification_style_scaled_font(14, true),
                     GRect(left + 14, text_top, width, font_height + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, nullptr);
}

//! A line down the left of an open stack ties its notifications together. On round watches it
//! is an arc along the rim.
static void prv_draw_stack_line(GContext *ctx, const Layer *cell_layer, GRect bounds,
                                int16_t row_index) {
  const NotificationShadeRow *above = notification_shade_row(&s_data->shade, row_index - 1);
  const NotificationShadeRow *below = notification_shade_row(&s_data->shade, row_index + 1);
  const int16_t top = bounds.origin.y + ((above && above->in_stack) ? 0 : STACK_LINE_GAP);
  const int16_t bottom =
      bounds.origin.y + bounds.size.h - ((below && below->in_stack) ? 0 : STACK_LINE_GAP);
#if PBL_ROUND
#if PBL_COLOR
  graphics_context_set_fill_color(ctx, notification_style_secondary());
#else
  graphics_context_set_fill_color(ctx, menu_cell_layer_is_highlighted(cell_layer)
                                           ? notification_style_bg()
                                           : notification_style_fg());
#endif
  // A ring concentric with the screen in the middle, filled one pixel row at a time.
  const int16_t screen_y = layer_convert_point_to_screen(cell_layer, GPointZero).y;
  for (int16_t y = top; y < bottom; y++) {
    const int16_t dy = screen_y + y - PBL_DISPLAY_HEIGHT / 2;
    const int16_t from = prv_stack_arc_x(STACK_LINE_EDGE, dy);
    int16_t to = prv_stack_arc_x(STACK_LINE_EDGE + STACK_LINE_WIDTH, dy);
    if (to <= from) {
      to = from + 1;
    }
    graphics_fill_rect(ctx, &GRect(from, y, to - from, 1));
  }
#else
  graphics_context_set_fill_color(ctx, notification_style_secondary());
  graphics_fill_rect(ctx, &GRect(STACK_LINE_EDGE, top, STACK_LINE_WIDTH, bottom - top));
#endif
}

#if PBL_RECT
//! The menu slides its highlight by redrawing every row it touches as highlighted. When the
//! selection is inside an open stack, trim every highlighted row the same way so the moving bar
//! stays one shape and never peeks out beside the stack line.
static void prv_trim_highlight_strip(GContext *ctx, const Layer *cell_layer) {
  if (!menu_cell_layer_is_highlighted(cell_layer)) {
    return;
  }
  const NotificationShadeRow *selected = notification_shade_row(
      &s_data->shade, menu_layer_get_selected_index(&s_data->menu_layer).row);
  if (!selected || !selected->in_stack) {
    return;
  }
  const GRect bounds = cell_layer->bounds;
  graphics_context_set_fill_color(ctx, notification_style_bg());
  graphics_fill_rect(
      ctx,
      &GRect(0, 0, STACK_LINE_EDGE + STACK_LINE_WIDTH + STACK_LINE_HIGHLIGHT_GAP, bounds.size.h));
}
#endif

static void prv_draw_item_row(GContext *ctx, const Layer *cell_layer, GRect bounds,
                              const NotificationShadeRow *row, int16_t row_index) {
  LoadedNotificationNode *loaded_node = prv_load_row_item(s_data, row);
  if (!loaded_node) {
    return;
  }

#if PBL_ROUND
  // Keep the icon and text clear of the rim, and inside an open stack of its arc: rows away from
  // the centre start further in, and near the ends move on along the rim.
  int16_t left = prv_round_rim_at_row(cell_layer, bounds) + 4;
  if (row->in_stack) {
    left = PBL_MAX(left + 7, prv_stack_arc_clearance(cell_layer, bounds));
  }
  const int16_t inset = prv_round_inset(left);
  bounds.origin.x += prv_round_shift(left);
#else
  const int16_t inset = row->in_stack ? 12 : 5;
#endif

  if (row->in_stack) {
    prv_draw_stack_line(ctx, cell_layer, bounds, row_index);
  }

  GSize icon_size = GSizeZero;
  if (loaded_node->icon) {
    icon_size = gdraw_command_image_get_bounds_size(loaded_node->icon);
    GRect box = bounds;
    box.origin.x += inset;
    GRect icon_rect = (GRect){.size = icon_size};
    grect_align(&icon_rect, &box, GAlignLeft, false /* clip */);
    prv_draw_notification_icon(ctx, cell_layer, loaded_node->icon, icon_rect.origin);
  }

  const TimelineItem *notification = &loaded_node->notification;
  const char *title = attribute_get_string(&notification->attr_list, AttributeIdTitle, "");
  const char *app_name = attribute_get_string(&notification->attr_list, AttributeIdAppName, "");
  // Notifications with the generic icon are titled with their app, except inside a stack, whose
  // header already names the app: there they are titled with who they are from.
  if (loaded_node->icon_is_default && !PBL_IS_EMPTY_STRING(app_name) &&
      !(row->in_stack && !PBL_IS_EMPTY_STRING(title))) {
    title = app_name;
  }
  const char *body = attribute_get_string(&notification->attr_list, AttributeIdBody, "");
  if (PBL_IS_EMPTY_STRING(body)) {
    body = attribute_get_string(&notification->attr_list, AttributeIdSubtitle, "");
  }
  const char *header = attribute_get_string(&notification->attr_list, AttributeIdTitle, "");
  if (PBL_IS_EMPTY_STRING(header)) {
    header = app_name;
  }
  const char *subtitle = notification_style_body_text(header, body);
  if (PBL_IS_EMPTY_STRING(title)) {
    title = !PBL_IS_EMPTY_STRING(subtitle) ? subtitle : pbl_i18n_get("[Empty]", s_data);
    subtitle = nullptr;
  }

  const int16_t icon_gap = 5;
  const int16_t text_left = inset + PBL_MAX(icon_size.w, ATTRIBUTE_ICON_TINY_SIZE_PX) + icon_gap;
  // A closed stack keeps room on the right for its count.
  const int16_t right_room =
      PBL_IF_ROUND_ELSE(inset, 5) + ((row->kind == NotificationShadeRowStack) ? 22 : 0);
  const GRect text_box = grect_inset(bounds, GEdgeInsets(0, right_room, 0, text_left));
  const int16_t title_height = notification_style_scaled_points(24);
  const int16_t subtitle_height = subtitle ? notification_style_scaled_points(14) : 0;
  const int16_t top =
      text_box.origin.y + (text_box.size.h - (title_height + subtitle_height + 10)) / 2;

  graphics_draw_text(ctx, title, notification_style_scaled_font(24, true),
                     GRect(text_box.origin.x, top, text_box.size.w, title_height + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, nullptr);
  if (subtitle) {
    graphics_draw_text(
        ctx, subtitle, notification_style_scaled_font(14, false),
        GRect(text_box.origin.x, top + title_height, text_box.size.w, subtitle_height + 4),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, nullptr);
  }

  if (row->kind == NotificationShadeRowStack) {
    // A closed stack: how many notifications it holds, with a caret to open it.
    char count[6];
    snprintf(count, sizeof(count), "%u", (unsigned int)row->count);
    const int16_t right = bounds.size.w - PBL_IF_ROUND_ELSE(inset, 5);
    const int16_t middle = bounds.origin.y + bounds.size.h / 2;
    graphics_draw_text(ctx, count, notification_style_scaled_font(14, true),
                       GRect(right - 26, middle - 10, 16, 18), GTextOverflowModeFill,
                       GTextAlignmentRight, nullptr);
    const bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
    graphics_context_set_fill_color(
        ctx, highlighted ? prv_highlight_text_color() : notification_style_fg());
    prv_draw_caret(ctx, GPoint(right - 4, middle), false);
  }
}

static void prv_draw_dismiss_all_row(GContext *ctx, const Layer *cell_layer) {
  const GRect bounds = cell_layer->bounds;
  const int16_t height = notification_style_scaled_points(24);
  graphics_context_set_text_color(ctx, menu_cell_layer_is_highlighted(cell_layer)
                                           ? prv_highlight_text_color()
                                           : notification_style_fg());
  graphics_draw_text(ctx, pbl_i18n_get("Dismiss All", s_data),
                     notification_style_scaled_font(24, true),
                     GRect(0, (bounds.size.h - height) / 2 - 4, bounds.size.w, height + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, nullptr);
}

static void prv_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                                  void *context) {
  NotificationsData *data = context;
  if (prv_is_dismiss_all_row(data, cell_index->row)) {
    prv_draw_dismiss_all_row(ctx, cell_layer);
    return;
  }
  const NotificationShadeRow *row = notification_shade_row(&data->shade, cell_index->row);
  if (!row) {
    return;
  }
#if PBL_RECT
  prv_trim_highlight_strip(ctx, cell_layer);
#endif
  // A row that is opening or closing is drawn at full size, sliding out from under the row above.
  GRect bounds = cell_layer->bounds;
  const int16_t full = prv_full_row_height(row);
  if (bounds.size.h < full) {
    bounds.origin.y = bounds.size.h - full;
    bounds.size.h = full;
  }
  if (prv_row_is_header(row)) {
    prv_draw_header_row(ctx, cell_layer, bounds, row, cell_index->row);
  } else {
    prv_draw_item_row(ctx, cell_layer, bounds, row, cell_index->row);
  }
}

///////////////////
// Opening and closing headers and stacks

//! Round: inside an open stack, the highlight pill starts inside the stack's arc.
static void prv_update_highlight_pill(NotificationsData *data) {
#if PBL_ROUND
  const NotificationShadeRow *selected =
      notification_shade_row(&data->shade, menu_layer_get_selected_index(&data->menu_layer).row);
  const bool in_stack = selected && selected->in_stack;
  menu_layer_set_round_pill_left_margin(
      &data->menu_layer, in_stack ? (STACK_LINE_EDGE + STACK_LINE_WIDTH + STACK_LINE_HIGHLIGHT_GAP -
                                     MENU_ROUND_PILL_BEZEL_MARGIN)
                                  : 0);
#endif
}

static void prv_reload_rows(NotificationsData *data) {
#if PBL_ROUND
  ScrollLayer *scroll = menu_layer_get_scroll_layer(&data->menu_layer);
  const GPoint offset = scroll_layer_get_content_offset(scroll);
#endif
  menu_layer_reload_data(&data->menu_layer);
#if PBL_ROUND
  // Rows alone end at the last one, which clamped a list resting in the space below.
  GSize size = scroll_layer_get_content_size(scroll);
  size.h += prv_round_space_below(data);
  scroll_layer_set_content_size(scroll, size);
  if (scroll_layer_get_content_offset(scroll).y != offset.y) {
    scroll_layer_set_content_offset(scroll, offset, false /* animated */);
  }
#endif
  prv_update_highlight_pill(data);
}

static void prv_save_collapsed_sections(NotificationsData *data) {
  alerts_preferences_set_notification_collapsed_sections(data->shade.collapsed_groups);
}

static void prv_fold_finish(NotificationsData *data, bool apply) {
  if (data->fold_first < 0) {
    return;
  }
  const bool closing = !data->fold_opening;
  const int16_t header = data->fold_header;
  data->fold_first = -1;
  data->fold_count = 0;
  data->fold_moving = 0;
  data->fold_amount = FOLD_FULL;
  Animation *running = data->fold_animation;
  data->fold_animation = nullptr;
  if (running) {
    animation_unschedule(running);
  }
  if (closing && apply) {
    notification_shade_toggle_row(&data->shade, header);
    prv_save_collapsed_sections(data);
  }
  prv_reload_rows(data);
}

//! After a header opens, scroll just enough to show what it revealed, keeping it on screen.
static void prv_reveal_opened_rows(NotificationsData *data, int16_t header, int16_t count) {
#if PBL_RECT
  ScrollLayer *scroll = menu_layer_get_scroll_layer(&data->menu_layer);
  const int16_t offset = -scroll_layer_get_content_offset(scroll).y;
  int16_t header_top = 0;
  for (int16_t i = 0; i < header; i++) {
    header_top += prv_row_height_at(data, i);
  }
  int16_t bottom = header_top + prv_row_height_at(data, header);
  for (int16_t i = 1; i <= count; i++) {
    bottom += prv_row_height_at(data, header + i);
  }
  const int16_t view = menu_layer_get_layer(&data->menu_layer)->bounds.size.h;
  if (bottom <= offset + view) {
    return;
  }
  int16_t target = bottom - view;
  if (target > header_top) {
    target = header_top;
  }
  if (target > offset) {
    scroll_layer_set_content_offset(scroll, GPoint(0, -target), true /* animated */);
  }
#endif
}

// Back easing: past the target by about a tenth, then back.
static int32_t prv_ease_out_back(int32_t t) {
  const int64_t u = t - FOLD_FULL;
  return FOLD_FULL + (int32_t)(2702 * u * u * u / 1000000000LL + 1702 * u * u / 1000000LL);
}

static int32_t prv_ease_in_back(int32_t t) {
  const int64_t v = t;
  return (int32_t)(2702 * v * v * v / 1000000000LL - 1702 * v * v / 1000000LL);
}

static void prv_fold_update(Animation *animation, const AnimationProgress progress) {
  NotificationsData *data = s_data;
  const int32_t t = (int32_t)progress * FOLD_FULL / ANIMATION_NORMALIZED_MAX;
  data->fold_amount = data->fold_opening ? prv_ease_out_back(t) : FOLD_FULL - prv_ease_in_back(t);
  prv_reload_rows(data);
}

static void prv_fold_stopped(Animation *animation, bool finished, void *context) {
  NotificationsData *data = context;
  if (animation != data->fold_animation) {
    return;
  }
  data->fold_animation = nullptr;
  if (finished) {
    const bool opened = data->fold_opening;
    const int16_t header = data->fold_header;
    const int16_t count = data->fold_count;
    prv_fold_finish(data, true);
    if (opened) {
      prv_reveal_opened_rows(data, header, count);
    }
  }
}

//! How many rows below a header fill the screen beneath it, open, at most count.
static int16_t prv_rows_filling_below(NotificationsData *data, int16_t header, int16_t count) {
  ScrollLayer *scroll = menu_layer_get_scroll_layer(&data->menu_layer);
  int16_t bottom = scroll_layer_get_content_offset(scroll).y;
  for (int16_t i = 0; i <= header; i++) {
    bottom += prv_row_height_at(data, i);
  }
  const int16_t view = menu_layer_get_layer(&data->menu_layer)->bounds.size.h;
  int16_t rows = 0;
  while ((rows < count) && (bottom < view)) {
    bottom += prv_full_row_height(notification_shade_row(&data->shade, header + 1 + rows));
    rows++;
  }
  return PBL_MAX(rows, 1);
}

static void prv_fold_start(NotificationsData *data, int16_t header, int16_t count, bool opening) {
  data->fold_header = header;
  data->fold_moving = prv_rows_filling_below(data, header, count);
  data->fold_first = header + 1;
  data->fold_count = count;
  data->fold_opening = opening;
  data->fold_amount = opening ? 0 : FOLD_FULL;

  static const AnimationImplementation s_implementation = {
    .update = prv_fold_update,
  };
  Animation *animation = animation_create();
  if (!animation) {
    prv_fold_finish(data, true);
    return;
  }
  data->fold_animation = animation;
  animation_set_implementation(animation, &s_implementation);
  animation_set_duration(animation, opening ? FOLD_OPEN_MS : FOLD_CLOSE_MS);
  animation_set_curve(animation, AnimationCurveLinear);
  animation_set_handlers(animation,
                         (AnimationHandlers){
                           .stopped = prv_fold_stopped,
                         },
                         data);
  prv_reload_rows(data);
  if (!animation_schedule(animation)) {
    prv_fold_finish(data, true);
  }
}

static void prv_toggle_row(NotificationsData *data, uint16_t index) {
  if (data->fold_first >= 0) {
    // Pressed again mid-animation: just finish it (closing changes which row is where).
    const bool was_closing = !data->fold_opening;
    prv_fold_finish(data, true);
    if (was_closing) {
      return;
    }
  }
  const NotificationShadeRow *row = notification_shade_row(&data->shade, index);
  if (!row) {
    return;
  }
  if (row->expanded) {
    // Close: shrink the rows first; the model drops them when the animation ends.
    const uint16_t count = notification_shade_rows_folded_by(&data->shade, index);
    if (count > 0) {
      prv_fold_start(data, index, count, false /* opening */);
      return;
    }
    notification_shade_toggle_row(&data->shade, index);
  } else {
    const uint16_t before = notification_shade_row_count(&data->shade);
    notification_shade_toggle_row(&data->shade, index);
    const uint16_t after = notification_shade_row_count(&data->shade);
    if (after > before) {
      prv_save_collapsed_sections(data);
      prv_fold_start(data, index, after - before, true /* opening */);
      return;
    }
  }
  prv_save_collapsed_sections(data);
  prv_reload_rows(data);
}

///////////////////
// MenuLayer callbacks

static void prv_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  NotificationsData *data = context;
  if (prv_is_dismiss_all_row(data, cell_index->row)) {
    prv_settings_clear_history_window_push(data);
    return;
  }
  const NotificationShadeRow *row = notification_shade_row(&data->shade, cell_index->row);
  if (!row) {
    return;
  }
  if (row->kind != NotificationShadeRowItem) {
    // Headers and stacks open or close, as on the phone.
    prv_toggle_row(data, cell_index->row);
    return;
  }
  const NotificationShadeEntry *entry = notification_shade_item(&data->shade, row->item);
  if (entry) {
    prv_push_notification_window(data, &entry->id);
  }
}

static uint16_t prv_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index,
                                          void *context) {
  return prv_row_count(context);
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  NotificationsData *data = context;
  return prv_row_height_at(data, cell_index->row);
}

#if PBL_ROUND
static int16_t prv_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return prv_round_space_above(context);
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index,
                            void *context) {
}
#endif

static void prv_selection_changed(MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index,
                                  void *context) {
  NotificationsData *data = context;
  // Moving off a header that is still opening or closing finishes it.
  if ((data->fold_first >= 0) && (old_index.row == data->fold_header)) {
    prv_fold_finish(data, true);
  }
  prv_update_highlight_pill(data);
}

// Display the appropriate layer
static void prv_update_text_layer_visibility(NotificationsData *data) {
  const bool empty = (notification_shade_count(&data->shade) == 0);
  layer_set_hidden((Layer *)&data->menu_layer, empty);
  layer_set_hidden((Layer *)&data->text_layer, !empty);
}

//! Keeps the selection on the same notification (or the header or stack holding it) after the
//! list changes, without recentering.
static void prv_reload_keeping_selection(NotificationsData *data, const Uuid *selected_id,
                                         int16_t fallback_row) {
  prv_reload_rows(data);
  const uint16_t count = notification_shade_row_count(&data->shade);
  if (count == 0) {
    return;
  }
  int row = selected_id ? notification_shade_row_of(&data->shade, selected_id) : -1;
  if (row < 0) {
    row = (fallback_row < count) ? fallback_row : count - 1;
  }
  if (row >= 0 && (row != menu_layer_get_selected_index(&data->menu_layer).row)) {
    menu_layer_set_selected_index(&data->menu_layer, MenuIndex(0, row), MenuRowAlignNone,
                                  false /* animated */);
  }
}

//! The notification the selected row shows, if any.
static bool prv_selected_id(NotificationsData *data, Uuid *id_out) {
  const NotificationShadeRow *row =
      notification_shade_row(&data->shade, menu_layer_get_selected_index(&data->menu_layer).row);
  if (!row || (row->kind == NotificationShadeRowHeader)) {
    return false;
  }
  const NotificationShadeEntry *entry = notification_shade_item(&data->shade, row->item);
  if (!entry) {
    return false;
  }
  *id_out = entry->id;
  return true;
}

static void prv_remove_from_list(NotificationsData *data, const Uuid *id) {
  prv_fold_finish(data, true);
  Uuid selected;
  const bool has_selected = prv_selected_id(data, &selected) && !uuid_equal(&selected, id);
  const int16_t selected_row = menu_layer_get_selected_index(&data->menu_layer).row;
  notification_shade_remove(&data->shade, id);
  prv_reload_keeping_selection(data, has_selected ? &selected : nullptr, selected_row);
}

static void prv_handle_notification_removed(Uuid *id) {
  prv_remove_from_list(s_data, id);
  app_notification_window_remove_notification_by_id(id);
}

static void prv_handle_notification_added(Uuid *id) {
  NotificationsData *data = s_data;
  TimelineItem notification;
  if (!notification_storage_get(id, &notification)) {
    return;
  }
  prv_fold_finish(data, true);
  Uuid selected;
  const bool has_selected = prv_selected_id(data, &selected);
  const int16_t selected_row = menu_layer_get_selected_index(&data->menu_layer).row;

  // A re-sent notification replaces the one listed.
  notification_shade_remove(&data->shade, id);
  char sender[ATTRIBUTE_TITLE_MAX_LEN + 1];
  char title[ATTRIBUTE_TITLE_MAX_LEN + 1];
  char app_name[ATTRIBUTE_TITLE_MAX_LEN + 1];
  strncpy(sender, attribute_get_string(&notification.attr_list, AttributeIdSender, ""),
          sizeof(sender) - 1);
  strncpy(title, attribute_get_string(&notification.attr_list, AttributeIdTitle, ""),
          sizeof(title) - 1);
  strncpy(app_name, attribute_get_string(&notification.attr_list, AttributeIdAppName, ""),
          sizeof(app_name) - 1);
  sender[sizeof(sender) - 1] = title[sizeof(title) - 1] = app_name[sizeof(app_name) - 1] = '\0';
  notification_shade_add_item(&data->shade, &notification, app_name, sender, title);
  notification_shade_rebuild(&data->shade);
  timeline_item_free_allocated_buffer(&notification);

  // Drop a cached copy so the row shows the new contents.
  LoadedNotificationNode *loaded = prv_find_loaded_notification(data->loaded_notification_list, id);
  if (loaded) {
    list_remove(&loaded->node, (ListNode **)&data->loaded_notification_list, nullptr);
    prv_unload_loaded_notification(loaded);
  }

  prv_reload_keeping_selection(data, has_selected ? &selected : nullptr, selected_row);
  app_notification_window_add_new_notification_by_id(id);
}

static void prv_handle_notification(PebbleEvent *e, void *context) {
  if (e->type == PEBBLE_SYS_NOTIFICATION_EVENT) {
    Uuid *id = e->sys_notification.notification_id;
    switch (e->sys_notification.type) {
      case NotificationAdded:
        prv_handle_notification_added(id);
        break;
      case NotificationRemoved:
      case NotificationActedUpon:
        prv_handle_notification_removed(id);
        break;
      case NotificationActionResult: {
        PebbleSysNotificationActionResult *action_result = e->sys_notification.action_result;
        if (action_result && (action_result->type == ActionResultTypeSuccess ||
                              action_result->type == ActionResultTypeSuccessANCSDismiss)) {
          prv_handle_notification_removed(&action_result->id);
        }
        break;
      }
      default:
        break;
        // Not implemented
    }
    prv_update_text_layer_visibility(s_data);
  }
  // we don't handle reminders within the notifications app
}

///////////////////
// Window callbacks

static void prv_window_appear(Window *window) {
  NotificationsData *data = window_get_user_data(window);

  prv_update_text_layer_visibility(data);
}

static void prv_window_disappear(Window *window) {
  NotificationsData *data = window_get_user_data(window);
  prv_loaded_notification_list_deinit(data->loaded_notification_list);
  data->loaded_notification_list = nullptr;
}

static void prv_window_load(Window *window) {
  NotificationsData *data = window_get_user_data(window);
  MenuLayer *menu_layer = &data->menu_layer;
  menu_layer_init(menu_layer, &window->layer.bounds);
  menu_layer_set_callbacks(menu_layer, data,
                           &(MenuLayerCallbacks){
                             .get_num_rows = prv_get_num_rows_callback,
                             .draw_row = prv_draw_row_callback,
                             .get_cell_height = prv_get_cell_height,
                             .select_click = prv_select_callback,
                             .selection_changed = prv_selection_changed,
#if PBL_ROUND
                             .get_header_height = prv_get_header_height,
                             .draw_header = prv_draw_header,
#endif
                           });

#if PBL_COLOR
  menu_layer_set_highlight_colors(menu_layer, DEFAULT_NOTIFICATION_COLOR, GColorWhite);
#endif
#if PBL_ROUND
  // Slide like a rectangular list instead of jumping a row per press.
  menu_layer_set_center_focused(menu_layer, false);
  menu_layer_pad_bottom_enable(menu_layer, false);
#endif

  menu_layer_set_click_config_onto_window(menu_layer, window);
  menu_layer_set_scroll_wrap_around(menu_layer, shell_prefs_get_menu_scroll_wrap_around_enable());
  menu_layer_set_scroll_vibe_on_wrap(
      menu_layer, shell_prefs_get_menu_scroll_vibe_behavior() == MenuScrollVibeOnWrapAround);
  menu_layer_set_scroll_vibe_on_blocked(
      menu_layer, shell_prefs_get_menu_scroll_vibe_behavior() == MenuScrollVibeOnLocked);
  layer_add_child(&window->layer, menu_layer_get_layer(menu_layer));

  TextLayer *text_layer = &data->text_layer;
  const int16_t margin = 10;
  const GFont font = notification_style_scaled_font(24, true);
  const int16_t font_height = notification_style_scaled_points(24);
  text_layer_init_with_parameters(
      text_layer,
      &GRect(margin, (window->layer.bounds.size.h - font_height) / 2 - 4,
             window->layer.bounds.size.w - 2 * margin, font_height + 8),
      pbl_i18n_get("No Notifications", data), font, GColorBlack, GColorClear, GTextAlignmentCenter,
      GTextOverflowModeTrailingEllipsis);
  text_layer_set_text_color_follows_theme(text_layer);
  layer_add_child(&window->layer, text_layer_get_layer(text_layer));

  // Start on the first notification rather than on a header.
  uint16_t first = 0;
  const NotificationShadeRow *row;
  while ((row = notification_shade_row(&data->shade, first)) &&
         (row->kind == NotificationShadeRowHeader)) {
    first++;
  }
  if (!row) {
    first = 0;
  }
  prv_reload_rows(data);
  menu_layer_set_selected_index(menu_layer, MenuIndex(0, first), MenuRowAlignNone, false);
  prv_update_highlight_pill(data);
}

static void prv_push_window(NotificationsData *data) {
  Window *window = &data->window;
  window_init(window, WINDOW_NAME("Notifications"));
  window_set_user_data(window, data);
  window_set_window_handlers(window, &(WindowHandlers){
                                       .load = prv_window_load,
                                       .appear = prv_window_appear,
                                       .disappear = prv_window_disappear,
                                     });

  const bool animated = true;
  app_window_stack_push(window, animated);
}

////////////////////
// App boilerplate

static void prv_handle_init(void) {
  NotificationsData *data = s_data = app_zalloc_check(sizeof(NotificationsData));

  app_state_set_user_data(data);

  data->notification_event_info = (EventServiceInfo){
    .type = PEBBLE_SYS_NOTIFICATION_EVENT,
    .handler = prv_handle_notification,
  };
  event_service_client_subscribe(&data->notification_event_info);
  data->fold_first = -1;
  data->fold_amount = FOLD_FULL;
  notification_shade_init_with_prefs(&data->shade);
  notification_shade_load_storage(&data->shade);

  prv_push_window(data);
  // The list shows what is stored; anything the phone resends arrives as an added event.
  notification_sync_check();
}

static void prv_handle_deinit(void) {
  NotificationsData *data = app_state_get_user_data();
  prv_fold_finish(data, false);
  menu_layer_deinit(&data->menu_layer);
  event_service_client_unsubscribe(&data->notification_event_info);
  prv_loaded_notification_list_deinit(data->loaded_notification_list);
  notification_shade_deinit(&data->shade);

  pbl_i18n_free_all(data);
  app_free(data);
  s_data = nullptr;
}

static void prv_s_main(void) {
  prv_handle_init();

  app_event_loop();

  prv_handle_deinit();
}

// Launch the existing clear-history confirmation flow without opening the list UI.
static void prv_clear_history_handle_init(void) {
  // Reuse the existing dialog callbacks, which expect NotificationsData storage.
  NotificationsData *data = app_zalloc_check(sizeof(NotificationsData));

  app_state_set_user_data(data);
  prv_settings_clear_history_window_push(data);
}

static void prv_clear_history_handle_deinit(void) {
  NotificationsData *data = app_state_get_user_data();

  pbl_i18n_free_all(data);
  app_free(data);
}

static void prv_clear_history_main(void) {
  prv_clear_history_handle_init();

  app_event_loop();

  prv_clear_history_handle_deinit();
}

const PebbleProcessMd *notifications_app_get_info() {
  static const PebbleProcessMdSystem s_app_md = {
    .common =
        {
          .main_func = prv_s_main,
          // UUID: b2cae818-10f8-46df-ad2b-98ad2254a3c1
          .uuid =
              {0xb2, 0xca, 0xe8, 0x18, 0x10, 0xf8, 0x46, 0xdf, 0xad, 0x2b, 0x98, 0xad, 0x22, 0x54,
               0xa3, 0xc1},
        },
    .name = pbl_i18n_noop("Notifications"),
    .icon_resource_id = RESOURCE_ID_NOTIFICATIONS_APP_GLANCE,
  };
  return (const PebbleProcessMd *)&s_app_md;
}

const PebbleProcessMd *notifications_clear_history_app_get_info(void) {
  static const PebbleProcessMdSystem s_app_md = {
    .common =
        {
          .main_func = prv_clear_history_main,
          .uuid = NOTIFICATIONS_CLEAR_HISTORY_UUID,
          .visibility = ProcessVisibilityQuickLaunch,
        },
    .name = pbl_i18n_noop("Clear Notification History"),
  };
  return (const PebbleProcessMd *)&s_app_md;
}
