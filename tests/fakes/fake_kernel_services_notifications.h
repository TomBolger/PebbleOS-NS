/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/services/timeline/item.h>

void notifications_handle_ancs_message(TimelineItem *notification);

void notifications_add_notification(TimelineItem *notification);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Fake manipulation:

//! Resets the fake (i.e. ANCS count)
void fake_kernel_services_notifications_reset(void);

//! @return Number of times notifications_handle_ancs_message() was called.
uint32_t fake_kernel_services_notifications_ancs_notifications_count(void);

//! @return Number of times notifications_handle_notification_acted_upon() was called.
uint32_t fake_kernel_services_notifications_acted_upon_count(void);

//! Notifications added with an alert, by notifications_add_notification()
uint32_t fake_kernel_services_notifications_alert_count(void);
