/* SPDX-FileCopyrightText: 2026 Aliaksandr Karnilovich */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/services/timeline/item.h>

//! Whether a stored notification is left out of the history: acted on or dismissed, with
//! dismissal sync on.
static inline bool notifications_history_is_hidden(const CommonTimelineItemHeader *header) {
  return alerts_preferences_get_notification_dismissal_sync() &&
         header->status & (TimelineItemStatusActioned | TimelineItemStatusDismissed);
}
