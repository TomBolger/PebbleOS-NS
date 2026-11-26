/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <pbl/kernel/malloc.h>
#include <pbl/logging/logging.h>
#include <pbl/services/blob_db/notif_db.h>
#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/notifications/notification_storage.h>
#include <pbl/services/timeline/attribute.h>
#include <pbl/services/timeline/timeline.h>
#include <pbl/util/string.h>

PBL_LOG_MODULE_DECLARE(service_blob_db, CONFIG_SERVICE_BLOB_DB_LOG_LEVEL);

void notif_db_init(void) {
}

// The Core app names an Android notification's app only in the first of its actions, "Mute <app>"
#define CORE_MUTE_APP_PREFIX "Mute "

static const char *prv_app_name_from_mute_action(const TimelineItem *notification) {
  static const Uuid s_android_notifications = UUID_NOTIFICATIONS_DATA_SOURCE;
  if (!uuid_equal(&notification->header.parent_id, &s_android_notifications) ||
      !PBL_IS_EMPTY_STRING(
          attribute_get_string(&notification->attr_list, AttributeIdAppName, ""))) {
    return nullptr;
  }
  const size_t prefix_len = strlen(CORE_MUTE_APP_PREFIX);
  for (uint8_t i = 0; i < notification->action_group.num_actions; i++) {
    const TimelineItemAction *action = &notification->action_group.actions[i];
    if (action->type != TimelineItemActionTypeGeneric) {
      continue;
    }
    const char *title = attribute_get_string(&action->attr_list, AttributeIdTitle, "");
    if (strncmp(title, CORE_MUTE_APP_PREFIX, prefix_len) == 0) {
      const char *app_name = title + prefix_len;
      return PBL_IS_EMPTY_STRING(app_name) ? nullptr : app_name;
    }
  }
  return nullptr;
}

//! Stores a new notification, naming its app when the phone only names it in an action, so the
//! history can list and stack it by app.
static void prv_store(TimelineItem *notification) {
  const char *app_name = prv_app_name_from_mute_action(notification);
  if (!app_name) {
    notification_storage_store(notification);
    return;
  }
  AttributeList attr_list = {};
  for (uint8_t i = 0; i < notification->attr_list.num_attributes; i++) {
    attribute_list_add_attribute(&attr_list, &notification->attr_list.attributes[i]);
  }
  attribute_list_add_cstring(&attr_list, AttributeIdAppName, app_name);
  TimelineItem *named = timeline_item_create_with_attributes(
      notification->header.timestamp, notification->header.duration, notification->header.type,
      notification->header.layout, &attr_list, &notification->action_group);
  attribute_list_destroy_list(&attr_list);
  if (!named) {
    notification_storage_store(notification);
    return;
  }
  named->header = notification->header;
  notification_storage_store(named);
  timeline_item_destroy(named);
}

int notif_db_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len) {
  if (key_len != UUID_SIZE || val_len < (int)sizeof(SerializedTimelineItemHeader)) {
    return -EINVAL;
  }

  // [FBO] this is a little bit silly: we deserialize the item to then re-serialize it in
  // notification_storage_store. It has the advantage that it validates the payload
  // and works with the existing storage
  SerializedTimelineItemHeader *hdr = (SerializedTimelineItemHeader *)val;
  const uint8_t *payload = val + sizeof(SerializedTimelineItemHeader);

  const bool has_status_bits = (hdr->common.status != 0);

  TimelineItem notification = {};
  if (!timeline_item_deserialize_item(&notification, hdr, payload)) {
    return -EIO;
  }

  Uuid *id = pbl_malloc_check(sizeof(Uuid));
  *id = notification.header.id;

  char uuid_string[UUID_STRING_BUFFER_LENGTH];
  uuid_to_string(id, uuid_string);

  // notification_storage_notification_exists() also matches dismissed notifications, while
  // notification_storage_get_status() only matches live ones.
  uint8_t status;
  const bool exists = notification_storage_notification_exists(&notification.header.id);
  const bool live = exists && notification_storage_get_status(&notification.header.id, &status);

  if (live) {
    // A stored notification only gains status bits from the phone. Sent again unchanged (a
    // phone resending what the watch should hold), it is left alone.
    const uint8_t merged = status | notification.header.status;
    if (merged != status) {
      notification_storage_set_status(&notification.header.id, merged);
      PBL_LOG_INFO("Notification modified: %s", uuid_string);
      notifications_handle_notification_acted_upon(id);
    } else {
      PBL_LOG_DBG("Notification unchanged: %s", uuid_string);
      pbl_free(id);
    }
  } else if (exists && alerts_preferences_get_notification_dismissal_sync()) {
    PBL_LOG_DBG("Notification already dismissed: %s", uuid_string);
    pbl_free(id);
  } else if (!has_status_bits) {
    prv_store(&notification);
    PBL_LOG_INFO("Notification added: %s", uuid_string);
    notifications_handle_notification_added(id);
  } else {
    pbl_free(id);
  }

  timeline_item_free_allocated_buffer(&notification);
  return 0;
}

int notif_db_get_len(const uint8_t *key, int key_len) {
  if (key_len < UUID_SIZE) {
    return 0;
  }

  return notification_storage_get_len((Uuid *)key);
}

int notif_db_read(const uint8_t *key, int key_len, uint8_t *val_out, int val_out_len) {
  // NYI
  return 0;
}

int notif_db_delete(const uint8_t *key, int key_len) {
  if (key_len != UUID_SIZE) {
    return -EINVAL;
  }

  notification_storage_remove((Uuid *)key);
  notifications_handle_notification_removed((Uuid *)key);

  return 0;
}

int notif_db_flush(void) {
  notification_storage_reset_and_init();
  return 0;
}
