/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Filling the shade from notification storage, as the preferences say.

#include "notifications_history.h"
#include "notifications_shade.h"

#include <string.h>

#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/notifications/notification_storage.h>
#include <pbl/services/time.h>
#include <pbl/services/timeline/attribute.h>
#include <pbl/services/timeline/item.h>
#include <pbl/services/timeline/timeline.h>
#include <pbl/util/string.h>
#include <pbl/util/units.h>

static uint32_t prv_hash(const char *text) {
  // FNV-1a
  uint32_t hash = 2166136261u;
  while (*text) {
    hash = (hash ^ (uint8_t)*text++) * 16777619u;
  }
  return hash ? hash : 1;
}

static const char *prv_trimmed(char *text) {
  string_strip_trailing_whitespace(text, text);
  const char *start = string_strip_leading_whitespace(text);
  return PBL_IS_EMPTY_STRING(start) ? nullptr : start;
}

// The key a notification is stacked by when the phone doesn't say: its app, or its sender
// (Android only, as with Group by Sender).
static uint32_t prv_fallback_stack_key(const NotificationShade *shade, const TimelineItem *item,
                                       char *app_name, char *sender, char *title) {
  if (shade->group_by_app) {
    const char *app = prv_trimmed(app_name);
    return app ? prv_hash(app) : 0;
  }
  static const Uuid s_android_notifications_source = UUID_NOTIFICATIONS_DATA_SOURCE;
  if (!shade->group_by_sender || (item->header.timestamp < shade->grouping_cutoff) ||
      timeline_item_is_ancs_notif(item) ||
      !uuid_equal(&item->header.parent_id, &s_android_notifications_source)) {
    return 0;
  }
  char *from = sender;
  const bool from_title = PBL_IS_EMPTY_STRING(from);
  if (from_title) {
    // "Conversation: Sender" titles group by the conversation
    from = title;
    char *separator = strstr(from, ": ");
    if (separator && (separator != from) && (separator[2] != '\0')) {
      *separator = '\0';
    }
  }
  const char *key = prv_trimmed(from);
  return key ? prv_hash(key) : 0;
}

void notification_shade_add_item(NotificationShade *shade, const TimelineItem *item, char *app_name,
                                 char *sender, char *title) {
  if (notifications_history_is_hidden(&item->header)) {
    return;
  }
  const uint8_t section = attribute_get_uint8(&item->attr_list, AttributeIdNotificationSection, 0);
  uint32_t stack_key = attribute_get_uint32(&item->attr_list, AttributeIdStackId, 0);
  if (stack_key == 0) {
    stack_key = prv_fallback_stack_key(shade, item, app_name, sender, title);
  }
  notification_shade_add(shade, &item->header.id, item->header.timestamp,
                         notification_shade_group_for_section(section), stack_key);
}

typedef struct LoadContext {
  NotificationShade *shade;
  char *app_name;
  char *sender;
  char *title;
} LoadContext;

static bool prv_notif_item_iterator_callback(void *context, const CommonTimelineItemHeader *header,
                                             const TimelineItem *item) {
  LoadContext *load = context;
  if (item) {
    notification_shade_add_item(load->shade, item, load->app_name, load->sender, load->title);
  }
  return true;
}

void notification_shade_init_with_prefs(NotificationShade *shade) {
  const bool group_by_app = alerts_preferences_get_notification_group_by_app();
  const NotificationGroupingRange range = alerts_preferences_get_notification_grouping_range();
  const bool group_by_sender = !group_by_app && (range != NotificationGroupingRange_Never);
  time_t window = 0;
  if (range == NotificationGroupingRange_OneDay) {
    window = PBL_SEC_PER_DAY;
  } else if (range == NotificationGroupingRange_OneWeek) {
    window = 7 * PBL_SEC_PER_DAY;
  }
  const time_t now = pbl_realtime();

#ifdef CONFIG_NOTIFICATION_MIRROR
  const bool sections = alerts_preferences_get_notification_sections();
#else
  // Only a phone mirroring its shade files notifications in sections
  const bool sections = false;
#endif
  // Stacks are on whenever notifications are grouped by app or sender; the phone's own stacks
  // (AttributeIdStackId) take precedence.
  notification_shade_init(shade, sections, group_by_app || group_by_sender,
                          alerts_preferences_get_notification_collapsed_sections());
  shade->group_by_app = group_by_app;
  shade->group_by_sender = group_by_sender;
  shade->grouping_cutoff = ((window > 0) && (now > window)) ? now - window : 0;
}

void notification_shade_load_storage(NotificationShade *shade) {
  char sender[ATTRIBUTE_TITLE_MAX_LEN + 1];
  char title[ATTRIBUTE_TITLE_MAX_LEN + 1];
  char app_name[ATTRIBUTE_TITLE_MAX_LEN + 1];
  Attribute attributes[] = {
    {.id = AttributeIdSender, .cstring = sender},
    {.id = AttributeIdTitle, .cstring = title},
    {.id = AttributeIdAppName, .cstring = app_name},
    {.id = AttributeIdNotificationSection},
    {.id = AttributeIdStackId},
  };
  AttributeList attr_list = {
    .num_attributes = PBL_ARRAY_SIZE(attributes),
    .attributes = attributes,
  };
  LoadContext load = {
    .shade = shade,
    .app_name = app_name,
    .sender = sender,
    .title = title,
  };
  notification_storage_iterate_strings_after(0, &attr_list, sizeof(sender),
                                             prv_notif_item_iterator_callback, &load);
  notification_shade_rebuild(shade);
}
