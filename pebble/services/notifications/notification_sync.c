/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/pp/session.h>
#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/notifications/notification_storage.h>
#include <pbl/services/notifications/notification_sync.h>
#include <pbl/services/timeline/timeline.h>

PBL_LOG_MODULE_DECLARE(service_notifications, CONFIG_SERVICE_NOTIFICATIONS_LOG_LEVEL);

static time_t s_last_check;

bool notification_sync_check_is_due(time_t now, time_t last_check) {
  return (last_check == 0) || (now < last_check) ||
         (now - last_check >= NOTIFICATION_SYNC_CHECK_INTERVAL_S);
}

static void prv_send_request(void *data) {
  static const Uuid s_android_notifications = UUID_NOTIFICATIONS_DATA_SOURCE;
  NotificationStorageDigest digest;
  notification_storage_digest(&s_android_notifications, &digest);
  const NotificationSyncRequest request = {
    .version = NOTIFICATION_SYNC_REQUEST_VERSION,
    .count = digest.count,
    .hash = digest.hash,
    .time = (uint32_t)pbl_realtime(),
  };
  alerts_preferences_set_notification_sync_request(&request, sizeof(request));
  PBL_LOG_DBG("Asked the phone to check %u notifications", digest.count);
}

void notification_sync_check(void) {
  if (!pbl_pp_session_get_system()) {
    return;
  }
  const time_t now = pbl_realtime();
  if (!notification_sync_check_is_due(now, s_last_check)) {
    return;
  }
  s_last_check = now;
  pbl_sys_work_call(prv_send_request, nullptr);
}
