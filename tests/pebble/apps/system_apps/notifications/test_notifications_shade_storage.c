/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Built with and without CONFIG_NOTIFICATION_MIRROR.

#include <string.h>

#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/timeline/attribute.h>
#include <pbl/services/timeline/timeline.h>

#include <apps/system/notifications_shade.h>
#include <clar.h>
#include <fake_pbl_malloc.h>

static NotificationShade s_shade;

// Preferences: Sections on, Group by App, Silent closed
bool alerts_preferences_get_notification_sections(void) {
  return true;
}

bool alerts_preferences_get_notification_group_by_app(void) {
  return true;
}

uint8_t alerts_preferences_get_notification_collapsed_sections(void) {
  return NOTIFICATION_SHADE_DEFAULT_COLLAPSED;
}

NotificationGroupingRange alerts_preferences_get_notification_grouping_range(void) {
  return NotificationGroupingRange_Never;
}

void notification_storage_iterate_strings_after(
    time_t item_cutoff, AttributeList *attr_list, size_t buffer_size,
    bool (*iter_callback)(void *data, const CommonTimelineItemHeader *header,
                          const TimelineItem *item),
    void *data) {
}

//! Adds an Android notification as the phone sends it: section and stack id only when given.
static void prv_add(uint8_t id, time_t timestamp, const char *app, int section) {
  Attribute attributes[1];
  AttributeList attr_list = {.attributes = attributes};
  if (section >= 0) {
    attributes[attr_list.num_attributes++] = (Attribute){
      .id = AttributeIdNotificationSection,
      .uint8 = (uint8_t)section,
    };
  }
  const TimelineItem item = {
    .header =
        {
          .id = UuidMake(id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
          .parent_id = UUID_NOTIFICATIONS_DATA_SOURCE,
          .timestamp = timestamp,
          .type = TimelineItemTypeNotification,
        },
    .attr_list = attr_list,
  };
  char app_name[ATTRIBUTE_TITLE_MAX_LEN + 1];
  char sender[ATTRIBUTE_TITLE_MAX_LEN + 1] = "";
  char title[ATTRIBUTE_TITLE_MAX_LEN + 1] = "Title";
  strncpy(app_name, app, sizeof(app_name) - 1);
  app_name[sizeof(app_name) - 1] = '\0';
  notification_shade_add_item(&s_shade, &item, app_name, sender, title);
}

static void prv_assert_row(uint16_t index, NotificationShadeRowKind kind, uint8_t group,
                           uint16_t count) {
  const NotificationShadeRow *row = notification_shade_row(&s_shade, index);
  cl_assert(row);
  cl_assert_equal_i(row->kind, kind);
  cl_assert_equal_i(row->group, group);
  cl_assert_equal_i(row->count, count);
}

static uint8_t prv_row_item_id(uint16_t index) {
  const NotificationShadeRow *row = notification_shade_row(&s_shade, index);
  cl_assert(row);
  return notification_shade_item(&s_shade, row->item)->id.byte0;
}

void test_notifications_shade_storage__initialize(void) {
  fake_pbl_malloc_clear_tracking();
  notification_shade_init_with_prefs(&s_shade);
}

void test_notifications_shade_storage__cleanup(void) {
  notification_shade_deinit(&s_shade);
  fake_pbl_malloc_check_net_allocs();
}

//! Notifications as the stock Core app sends them: no section, no stack id. They stack by app.
void test_notifications_shade_storage__without_sections_stack_by_app(void) {
  prv_add(1, 100, "WhatsApp", -1);
  prv_add(2, 200, "Gmail", -1);
  prv_add(3, 300, "WhatsApp", -1);
  notification_shade_rebuild(&s_shade);

  uint16_t row = 0;
#ifdef CONFIG_NOTIFICATION_MIRROR
  prv_assert_row(row++, NotificationShadeRowHeader, NotificationShadeGroupNotifications, 3);
#endif
  prv_assert_row(row, NotificationShadeRowStack, NotificationShadeGroupNotifications, 2);
  cl_assert_equal_i(prv_row_item_id(row++), 3);
  prv_assert_row(row, NotificationShadeRowItem, NotificationShadeGroupNotifications, 1);
  cl_assert_equal_i(prv_row_item_id(row++), 2);
  cl_assert_equal_i(notification_shade_row_count(&s_shade), row);

  // Paged through newest first, the stack kept together
  cl_assert_equal_i(notification_shade_count(&s_shade), 3);
  cl_assert_equal_i(notification_shade_item(&s_shade, 0)->id.byte0, 3);
  cl_assert_equal_i(notification_shade_item(&s_shade, 1)->id.byte0, 1);
  cl_assert_equal_i(notification_shade_item(&s_shade, 2)->id.byte0, 2);
}

//! Without an app name there is nothing to stack by.
void test_notifications_shade_storage__without_app_name_listed_one_by_one(void) {
  prv_add(1, 100, "", -1);
  prv_add(2, 200, " ", -1);
  notification_shade_rebuild(&s_shade);

  uint16_t row = 0;
#ifdef CONFIG_NOTIFICATION_MIRROR
  row++;
#endif
  prv_assert_row(row, NotificationShadeRowItem, NotificationShadeGroupNotifications, 1);
  cl_assert_equal_i(prv_row_item_id(row++), 2);
  prv_assert_row(row, NotificationShadeRowItem, NotificationShadeGroupNotifications, 1);
  cl_assert_equal_i(prv_row_item_id(row++), 1);
  cl_assert_equal_i(notification_shade_row_count(&s_shade), row);
}

//! Sections are listed only for a phone that mirrors its shade.
void test_notifications_shade_storage__sections_only_when_mirroring(void) {
  prv_add(1, 100, "Telegram", 1 /* conversation */);
  prv_add(2, 200, "Shop", 2 | (2 << 2) /* promotions */);
  notification_shade_rebuild(&s_shade);

#ifdef CONFIG_NOTIFICATION_MIRROR
  // Conversations, then Silent closed with Promotions in it
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 3);
  prv_assert_row(0, NotificationShadeRowHeader, NotificationShadeGroupConversations, 1);
  prv_assert_row(1, NotificationShadeRowItem, NotificationShadeGroupConversations, 1);
  prv_assert_row(2, NotificationShadeRowHeader, NotificationShadeGroupSilent, 1);
#else
  // Newest first, every one shown
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 2);
  cl_assert_equal_i(prv_row_item_id(0), 2);
  cl_assert_equal_i(prv_row_item_id(1), 1);
#endif
}
