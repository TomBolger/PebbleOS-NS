# SPDX-FileCopyrightText: 2026 Aliaksandr Karnilovich
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_notifications_history
  SOURCES
    pebble/apps/system/notifications_history.c
    lib/util/string.c
  OVERRIDES dummy_board
)

pbl_clar_test(test_notifications_shade
  SOURCES
    pebble/apps/system/notifications_shade.c
  OVERRIDES dummy_board
)

pbl_clar_test(test_notifications_round_path
  SOURCES
    pebble/apps/system/notifications_round_path.c
  OVERRIDES dummy_board
)
