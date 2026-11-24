/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <apps/system/notifications_shade.h>
#include <clar.h>
#include <fake_pbl_malloc.h>

static NotificationShade s_shade;

static Uuid prv_id(uint8_t value) {
  return UuidMake(value, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static void prv_add(uint8_t id, time_t timestamp, uint8_t group, uint32_t stack_key) {
  const Uuid uuid = prv_id(id);
  notification_shade_add(&s_shade, &uuid, timestamp, group, stack_key);
}

static uint8_t prv_item_id(uint16_t index) {
  return notification_shade_item(&s_shade, index)->id.byte0;
}

static void prv_assert_row(uint16_t index, NotificationShadeRowKind kind, uint8_t group,
                           uint16_t count, bool expanded) {
  const NotificationShadeRow *row = notification_shade_row(&s_shade, index);
  cl_assert(row);
  cl_assert_equal_i(row->kind, kind);
  cl_assert_equal_i(row->group, group);
  cl_assert_equal_i(row->count, count);
  cl_assert_equal_b(row->expanded, expanded);
}

static void prv_assert_item_row(uint16_t index, uint8_t id, bool in_stack) {
  const NotificationShadeRow *row = notification_shade_row(&s_shade, index);
  cl_assert(row);
  cl_assert_equal_i(row->kind, NotificationShadeRowItem);
  cl_assert_equal_i(prv_item_id(row->item), id);
  cl_assert_equal_b(row->in_stack, in_stack);
}

void test_notifications_shade__initialize(void) {
  fake_pbl_malloc_clear_tracking();
}

void test_notifications_shade__cleanup(void) {
  notification_shade_deinit(&s_shade);
  fake_pbl_malloc_check_net_allocs();
}

void test_notifications_shade__section_attribute_mapping(void) {
  cl_assert_equal_i(notification_shade_group_for_section(0), NotificationShadeGroupNotifications);
  cl_assert_equal_i(notification_shade_group_for_section(1), NotificationShadeGroupConversations);
  cl_assert_equal_i(notification_shade_group_for_section(2), NotificationShadeGroupSilent);
  cl_assert_equal_i(notification_shade_group_for_section(3), NotificationShadeGroupNotifications);
  cl_assert_equal_i(notification_shade_group_for_section((1 << 2) | 2),
                    NotificationShadeGroupSuggested);
  cl_assert_equal_i(notification_shade_group_for_section((2 << 2) | 2),
                    NotificationShadeGroupPromotions);
  cl_assert_equal_i(notification_shade_group_for_section((3 << 2) | 2), NotificationShadeGroupNews);
  cl_assert_equal_i(notification_shade_group_for_section((4 << 2) | 2),
                    NotificationShadeGroupSocial);
  // Unknown categories fall back to the section.
  cl_assert_equal_i(notification_shade_group_for_section((7 << 2) | 1),
                    NotificationShadeGroupConversations);
}

void test_notifications_shade__flat_without_sections(void) {
  notification_shade_init(&s_shade, false, false, NOTIFICATION_SHADE_DEFAULT_COLLAPSED);
  prv_add(1, 100, NotificationShadeGroupSilent, 0);
  prv_add(2, 300, NotificationShadeGroupConversations, 0);
  prv_add(3, 200, NotificationShadeGroupSocial, 0);
  notification_shade_rebuild(&s_shade);

  // No headers, newest first, even for groups that would be collapsed.
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 3);
  prv_assert_item_row(0, 2, false);
  prv_assert_item_row(1, 3, false);
  prv_assert_item_row(2, 1, false);
  cl_assert_equal_i(prv_item_id(0), 2);
}

void test_notifications_shade__sections_in_shade_order(void) {
  notification_shade_init(&s_shade, true, false, NOTIFICATION_SHADE_DEFAULT_COLLAPSED);
  prv_add(1, 400, NotificationShadeGroupSilent, 0);
  prv_add(2, 300, NotificationShadeGroupNotifications, 0);
  prv_add(3, 200, NotificationShadeGroupConversations, 0);
  prv_add(4, 100, NotificationShadeGroupNews, 0);
  prv_add(5, 50, NotificationShadeGroupConversations, 0);
  notification_shade_rebuild(&s_shade);

  // Silent starts closed and holds its categories, so News has no header of its own.
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 6);
  prv_assert_row(0, NotificationShadeRowHeader, NotificationShadeGroupConversations, 2, true);
  prv_assert_item_row(1, 3, false);
  prv_assert_item_row(2, 5, false);
  prv_assert_row(3, NotificationShadeRowHeader, NotificationShadeGroupNotifications, 1, true);
  prv_assert_item_row(4, 2, false);
  prv_assert_row(5, NotificationShadeRowHeader, NotificationShadeGroupSilent, 2, false);

  // The card order still holds the notifications of closed sections.
  cl_assert_equal_i(notification_shade_count(&s_shade), 5);
  cl_assert_equal_i(prv_item_id(0), 3);
  cl_assert_equal_i(prv_item_id(1), 5);
  cl_assert_equal_i(prv_item_id(2), 2);
  cl_assert_equal_i(prv_item_id(3), 1);
  cl_assert_equal_i(prv_item_id(4), 4);

  // A notification in a closed section maps to its header.
  const Uuid news = prv_id(4);
  cl_assert_equal_i(notification_shade_row_of(&s_shade, &news), 5);

  // Opening Silent shows its notifications and the News category under it.
  cl_assert_equal_i(notification_shade_rows_folded_by(&s_shade, 5), 0);
  cl_assert(notification_shade_toggle_row(&s_shade, 5));
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 9);
  prv_assert_row(5, NotificationShadeRowHeader, NotificationShadeGroupSilent, 2, true);
  prv_assert_item_row(6, 1, false);
  prv_assert_row(7, NotificationShadeRowHeader, NotificationShadeGroupNews, 1, true);
  prv_assert_item_row(8, 4, false);
  cl_assert_equal_i(notification_shade_row_of(&s_shade, &news), 8);
  cl_assert_equal_i(notification_shade_rows_folded_by(&s_shade, 5), 3);
  // Measuring changed nothing.
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 9);
  cl_assert_equal_i(s_shade.collapsed_groups, 0);

  // Items aren't toggleable.
  cl_assert(!notification_shade_toggle_row(&s_shade, 6));
}

void test_notifications_shade__stacks(void) {
  notification_shade_init(&s_shade, true, true, 0);
  prv_add(1, 500, NotificationShadeGroupNotifications, 0xAA);
  prv_add(2, 400, NotificationShadeGroupNotifications, 0);
  prv_add(3, 300, NotificationShadeGroupNotifications, 0xAA);
  prv_add(4, 200, NotificationShadeGroupNotifications, 0xBB);
  prv_add(5, 100, NotificationShadeGroupSilent, 0xAA);
  notification_shade_rebuild(&s_shade);

  // The stack sits at its newest notification. A lone stack member in another section isn't a
  // stack there.
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 6);
  prv_assert_row(0, NotificationShadeRowHeader, NotificationShadeGroupNotifications, 4, true);
  prv_assert_row(1, NotificationShadeRowStack, NotificationShadeGroupNotifications, 2, false);
  prv_assert_item_row(2, 2, false);
  prv_assert_item_row(3, 4, false);
  prv_assert_row(4, NotificationShadeRowHeader, NotificationShadeGroupSilent, 1, true);
  prv_assert_item_row(5, 5, false);

  // Stack members are consecutive in the card order.
  cl_assert_equal_i(prv_item_id(0), 1);
  cl_assert_equal_i(prv_item_id(1), 3);
  cl_assert_equal_i(prv_item_id(2), 2);
  const Uuid older_member = prv_id(3);
  cl_assert_equal_i(notification_shade_row_of(&s_shade, &older_member), 1);

  cl_assert_equal_i(notification_shade_rows_folded_by(&s_shade, 1), 0);
  cl_assert(notification_shade_toggle_row(&s_shade, 1));
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 8);
  prv_assert_row(1, NotificationShadeRowStack, NotificationShadeGroupNotifications, 2, true);
  prv_assert_item_row(2, 1, true);
  prv_assert_item_row(3, 3, true);
  prv_assert_item_row(4, 2, false);
  cl_assert_equal_i(notification_shade_row_of(&s_shade, &older_member), 3);
  cl_assert_equal_i(notification_shade_rows_folded_by(&s_shade, 1), 2);

  // Removing a member leaves a single notification, which is no longer a stack.
  cl_assert(notification_shade_remove(&s_shade, &older_member));
  cl_assert(!notification_shade_remove(&s_shade, &older_member));
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 6);
  prv_assert_item_row(1, 1, false);
}

void test_notifications_shade__stacks_off_ignores_keys(void) {
  notification_shade_init(&s_shade, false, false, 0);
  prv_add(1, 200, NotificationShadeGroupNotifications, 0xAA);
  prv_add(2, 100, NotificationShadeGroupNotifications, 0xAA);
  notification_shade_rebuild(&s_shade);
  cl_assert_equal_i(notification_shade_row_count(&s_shade), 2);
  prv_assert_item_row(0, 1, false);
  prv_assert_item_row(1, 2, false);
}

void test_notifications_shade__open_stacks_are_bounded(void) {
  notification_shade_init(&s_shade, false, true, 0);
  for (uint8_t i = 0; i < NOTIFICATION_SHADE_MAX_OPEN_STACKS + 2; i++) {
    prv_add(2 * i + 1, 1000 - 2 * i, NotificationShadeGroupNotifications, 100 + i);
    prv_add(2 * i + 2, 999 - 2 * i, NotificationShadeGroupNotifications, 100 + i);
  }
  notification_shade_rebuild(&s_shade);
  cl_assert_equal_i(notification_shade_row_count(&s_shade), NOTIFICATION_SHADE_MAX_OPEN_STACKS + 2);
  // Open every stack, last row first so earlier rows keep their index.
  for (int row = NOTIFICATION_SHADE_MAX_OPEN_STACKS + 1; row >= 0; row--) {
    cl_assert(notification_shade_toggle_row(&s_shade, row));
  }
  // Only the most recently opened stacks stay open.
  uint16_t open = 0;
  for (uint16_t i = 0; i < notification_shade_row_count(&s_shade); i++) {
    const NotificationShadeRow *row = notification_shade_row(&s_shade, i);
    if ((row->kind == NotificationShadeRowStack) && row->expanded) {
      open++;
    }
  }
  cl_assert_equal_i(open, NOTIFICATION_SHADE_MAX_OPEN_STACKS);
}

void test_notifications_shade__same_timestamp_keeps_arrival_order(void) {
  notification_shade_init(&s_shade, false, false, 0);
  prv_add(1, 100, NotificationShadeGroupNotifications, 0);
  prv_add(2, 100, NotificationShadeGroupNotifications, 0);
  notification_shade_rebuild(&s_shade);
  // The later one is newer.
  cl_assert_equal_i(prv_item_id(0), 2);
  cl_assert_equal_i(prv_item_id(1), 1);
}
