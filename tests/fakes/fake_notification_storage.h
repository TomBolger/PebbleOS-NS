/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/services/timeline/item.h>
#include <pbl/util/testing.h>

static TimelineItem s_last_stored_notification = {};
static int s_notification_store_count = 0;
static int s_notification_remove_count = 0;
static TimelineItem s_existing_ancs_notification = {
  .header = (CommonTimelineItemHeader){.id = UUID_INVALID, .ancs_uid = 0}
};

// Headers handed out by notification_storage_iterate()
#define FAKE_NOTIFICATION_STORAGE_MAX_HEADERS 16
static SerializedTimelineItemHeader s_iterate_headers[FAKE_NOTIFICATION_STORAGE_MAX_HEADERS];
static bool s_iterate_header_removed[FAKE_NOTIFICATION_STORAGE_MAX_HEADERS];
static int s_num_iterate_headers = 0;
static bool s_remove_fails = false;

extern PBL_T_STATIC bool prv_deep_copy_attributes_actions(AttributeList *attr_list,
                                                          TimelineItemActionGroup *action_group,
                                                          TimelineItem *item_out);

void fake_notification_storage_reset(void) {
  s_notification_store_count = 0;
  s_notification_remove_count = 0;
  s_num_iterate_headers = 0;
  s_remove_fails = false;
  s_existing_ancs_notification =
      (TimelineItem){.header = (CommonTimelineItemHeader){.id = UUID_INVALID, .ancs_uid = 0}};
}

TimelineItem *fake_notification_storage_get_last_notification(void) {
  return &s_last_stored_notification;
}

int fake_notification_storage_get_store_count(void) {
  return s_notification_store_count;
}

int fake_notification_storage_get_remove_count(void) {
  return s_notification_remove_count;
}

void fake_notification_storage_set_existing_ancs_notification(Uuid *uuid, uint32_t ancs_uid) {
  s_existing_ancs_notification =
      (TimelineItem){.header = (CommonTimelineItemHeader){.id = *uuid, .ancs_uid = ancs_uid}};
}

void fake_notification_storage_add_header(const Uuid *id, uint32_t ancs_uid, bool ancs_notif) {
  if (s_num_iterate_headers >= FAKE_NOTIFICATION_STORAGE_MAX_HEADERS) {
    return;
  }
  s_iterate_headers[s_num_iterate_headers] = (SerializedTimelineItemHeader){
    .common = {.id = *id, .ancs_uid = ancs_uid, .ancs_notif = ancs_notif},
  };
  s_iterate_header_removed[s_num_iterate_headers] = false;
  s_num_iterate_headers++;
}

//! Removing leaves the notifications stored, as when the file cannot be written.
void fake_notification_storage_set_remove_fails(bool fails) {
  s_remove_fails = fails;
}

bool fake_notification_storage_is_removed(const Uuid *id) {
  for (int i = 0; i < s_num_iterate_headers; i++) {
    if (memcmp(&s_iterate_headers[i].common.id, id, sizeof(*id)) == 0) {
      return s_iterate_header_removed[i];
    }
  }
  return false;
}

void notification_storage_init(void) {
}

void notification_storage_lock(void) {
}

void notification_storage_unlock(void) {
}

void notification_storage_store(TimelineItem *notification) {
  ++s_notification_store_count;

  // Copy notification into our last stored buffer
  timeline_item_free_allocated_buffer(&s_last_stored_notification);
  s_last_stored_notification = *notification;
  if (!prv_deep_copy_attributes_actions(&notification->attr_list, &notification->action_group,
                                        &s_last_stored_notification)) {
    s_last_stored_notification = (TimelineItem){};
  }
}

bool notification_storage_notification_exists(const Uuid *id) {
  return false;
}

size_t notification_storage_get_len(const Uuid *uuid) {
  return 0;
}

bool notification_storage_get(const Uuid *id, TimelineItem *item_out) {
  return false;
}

void notification_storage_set_status(const Uuid *id, uint8_t status) {
}

bool notification_storage_get_status(const Uuid *id, uint8_t *status) {
  return true;
}

void notification_storage_remove(const Uuid *id) {
  ++s_notification_remove_count;
  if (s_remove_fails) {
    return;
  }
  for (int i = 0; i < s_num_iterate_headers; i++) {
    if (memcmp(&s_iterate_headers[i].common.id, id, sizeof(*id)) == 0) {
      s_iterate_header_removed[i] = true;
    }
  }
}

void notification_storage_iterate(bool (*iter_callback)(void *data,
                                                        SerializedTimelineItemHeader *header),
                                  void *data) {
  for (int i = 0; i < s_num_iterate_headers; i++) {
    if (!s_iterate_header_removed[i] && !iter_callback(data, &s_iterate_headers[i])) {
      return;
    }
  }
}

bool notification_storage_find_ancs_notification_id(uint32_t ancs_uid, Uuid *uuid_out) {
  *uuid_out = s_existing_ancs_notification.header.id;
  return (s_existing_ancs_notification.header.ancs_uid == ancs_uid);
}

bool notification_storage_find_ancs_notification_by_timestamp(
    TimelineItem *notification, CommonTimelineItemHeader *header_out) {
  if (uuid_is_invalid(&s_existing_ancs_notification.header.id)) {
    return false;
  }
  *header_out = s_existing_ancs_notification.header;
  return true;
}

void notification_storage_rewrite(void (*iter_callback)(TimelineItem *notification,
                                                        SerializedTimelineItemHeader *header,
                                                        void *data),
                                  void *data) {
}
