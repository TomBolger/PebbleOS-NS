/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/fs/fs.h>
#include <pbl/services/blob_db/notif_db.h>
#include <pbl/services/notifications/notification_storage.h>
#include <pbl/services/timeline/attribute.h>
#include <pbl/services/timeline/timeline.h>
#include <pbl/util/uuid.h>

#include <clar.h>

// Fixture
////////////////////////////////////////////////////////////////

// Fakes
////////////////////////////////////////////////////////////////
#include <fake_kernel_services_notifications.h>
#include <fake_pfs.h>
#include <fake_spi_flash.h>
#include <fake_sys_work.h>

// Stubs
////////////////////////////////////////////////////////////////

static bool s_dismissal_sync;

bool alerts_preferences_get_notification_dismissal_sync(void) {
  return s_dismissal_sync;
}

void test_notif_db__initialize(void) {
  fake_spi_flash_init(0, 0x1000000);
  fake_pfs_mount();
  notification_storage_reset();
  fake_kernel_services_notifications_reset();
  s_dismissal_sync = false;
}

void test_notif_db__cleanup(void) {
}

void test_notif_db__get_length(void) {
  SerializedTimelineItemHeader hdr = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr.common.id);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr, UUID_SIZE), sizeof(hdr));
}

void test_notif_db__insert_remove(void) {
  SerializedTimelineItemHeader hdr = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr.common.id);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_delete((uint8_t *)&hdr, UUID_SIZE), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr, UUID_SIZE), 0);
}

void test_notif_db__flush(void) {
  SerializedTimelineItemHeader hdr1 = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr1.common.id);
  SerializedTimelineItemHeader hdr2 = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr2.common.id);
  SerializedTimelineItemHeader hdr3 = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr3.common.id);

  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr1, UUID_SIZE, (uint8_t *)&hdr1, sizeof(hdr1)),
                    0);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr2, UUID_SIZE, (uint8_t *)&hdr2, sizeof(hdr2)),
                    0);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr3, UUID_SIZE, (uint8_t *)&hdr3, sizeof(hdr3)),
                    0);
  cl_assert_equal_i(notif_db_flush(), 0);
  fake_sys_work_callbacks_invoke_pending();
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr1, UUID_SIZE), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr2, UUID_SIZE), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr3, UUID_SIZE), 0);
}

void test_notif_db__redelivery_after_dismissal(void) {
  s_dismissal_sync = true;
  SerializedTimelineItemHeader hdr = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr.common.id);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_delete((uint8_t *)&hdr, UUID_SIZE), 0);

  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr, UUID_SIZE), 0);
  cl_assert_equal_i(fake_kernel_services_notifications_ancs_notifications_count(), 0);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 0);

  hdr.common.status = TimelineItemStatusRead;
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr, UUID_SIZE), 0);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 0);
}

void test_notif_db__redelivery_updates_status(void) {
  SerializedTimelineItemHeader hdr = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr.common.id);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);

  hdr.common.status = TimelineItemStatusRead;
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr, UUID_SIZE), sizeof(hdr));
  cl_assert_equal_i(fake_kernel_services_notifications_ancs_notifications_count(), 1);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 1);

  uint8_t status;
  cl_assert(notification_storage_get_status(&hdr.common.id, &status));
  cl_assert_equal_i(status, TimelineItemStatusRead);
}

void test_notif_db__redelivery_after_dismissal_without_sync(void) {
  SerializedTimelineItemHeader hdr = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr.common.id);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_delete((uint8_t *)&hdr, UUID_SIZE), 0);

  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_get_len((uint8_t *)&hdr, UUID_SIZE), sizeof(hdr));
  cl_assert_equal_i(fake_kernel_services_notifications_ancs_notifications_count(), 1);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 0);
}

void test_notif_db__redelivery_unchanged(void) {
  SerializedTimelineItemHeader hdr = {
    .common = {
      .ancs_uid = 1,
      .layout = 0,
      .flags = 0,
      .timestamp = 0,
    },
  };
  uuid_generate(&hdr.common.id);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(fake_kernel_services_notifications_ancs_notifications_count(), 1);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 0);

  // Status bits the watch set are kept when the phone sends the notification again.
  hdr.common.status = TimelineItemStatusRead;
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 1);
  hdr.common.status = 0;
  cl_assert_equal_i(notif_db_insert((uint8_t *)&hdr, UUID_SIZE, (uint8_t *)&hdr, sizeof(hdr)), 0);
  cl_assert_equal_i(fake_kernel_services_notifications_acted_upon_count(), 1);

  uint8_t status;
  cl_assert(notification_storage_get_status(&hdr.common.id, &status));
  cl_assert_equal_i(status, TimelineItemStatusRead);
}

// A notification as the Core app sends it
////////////////////////////////////////////////////////////////

static size_t prv_put_attribute(uint8_t *at, uint8_t id, const char *text) {
  const uint16_t length = strlen(text);
  at[0] = id;
  memcpy(&at[1], &length, sizeof(length));
  memcpy(&at[3], text, length);
  return 3 + length;
}

static size_t prv_put_action(uint8_t *at, uint8_t id, uint8_t type, const char *title) {
  at[0] = id;
  at[1] = type;
  at[2] = 1;
  return 3 + prv_put_attribute(&at[3], AttributeIdTitle, title);
}

//! Inserts an Android notification with Core's actions, and an AppName if given.
static void prv_insert_core_notification(Uuid *id, const Uuid *parent_id, const char *app_name) {
  uint8_t buffer[512] = {};
  SerializedTimelineItemHeader *hdr = (SerializedTimelineItemHeader *)buffer;
  uint8_t *at = buffer + sizeof(*hdr);
  *hdr = (SerializedTimelineItemHeader){
    .common = {
      .parent_id = *parent_id,
      .type = TimelineItemTypeNotification,
      .layout = LayoutIdNotification,
    },
  };
  uuid_generate(&hdr->common.id);
  *id = hdr->common.id;
  at += prv_put_attribute(at, AttributeIdTitle, "Anna");
  at += prv_put_attribute(at, AttributeIdBody, "Running late");
  hdr->num_attributes = 2;
  if (app_name) {
    at += prv_put_attribute(at, AttributeIdAppName, app_name);
    hdr->num_attributes++;
  }
  at += prv_put_action(at, 0, TimelineItemActionTypeDismiss, "Dismiss");
  at += prv_put_action(at, 1, TimelineItemActionTypeGeneric, "Open on phone");
  at += prv_put_action(at, 2, TimelineItemActionTypeGeneric, "Mute WhatsApp");
  at += prv_put_action(at, 3, TimelineItemActionTypeGeneric, "Mute Channel Chats");
  hdr->num_actions = 4;
  hdr->payload_length = at - (buffer + sizeof(*hdr));
  cl_assert_equal_i(notif_db_insert((uint8_t *)id, UUID_SIZE, buffer, at - buffer), 0);
}

static void prv_assert_app_name(const Uuid *id, const char *app_name) {
  TimelineItem item;
  cl_assert(notification_storage_get(id, &item));
  cl_assert_equal_s(attribute_get_string(&item.attr_list, AttributeIdAppName, ""), app_name);
  cl_assert_equal_s(attribute_get_string(&item.attr_list, AttributeIdTitle, ""), "Anna");
  cl_assert_equal_s(attribute_get_string(&item.attr_list, AttributeIdBody, ""), "Running late");
  cl_assert_equal_i(item.action_group.num_actions, 4);
  cl_assert_equal_s(
      attribute_get_string(&item.action_group.actions[2].attr_list, AttributeIdTitle, ""),
      "Mute WhatsApp");
  cl_assert(uuid_equal(&item.header.id, id));
  timeline_item_free_allocated_buffer(&item);
}

void test_notif_db__app_named_from_mute_action(void) {
  const Uuid android = UUID_NOTIFICATIONS_DATA_SOURCE;
  Uuid id;
  prv_insert_core_notification(&id, &android, nullptr);
  prv_assert_app_name(&id, "WhatsApp");
  cl_assert_equal_i(fake_kernel_services_notifications_ancs_notifications_count(), 1);
}

void test_notif_db__app_name_given_is_kept(void) {
  const Uuid android = UUID_NOTIFICATIONS_DATA_SOURCE;
  Uuid id;
  prv_insert_core_notification(&id, &android, "WhatsApp Business");
  prv_assert_app_name(&id, "WhatsApp Business");
}

void test_notif_db__app_not_named_for_other_sources(void) {
  const Uuid app = UuidMake(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
  Uuid id;
  prv_insert_core_notification(&id, &app, nullptr);
  prv_assert_app_name(&id, "");
}
