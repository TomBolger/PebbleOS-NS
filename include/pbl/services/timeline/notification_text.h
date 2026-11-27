/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <applib/graphics/gtypes.h>
#include <applib/graphics/perimeter.h>
#include <applib/graphics/text.h>

#if PBL_ROUND
/**
 * @defgroup services_timeline_notification_text Notification text
 * @ingroup services_timeline
 * @brief A round notification's message, wrapped a row at a time to the shape of the display.
 *
 * Rows are wrapped greedily by word, each as the text engine measures it, so that each can be
 * as wide as the display is where it is, and drawn one by one.
 * @{
 */

/** @brief Rows narrower than this, at the very top and bottom of the display, are too narrow to
 * hold text. */
#define NOTIFICATION_TEXT_MIN_ROW_WIDTH 60

/** @brief A message, measured and drawn a row at a time. */
typedef struct NotificationText {
  /** Copy of the message, ended at a row while measuring or drawing it. */
  char *text;
  /** Length of the message in bytes. */
  uint16_t length;
  /** Font of the message. */
  GFont font;
  /** Height of the font. */
  int16_t font_h;
  /** Distance between lines. */
  int16_t line_h;
  /** Line spacing delta, so that line_h is the font's height plus it. */
  int16_t line_spacing_delta;
  /** Alignment of each row. */
  GTextAlignment alignment;
} NotificationText;

/**
 * @brief Copy a message to wrap it a row at a time.
 * @return false if out of memory.
 */
bool notification_text_init(NotificationText *text, const char *str, GFont font,
                            int16_t line_spacing_delta, GTextAlignment alignment);

/** @brief Free a message's copy. */
void notification_text_deinit(NotificationText *text);

/** @brief Whether text[from, to) fits on one row `width` wide. */
bool notification_text_fits(NotificationText *text, GContext *ctx, uint16_t from, uint16_t to,
                            int16_t width);

/** @brief Where the row starting at `start` ends, `width` wide: as many words as fit. */
uint16_t notification_text_wrap_forward(NotificationText *text, GContext *ctx, uint16_t start,
                                        int16_t width);

/** @brief Where the row ending at `end` starts, `width` wide: as many words before it as fit. */
uint16_t notification_text_wrap_backward(NotificationText *text, GContext *ctx, uint16_t end,
                                         int16_t width);

/** @brief Where the row after one ending at `end` starts: past its spaces and one line break. */
uint16_t notification_text_next_row(const NotificationText *text, uint16_t end);

/** @brief Draw text[from, to) as one row in `box`. */
void notification_text_draw_row(NotificationText *text, GContext *ctx, uint16_t from, uint16_t to,
                                const GRect *box);

/**
 * @brief Where a row of text whose top is at `top` on screen has room inside the display, `inset`
 * from its edge, as text flow fits it: by the lower half of the row.
 */
GRangeHorizontal notification_text_row_room(const NotificationText *text, int16_t top,
                                            uint8_t inset);

/** @} */
#endif
