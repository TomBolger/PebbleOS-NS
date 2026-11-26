/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <pbl/kernel/compiler.h>

/**
 * @defgroup services_notifications_notification_sync Notification sync check
 * @ingroup services_notifications
 * @brief Lets a phone that mirrors its shade check that the watch holds all of it.
 *
 * When the launcher or the Notifications app opens and the last check is older than
 * NOTIFICATION_SYNC_CHECK_INTERVAL_S, the watch writes a NotificationSyncRequest to the
 * notification preference "notifSyncRequest". The settings sync sends it to the phone at once,
 * through the BlobDB writeback of the Settings database. The phone compares the digest with the
 * notifications it sent and resends them if they differ; the watch adds the ones it lacks and
 * leaves the others alone.
 *
 * Nothing waits for the phone, and the digest is computed on the system work queue, so the
 * apps open as fast as without the check.
 * @{
 */

//! Least time between two checks.
#define NOTIFICATION_SYNC_CHECK_INTERVAL_S (10 * 60)

#define NOTIFICATION_SYNC_REQUEST_VERSION 1

//! Value of the "notifSyncRequest" preference, little endian.
typedef struct PBL_PACKED NotificationSyncRequest {
  //! NOTIFICATION_SYNC_REQUEST_VERSION
  uint8_t version;
  //! Digest of the stored notifications from Android (UUID_NOTIFICATIONS_DATA_SOURCE), see
  //! notification_storage_digest().
  uint16_t count;
  uint32_t hash;
  //! When the request was made (UTC seconds), so a phone can ignore an old one.
  uint32_t time;
} NotificationSyncRequest;

/**
 * @brief Whether a check is due.
 *
 * @param now Current time (UTC seconds).
 * @param last_check Time of the last check, 0 if none.
 * @return true if there was no check yet, the clock went back, or the interval has passed.
 */
bool notification_sync_check_is_due(time_t now, time_t last_check);

/**
 * @brief Ask the phone to check the watch's notifications, unless it checked recently or is
 * not connected. Returns at once. Does nothing without CONFIG_NOTIFICATION_MIRROR.
 */
#ifdef CONFIG_NOTIFICATION_MIRROR
void notification_sync_check(void);
#else
static inline void notification_sync_check(void) {
}
#endif

/** @} */
