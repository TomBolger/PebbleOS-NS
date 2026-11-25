/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <applib/graphics/gtypes.h>

// Declared here: the notification layout header needs the platform's timeline resources.
void notification_layout_fill_banner_round(GContext *ctx, int16_t card_top_y, int16_t below_rest);
int16_t notification_layout_get_banner_icon_top_round(int16_t below_rest);

void PBL_WEAK notification_layout_fill_banner_round(GContext *ctx, int16_t card_top_y,
                                                    int16_t below_rest) {
}

int16_t PBL_WEAK notification_layout_get_banner_icon_top_round(int16_t below_rest) {
  return 0;
}
