/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/services/notifications/notification_storage.h>
#include <pbl/services/notifications/notification_sync.h>
#include <pbl/services/timeline/timeline.h>

#include <clar.h>
#include <fake_rtc.h>

// Stubs
////////////////////////////////////

static bool s_connected;
static int s_digests;
static int s_requests;
static NotificationSyncRequest s_request;
static NotificationStorageDigest s_stored;
static Uuid s_digest_parent;

struct pbl_pp_session *pbl_pp_session_get_system(void) {
  return s_connected ? (struct pbl_pp_session *)1 : nullptr;
}

typedef void (*pbl_work_call_fn_t)(void *data);

bool pbl_sys_work_call(pbl_work_call_fn_t fn, void *data) {
  fn(data);
  return true;
}

void notification_storage_digest(const Uuid *parent_id, NotificationStorageDigest *digest) {
  s_digests++;
  s_digest_parent = *parent_id;
  *digest = s_stored;
}

void alerts_preferences_set_notification_sync_request(const void *request, size_t length) {
  cl_assert_equal_i(length, sizeof(NotificationSyncRequest));
  memcpy(&s_request, request, length);
  s_requests++;
}

// Setup
////////////////////////////////////

#define START_TIME 1795584541

// The module keeps its last check across cases: each case starts a day after the previous one.
static time_t s_now;

void test_notification_sync__initialize(void) {
  s_now = s_now ? s_now + 24 * 60 * 60 : START_TIME;
  fake_rtc_init(0, s_now);
  s_connected = true;
  s_digests = 0;
  s_requests = 0;
  s_request = (NotificationSyncRequest){};
  s_stored = (NotificationStorageDigest){.count = 33, .hash = 0x8badf00d};
}

void test_notification_sync__cleanup(void) {
}

// Tests
////////////////////////////////////

void test_notification_sync__is_due(void) {
  const time_t interval = NOTIFICATION_SYNC_CHECK_INTERVAL_S;
  cl_assert(notification_sync_check_is_due(START_TIME, 0));
  cl_assert(!notification_sync_check_is_due(START_TIME, START_TIME));
  cl_assert(!notification_sync_check_is_due(START_TIME + interval - 1, START_TIME));
  cl_assert(notification_sync_check_is_due(START_TIME + interval, START_TIME));
  // The clock went back (set by the phone): check rather than wait.
  cl_assert(notification_sync_check_is_due(START_TIME - 1, START_TIME));
}

void test_notification_sync__request_is_11_bytes(void) {
  cl_assert_equal_i(sizeof(NotificationSyncRequest), 11);
}

void test_notification_sync__sends_digest_then_waits_for_interval(void) {
  notification_sync_check();
  cl_assert_equal_i(s_requests, 1);
  cl_assert_equal_i(s_request.version, NOTIFICATION_SYNC_REQUEST_VERSION);
  cl_assert_equal_i(s_request.count, 33);
  cl_assert_equal_i(s_request.hash, 0x8badf00d);
  cl_assert_equal_i(s_request.time, s_now);
  const Uuid android = UUID_NOTIFICATIONS_DATA_SOURCE;
  cl_assert(uuid_equal(&s_digest_parent, &android));

  // Opening the launcher and then the app: one request.
  fake_rtc_increment_time(5);
  notification_sync_check();
  fake_rtc_increment_time(NOTIFICATION_SYNC_CHECK_INTERVAL_S - 10);
  notification_sync_check();
  cl_assert_equal_i(s_requests, 1);
  cl_assert_equal_i(s_digests, 1);

  fake_rtc_increment_time(5);
  notification_sync_check();
  cl_assert_equal_i(s_requests, 2);
}

void test_notification_sync__disconnected_does_nothing(void) {
  s_connected = false;
  notification_sync_check();
  cl_assert_equal_i(s_digests, 0);
  cl_assert_equal_i(s_requests, 0);

  // A disconnected attempt does not count as a check.
  s_connected = true;
  notification_sync_check();
  cl_assert_equal_i(s_requests, 1);
}
