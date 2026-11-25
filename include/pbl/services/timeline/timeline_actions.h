/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/services/notifications/notification_types.h>
#include <pbl/services/timeline/item.h>

#include <applib/ui/action_menu_window_private.h>

/**
 * @defgroup services_timeline_timeline_actions Timeline action menus
 * @ingroup services_timeline
 * @brief Action menus for timeline items and the handling of action results.
 *
 * Invoking an action shows progress, waits for the PebbleSysNotificationActionResult and shows
 * the result, follows chained actions or starts a reply as the result requests.
 * @{
 */

/** @brief Where an action was invoked from. */
typedef enum TimelineItemActionSource {
  /** Notification popup. */
  TimelineItemActionSourceModalNotification,
  /** Notifications app. */
  TimelineItemActionSourceNotificationApp,
  /** Timeline app. */
  TimelineItemActionSourceTimeline,
  /** Send Text app. */
  TimelineItemActionSourceSendTextApp,
  /** Phone call UI. */
  TimelineItemActionSourcePhoneUi,
} TimelineItemActionSource;

/**
 * @brief Add an action to an action menu level.
 *
 * Response and postpone actions get a submenu; the others a plain item. The label is the
 * action's title attribute.
 *
 * @param action Action to add; must outlive the menu.
 * @param root_level Level to add it to.
 */
void timeline_actions_add_action_to_root_level(TimelineItemAction *action,
                                               ActionMenuLevel *root_level);

/**
 * @brief Create the root level of a timeline action menu.
 *
 * Also records @p source and requests a responsive phone connection while the menu is open.
 *
 * @param num_actions Number of actions the level holds.
 * @param separator_index Index of the separator, see ActionMenuLevel.
 * @param source Window or app creating the menu.
 * @return Root level.
 */
ActionMenuLevel *timeline_actions_create_action_menu_root_level(uint8_t num_actions,
                                                                uint8_t separator_index,
                                                                TimelineItemActionSource source);

/**
 * @brief Whether an item has an action menu.
 *
 * The menu holds the item's actions and the watch's own: Remove for calendar pins and for pins
 * without actions, unless the phone gives one. It is never empty.
 *
 * @param item Item.
 * @return false if the item has no actions at all.
 */
bool timeline_actions_has_item_menu(const TimelineItem *item);

/**
 * @brief Perform the item's action without its menu, when that is all the menu would do.
 *
 * Applies when the menu would hold a single action that only opens an app or a pin; never to
 * actions that change anything, such as remove, dismiss or reply.
 *
 * @param item Item.
 * @return true if the action was performed and no menu should open.
 */
bool timeline_actions_perform_sole_action(const TimelineItem *item);

/**
 * @brief Create the root level of an item's action menu (see timeline_actions_has_item_menu).
 *
 * A menu whose single action opens a submenu, such as replies, starts with that submenu.
 *
 * @param item Item; must outlive the menu.
 * @param source Window or app creating the menu.
 * @return Root level, or NULL if the item has no actions.
 */
ActionMenuLevel *timeline_actions_create_item_root_level(TimelineItem *item,
                                                         TimelineItemActionSource source);

/**
 * @brief Called when an action succeeds with a "dismissed" result while its action menu is open.
 *
 * @return true if the caller showed the result itself (the menu is then closed without a result
 * dialog), false for the usual result dialog.
 */
typedef bool (*TimelineActionsDismissedHandler)(void);

/** @brief Set the handler for dismissed results; NULL for none. */
void timeline_actions_set_dismissed_handler(TimelineActionsDismissedHandler handler);

/**
 * @brief Create a timeline action menu and push it.
 *
 * The menu hierarchy is destroyed when the menu closes.
 *
 * @param base_config Menu configuration; its context must be the TimelineItem of the menu.
 * @param window_stack Window stack to push to.
 * @return Action menu, or NULL if out of memory.
 */
ActionMenu *timeline_actions_push_action_menu(ActionMenuConfig *base_config,
                                              WindowStack *window_stack);

struct ActionMenuHost;

/**
 * @brief Create a timeline action menu drawn by a host instead of its own window.
 *
 * Like timeline_actions_push_action_menu, but the menu is shown and driven by @p host (see
 * action_menu_open_hosted). Result and progress windows go on @p window_stack.
 *
 * @param base_config Menu configuration; its context must be the TimelineItem of the menu.
 * @param window_stack Window stack for result windows.
 * @param host Host showing the menu.
 * @param host_context Context passed to the host.
 * @return Action menu, or NULL if out of memory.
 */
ActionMenu *timeline_actions_open_hosted_action_menu(ActionMenuConfig *base_config,
                                                     WindowStack *window_stack,
                                                     const struct ActionMenuHost *host,
                                                     void *host_context);

/**
 * @brief Create a reply menu from a response action and push it.
 *
 * @param item Item the menu belongs to.
 * @param reply_action Response action.
 * @param bg_color Background color of the menu.
 * @param did_close_cb Called when the menu closes.
 * @param window_stack Window stack to push to.
 * @param source Window or app pushing the menu.
 * @param standalone_reply Label the voice option "Reply with Voice", for context when no menu was
 *                         shown before.
 * @return Action menu, or NULL if out of memory.
 */
ActionMenu *timeline_actions_push_response_menu(TimelineItem *item,
                                                TimelineItemAction *reply_action, GColor bg_color,
                                                ActionMenuDidCloseCb did_close_cb,
                                                WindowStack *window_stack,
                                                TimelineItemActionSource source,
                                                bool standalone_reply);

/**
 * @brief Called when an action or batch of actions completes.
 *
 * @param succeeded Whether it succeeded.
 * @param cb_data Context given with the callback.
 */
typedef void (*ActionCompleteCallback)(bool succeeded, void *cb_data);

/**
 * @brief Invoke the dismiss action of several notifications and reminders.
 *
 * Dismisses one item per KernelMain callback so the event queue drains in between; ANCS dismissals
 * after the first use bulk mode. Failures are ignored as long as one dismissal succeeds.
 *
 * @param notif_list Items to dismiss; copied.
 * @param num_notifications Number of items.
 * @param action_menu Menu the request came from, frozen until done; may be NULL.
 * @param dismiss_all_complete_callback Called when done, may be NULL.
 * @param dismiss_all_cb_data Context for @p dismiss_all_complete_callback.
 */
void timeline_actions_dismiss_all(NotificationInfo *notif_list, int num_notifications,
                                  ActionMenu *action_menu,
                                  ActionCompleteCallback dismiss_all_complete_callback,
                                  void *dismiss_all_cb_data);

/**
 * @brief Dismiss every stored notification on the phone, then remove it from the watch.
 *
 * Runs on KernelMain without any UI, one notification at a time as
 * timeline_actions_dismiss_all() does, so the phone gets one dismissal at a time. Notifications
 * already dismissed, or whose phone isn't connected, are only removed from the watch.
 * Notifications arriving meanwhile are kept.
 */
void timeline_actions_dismiss_all_stored(void);

/**
 * @brief Invoke an action without an action menu.
 *
 * @param action Action to perform.
 * @param pin Item the action belongs to.
 * @param cb Called when the action completes, also immediately with false for local or failed
 *           actions; may be NULL.
 * @param cb_data Context for @p cb.
 */
void timeline_actions_invoke_action(const TimelineItemAction *action, const TimelineItem *pin,
                                    ActionCompleteCallback cb, void *cb_data);

/** @} */
