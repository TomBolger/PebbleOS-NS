/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <pbl/util/uuid.h>

//! Sections and categories of the phone's notification shade, in the order they are listed.
//! Categories are listed inside Silent.
typedef enum {
  NotificationShadeGroupConversations = 0,
  NotificationShadeGroupNotifications,
  NotificationShadeGroupSilent,
  NotificationShadeGroupSuggested,
  NotificationShadeGroupPromotions,
  NotificationShadeGroupNews,
  NotificationShadeGroupSocial,
  NotificationShadeGroupCount,
} NotificationShadeGroup;

typedef enum {
  //! A notification; item is its index in the card order.
  NotificationShadeRowItem,
  //! The header of a section or category.
  NotificationShadeRowHeader,
  //! An app's stack: closed, one row for its newest notification (item); open, a small header.
  NotificationShadeRowStack,
} NotificationShadeRowKind;

typedef struct NotificationShadeEntry {
  Uuid id;
  time_t timestamp;
  uint32_t sequence;
  //! Notifications with the same non-zero key in the same group form a stack.
  uint32_t stack_key;
  uint8_t group;
} NotificationShadeEntry;

typedef struct NotificationShadeRow {
  uint8_t kind;
  uint8_t group;
  bool expanded;
  //! Notifications inside an open stack are drawn indented.
  bool in_stack;
  uint16_t item;
  uint16_t count;
} NotificationShadeRow;

#define NOTIFICATION_SHADE_MAX_OPEN_STACKS 8

typedef struct NotificationShade {
  NotificationShadeEntry *entries;
  uint16_t num_entries;
  uint16_t capacity;
  //! Card order: entry indices in list order, closed headers and stacks included.
  uint16_t *order;
  NotificationShadeRow *rows;
  uint16_t num_rows;
  bool sections;
  bool stacks;
  //! Bit per NotificationShadeGroup
  uint8_t collapsed_groups;
  uint32_t open_stacks[NOTIFICATION_SHADE_MAX_OPEN_STACKS];
  uint32_t next_sequence;
} NotificationShade;

//! Default collapsed groups: Silent starts closed.
#define NOTIFICATION_SHADE_DEFAULT_COLLAPSED (1 << NotificationShadeGroupSilent)

void notification_shade_init(NotificationShade *shade, bool sections, bool stacks,
                             uint8_t collapsed_groups);
void notification_shade_deinit(NotificationShade *shade);

//! Maps an AttributeIdNotificationSection value to the group it is listed in.
uint8_t notification_shade_group_for_section(uint8_t section_attribute);

//! Adds a notification. Call notification_shade_rebuild() once all are added.
void notification_shade_add(NotificationShade *shade, const Uuid *id, time_t timestamp,
                            uint8_t group, uint32_t stack_key);
//! Removes a notification and rebuilds the rows. Returns false if it isn't there.
bool notification_shade_remove(NotificationShade *shade, const Uuid *id);
void notification_shade_rebuild(NotificationShade *shade);

uint16_t notification_shade_row_count(const NotificationShade *shade);
const NotificationShadeRow *notification_shade_row(const NotificationShade *shade, uint16_t row);

//! All notifications in card order.
uint16_t notification_shade_count(const NotificationShade *shade);
const NotificationShadeEntry *notification_shade_item(const NotificationShade *shade,
                                                      uint16_t index);
//! Index in card order, -1 if absent.
int notification_shade_index_of(const NotificationShade *shade, const Uuid *id);
//! The row showing this notification, or the closed stack or header holding it; -1 if none.
int notification_shade_row_of(const NotificationShade *shade, const Uuid *id);

//! Opens or closes the header or stack at this row and rebuilds. Returns false for other rows.
bool notification_shade_toggle_row(NotificationShade *shade, uint16_t row);
//! How many rows closing this open header or stack would remove; changes nothing.
uint16_t notification_shade_rows_folded_by(NotificationShade *shade, uint16_t row);

//! Untranslated name of a group, for i18n_get().
const char *notification_shade_group_name(uint8_t group);
