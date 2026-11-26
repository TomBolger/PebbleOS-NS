# SPDX-FileCopyrightText: 2026 Aliaksandr Karnilovich
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_notifications_shade
  SOURCES
    pebble/apps/system/notifications_shade.c
  OVERRIDES dummy_board applib_malloc
)

pbl_clar_test(test_notifications_round_path
  SOURCES
    pebble/apps/system/notifications_round_path.c
  OVERRIDES dummy_board
)

pbl_clar_test(test_notifications_shade_storage
  MODULES timeline_item time
  SOURCES
    pebble/apps/system/notifications_shade.c
    pebble/apps/system/notifications_shade_storage.c
  DEFINES CONFIG_NOTIFICATION_MIRROR=1
  OVERRIDES dummy_board applib_malloc
)

pbl_clar_test(test_notifications_shade_storage_stock
  SOURCE test_notifications_shade_storage.c
  MODULES timeline_item time
  SOURCES
    pebble/apps/system/notifications_shade.c
    pebble/apps/system/notifications_shade_storage.c
  OVERRIDES dummy_board applib_malloc
)
