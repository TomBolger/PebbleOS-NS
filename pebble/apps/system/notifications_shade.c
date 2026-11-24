/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "notifications_shade.h"

#include <string.h>

#include <pbl/i18n/i18n.h>

#include <applib/applib_malloc.auto.h>

#define SECTION_MASK         0x03
#define SECTION_CONVERSATION 1
#define SECTION_SILENT       2
#define CATEGORY_SHIFT       2
#define CATEGORY_MASK        0x07
#define NUM_CATEGORIES       4

void notification_shade_init(NotificationShade *shade, bool sections, bool stacks,
                             uint8_t collapsed_groups) {
  *shade = (NotificationShade){
    .sections = sections,
    .stacks = stacks,
    .collapsed_groups = collapsed_groups,
  };
}

void notification_shade_deinit(NotificationShade *shade) {
  applib_free(shade->entries);
  applib_free(shade->order);
  applib_free(shade->rows);
  shade->entries = nullptr;
  shade->order = nullptr;
  shade->rows = nullptr;
  shade->num_entries = 0;
  shade->num_rows = 0;
  shade->capacity = 0;
}

uint8_t notification_shade_group_for_section(uint8_t section_attribute) {
  const uint8_t category = (section_attribute >> CATEGORY_SHIFT) & CATEGORY_MASK;
  if ((category >= 1) && (category <= NUM_CATEGORIES)) {
    return NotificationShadeGroupSuggested + category - 1;
  }
  switch (section_attribute & SECTION_MASK) {
    case SECTION_CONVERSATION:
      return NotificationShadeGroupConversations;
    case SECTION_SILENT:
      return NotificationShadeGroupSilent;
    default:
      return NotificationShadeGroupNotifications;
  }
}

// Newest first: by timestamp, then by the order they were added in.
static bool prv_is_newer(const NotificationShadeEntry *a, const NotificationShadeEntry *b) {
  if (a->timestamp != b->timestamp) {
    return a->timestamp > b->timestamp;
  }
  return a->sequence > b->sequence;
}

static void prv_grow(NotificationShade *shade) {
  const uint16_t capacity = shade->capacity ? shade->capacity * 2 : 16;
  NotificationShadeEntry *entries = applib_zalloc(capacity * sizeof(*entries));
  uint16_t *order = applib_zalloc(capacity * sizeof(*order));
  // At most one header per group and one stack row per notification.
  NotificationShadeRow *rows =
      applib_zalloc((2 * capacity + NotificationShadeGroupCount) * sizeof(*rows));
  if (shade->num_entries) {
    memcpy(entries, shade->entries, shade->num_entries * sizeof(*entries));
  }
  applib_free(shade->entries);
  applib_free(shade->order);
  applib_free(shade->rows);
  shade->entries = entries;
  shade->order = order;
  shade->rows = rows;
  shade->capacity = capacity;
}

void notification_shade_add(NotificationShade *shade, const Uuid *id, time_t timestamp,
                            uint8_t group, uint32_t stack_key) {
  if (shade->num_entries == shade->capacity) {
    prv_grow(shade);
  }
  const NotificationShadeEntry entry = {
    .id = *id,
    .timestamp = timestamp,
    .sequence = shade->next_sequence++,
    .stack_key = stack_key,
    .group = (group < NotificationShadeGroupCount) ? group : NotificationShadeGroupNotifications,
  };
  uint16_t at = 0;
  while ((at < shade->num_entries) && !prv_is_newer(&entry, &shade->entries[at])) {
    at++;
  }
  memmove(&shade->entries[at + 1], &shade->entries[at], (shade->num_entries - at) * sizeof(entry));
  shade->entries[at] = entry;
  shade->num_entries++;
}

static int prv_entry_of(const NotificationShade *shade, const Uuid *id) {
  for (uint16_t i = 0; i < shade->num_entries; i++) {
    if (uuid_equal(&shade->entries[i].id, id)) {
      return i;
    }
  }
  return -1;
}

bool notification_shade_remove(NotificationShade *shade, const Uuid *id) {
  const int at = prv_entry_of(shade, id);
  if (at < 0) {
    return false;
  }
  memmove(&shade->entries[at], &shade->entries[at + 1],
          (shade->num_entries - at - 1) * sizeof(*shade->entries));
  shade->num_entries--;
  notification_shade_rebuild(shade);
  return true;
}

static uint8_t prv_display_group(const NotificationShade *shade,
                                 const NotificationShadeEntry *entry) {
  return shade->sections ? entry->group : NotificationShadeGroupNotifications;
}

static uint32_t prv_stack_key(const NotificationShade *shade, const NotificationShadeEntry *entry) {
  return shade->stacks ? entry->stack_key : 0;
}

static uint16_t prv_stack_size(const NotificationShade *shade, uint16_t index, uint8_t group) {
  const uint32_t key = prv_stack_key(shade, &shade->entries[index]);
  if (key == 0) {
    return 1;
  }
  uint16_t count = 0;
  for (uint16_t i = 0; i < shade->num_entries; i++) {
    if ((shade->entries[i].stack_key == key) &&
        (prv_display_group(shade, &shade->entries[i]) == group)) {
      count++;
    }
  }
  return count;
}

static bool prv_is_stack_open(const NotificationShade *shade, uint32_t key) {
  for (int i = 0; i < NOTIFICATION_SHADE_MAX_OPEN_STACKS; i++) {
    if (shade->open_stacks[i] == key) {
      return true;
    }
  }
  return false;
}

static void prv_set_stack_open(NotificationShade *shade, uint32_t key, bool open) {
  for (int i = 0; i < NOTIFICATION_SHADE_MAX_OPEN_STACKS; i++) {
    if (shade->open_stacks[i] == key) {
      if (!open) {
        shade->open_stacks[i] = 0;
      }
      return;
    }
  }
  if (!open) {
    return;
  }
  for (int i = 0; i < NOTIFICATION_SHADE_MAX_OPEN_STACKS; i++) {
    if (shade->open_stacks[i] == 0) {
      shade->open_stacks[i] = key;
      return;
    }
  }
  memmove(&shade->open_stacks[0], &shade->open_stacks[1],
          (NOTIFICATION_SHADE_MAX_OPEN_STACKS - 1) * sizeof(shade->open_stacks[0]));
  shade->open_stacks[NOTIFICATION_SHADE_MAX_OPEN_STACKS - 1] = key;
}

static bool prv_group_open(const NotificationShade *shade, uint8_t group) {
  if (shade->collapsed_groups & (1 << group)) {
    return false;
  }
  // Categories are listed inside Silent.
  return (group < NotificationShadeGroupSuggested) ||
         !(shade->collapsed_groups & (1 << NotificationShadeGroupSilent));
}

static uint16_t prv_count_in_group(const NotificationShade *shade, uint8_t group,
                                   bool with_categories) {
  uint16_t count = 0;
  for (uint16_t i = 0; i < shade->num_entries; i++) {
    const uint8_t g = prv_display_group(shade, &shade->entries[i]);
    if ((g == group) || (with_categories && (group == NotificationShadeGroupSilent) &&
                         (g >= NotificationShadeGroupSuggested))) {
      count++;
    }
  }
  return count;
}

static void prv_add_row(NotificationShade *shade, NotificationShadeRowKind kind, uint8_t group,
                        uint16_t item, uint16_t count, bool expanded, bool in_stack) {
  shade->rows[shade->num_rows++] = (NotificationShadeRow){
    .kind = kind,
    .group = group,
    .item = item,
    .count = count,
    .expanded = expanded,
    .in_stack = in_stack,
  };
}

// Lays the notifications out like the phone's shade: Conversations, Notifications, then Silent
// with its categories, each newest first, with a stack kept together at the place of its newest
// notification. Builds both the card order and the list rows.
void notification_shade_rebuild(NotificationShade *shade) {
  shade->num_rows = 0;
  if (shade->num_entries == 0) {
    return;
  }
  bool *placed = applib_zalloc(shade->num_entries * sizeof(bool));
  uint16_t num_placed = 0;

  for (uint8_t group = 0; group < NotificationShadeGroupCount; group++) {
    const uint16_t total = prv_count_in_group(shade, group, true);
    if (total == 0) {
      continue;
    }
    const bool open = !shade->sections || prv_group_open(shade, group);
    if (shade->sections) {
      const bool parent_open = (group < NotificationShadeGroupSuggested) ||
                               prv_group_open(shade, NotificationShadeGroupSilent);
      if (parent_open) {
        const uint16_t count = (group == NotificationShadeGroupSilent)
                                   ? total
                                   : prv_count_in_group(shade, group, false);
        prv_add_row(shade, NotificationShadeRowHeader, group, 0, count, open, false);
      }
    }

    for (uint16_t first = 0; first < shade->num_entries; first++) {
      if (placed[first] || (prv_display_group(shade, &shade->entries[first]) != group)) {
        continue;
      }
      const uint16_t members = prv_stack_size(shade, first, group);
      const uint32_t key = prv_stack_key(shade, &shade->entries[first]);
      const bool stack_open = (members > 1) && prv_is_stack_open(shade, key);
      if ((members > 1) && open) {
        prv_add_row(shade, NotificationShadeRowStack, group, num_placed, members, stack_open,
                    false);
      }
      for (uint16_t member = first; member < shade->num_entries; member++) {
        const bool same =
            (member == first) || ((members > 1) && (shade->entries[member].stack_key == key) &&
                                  (prv_display_group(shade, &shade->entries[member]) == group));
        if (!same || placed[member]) {
          continue;
        }
        placed[member] = true;
        if (open && ((members == 1) || stack_open)) {
          prv_add_row(shade, NotificationShadeRowItem, group, num_placed, 1, true, members > 1);
        }
        shade->order[num_placed++] = member;
      }
    }
  }
  applib_free(placed);
}

uint16_t notification_shade_row_count(const NotificationShade *shade) {
  return shade->num_rows;
}

const NotificationShadeRow *notification_shade_row(const NotificationShade *shade, uint16_t row) {
  return (row < shade->num_rows) ? &shade->rows[row] : nullptr;
}

uint16_t notification_shade_count(const NotificationShade *shade) {
  return shade->num_entries;
}

const NotificationShadeEntry *notification_shade_item(const NotificationShade *shade,
                                                      uint16_t index) {
  return (index < shade->num_entries) ? &shade->entries[shade->order[index]] : nullptr;
}

int notification_shade_index_of(const NotificationShade *shade, const Uuid *id) {
  for (uint16_t i = 0; i < shade->num_entries; i++) {
    if (uuid_equal(&shade->entries[shade->order[i]].id, id)) {
      return i;
    }
  }
  return -1;
}

int notification_shade_row_of(const NotificationShade *shade, const Uuid *id) {
  const int index = notification_shade_index_of(shade, id);
  if (index < 0) {
    return -1;
  }
  const uint8_t group = prv_display_group(shade, notification_shade_item(shade, index));
  int container = -1;
  for (uint16_t i = 0; i < shade->num_rows; i++) {
    const NotificationShadeRow *row = &shade->rows[i];
    if ((row->kind == NotificationShadeRowItem) && (row->item == index)) {
      return i;
    }
    if ((row->kind == NotificationShadeRowStack) && !row->expanded && (index >= row->item) &&
        (index < row->item + row->count)) {
      return i;
    }
    if ((row->kind == NotificationShadeRowHeader) &&
        ((row->group == group) || ((row->group == NotificationShadeGroupSilent) &&
                                   (group >= NotificationShadeGroupSuggested)))) {
      container = i;
    }
  }
  return container;
}

static bool prv_flip_row(NotificationShade *shade, uint16_t index) {
  const NotificationShadeRow *row = notification_shade_row(shade, index);
  if (!row) {
    return false;
  }
  if (row->kind == NotificationShadeRowHeader) {
    shade->collapsed_groups ^= (1 << row->group);
  } else if (row->kind == NotificationShadeRowStack) {
    const NotificationShadeEntry *entry = notification_shade_item(shade, row->item);
    prv_set_stack_open(shade, entry->stack_key, !row->expanded);
  } else {
    return false;
  }
  return true;
}

bool notification_shade_toggle_row(NotificationShade *shade, uint16_t row) {
  if (!prv_flip_row(shade, row)) {
    return false;
  }
  notification_shade_rebuild(shade);
  return true;
}

uint16_t notification_shade_rows_folded_by(NotificationShade *shade, uint16_t index) {
  const NotificationShadeRow *row = notification_shade_row(shade, index);
  if (!row || !row->expanded) {
    return 0;
  }
  const uint16_t before = shade->num_rows;
  const uint8_t collapsed = shade->collapsed_groups;
  uint32_t open_stacks[NOTIFICATION_SHADE_MAX_OPEN_STACKS];
  memcpy(open_stacks, shade->open_stacks, sizeof(open_stacks));
  if (!prv_flip_row(shade, index)) {
    return 0;
  }
  notification_shade_rebuild(shade);
  const uint16_t after = shade->num_rows;
  shade->collapsed_groups = collapsed;
  memcpy(shade->open_stacks, open_stacks, sizeof(open_stacks));
  notification_shade_rebuild(shade);
  return (before > after) ? (before - after) : 0;
}

const char *notification_shade_group_name(uint8_t group) {
  switch (group) {
    case NotificationShadeGroupConversations:
      return pbl_i18n_noop("Conversations");
    case NotificationShadeGroupSilent:
      return pbl_i18n_noop("Silent");
    case NotificationShadeGroupSuggested:
      return pbl_i18n_noop("Suggested");
    case NotificationShadeGroupPromotions:
      return pbl_i18n_noop("Promotions");
    case NotificationShadeGroupNews:
      return pbl_i18n_noop("News");
    case NotificationShadeGroupSocial:
      return pbl_i18n_noop("Social");
    default:
      return pbl_i18n_noop("Notifications");
  }
}
