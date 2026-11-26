/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <applib/graphics/gtypes.h>
#include <applib/graphics/perimeter.h>
#include <applib/graphics/text.h>

#if PBL_ROUND

/**
 * @defgroup services_timeline_notification_text_block Notification text block
 * @ingroup services_timeline
 * @brief The message of a round notification, read in a block of lines of one width.
 *
 * The message is wrapped once as wide as the display is at the highest row text starts at on the
 * card at rest. Those lines keep their words while they are inside the block, the rows from there
 * down to where the display is as wide again, wherever the card is scrolled. The text above the
 * block is rewrapped to fit inside the display, ending where the block begins, and the text below
 * it flows inside the display as text flow does. Only those rewrap as the card scrolls.
 * @{
 */

/** @brief Most rows rewrapped above the block. */
#define NOTIFICATION_TEXT_BLOCK_MAX_ROWS_ABOVE 6

/** @brief A message laid out as a block; see notification_text_block_create(). */
typedef struct NotificationTextBlock {
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
  /** Alignment of each line. */
  GTextAlignment alignment;
  /** How far inside the display's edge text runs. */
  uint8_t inset;
  /** The block, on screen. */
  GTextBlock block;
  /** What the text below the block flows inside. */
  GPerimeter below;
  /** Where the message's first line is on screen at rest. */
  int16_t rest_top;
  /** Number of lines of the message wrapped at the block's width. */
  uint16_t num_lines;
  /** Where each of those lines starts. */
  uint16_t *line_starts;
  /** The line the rows above were rewrapped for, or -1. */
  int16_t above_for;
  /** Number of rows rewrapped above that line. */
  uint8_t num_above;
  /** Where each row above starts, the nearest first. */
  uint16_t above_starts[NOTIFICATION_TEXT_BLOCK_MAX_ROWS_ABOVE];
  /** Where each row above runs. */
  GRangeHorizontal above_ranges[NOTIFICATION_TEXT_BLOCK_MAX_ROWS_ABOVE];
} NotificationTextBlock;

/**
 * @brief Wrap a message at the width of the block anchored where it starts.
 * @param ctx Context to measure text with.
 * @param text The message; it is copied.
 * @param font Its font.
 * @param line_spacing_delta Its line spacing delta.
 * @param alignment Alignment of its lines.
 * @param inset How far inside the display's edge it runs.
 * @param anchor_top Where the block starts on screen: a line there runs edge to edge.
 * @param rest_top Where its first line is on screen when the card is at rest.
 * @return The block, or nullptr if out of memory.
 */
NotificationTextBlock *notification_text_block_create(GContext *ctx, const char *text, GFont font,
                                                      int16_t line_spacing_delta,
                                                      GTextAlignment alignment, uint8_t inset,
                                                      int16_t anchor_top, int16_t rest_top);

/** @brief Destroy a block; nullptr is ignored. */
void notification_text_block_destroy(NotificationTextBlock *text_block);

/**
 * @brief Lay out, and draw, the message scrolled by `scroll` from where it rests.
 * @param text_block The block.
 * @param ctx Context to draw with.
 * @param origin Where the message's first line is in the drawing box.
 * @param scroll How far the card is scrolled up from rest.
 * @param top Where text stops showing at the top of the screen, beneath the status bar.
 * @param render Whether to draw it, or only measure it.
 * @return The height of the message as laid out.
 */
int16_t notification_text_block_draw(NotificationTextBlock *text_block, GContext *ctx,
                                     GPoint origin, int16_t scroll, int16_t top, bool render);

/** @} */
#endif
