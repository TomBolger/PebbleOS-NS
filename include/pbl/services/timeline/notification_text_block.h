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
 *
 * The message is laid out once for each scroll offset the card comes to rest at, and drawn row
 * by row from that layout while the card moves: the swap layer lays out where a scroll step will
 * end as it starts, and shows that layout where the step cuts over to it.
 * @{
 */

/** @brief Most rows of a layout: those on screen, and a few beyond it either way. */
#define NOTIFICATION_TEXT_BLOCK_MAX_ROWS 24

/** @brief A line of a laid out message. */
typedef struct {
  /** Where its text starts in the message. */
  uint16_t start;
  /** Where its text ends. */
  uint16_t end;
  /** Its top, from the message's first line. */
  int16_t y;
  /** Where it runs. */
  int16_t x;
  /** How wide it runs. */
  int16_t w;
  /** Too narrow where it rests to show there: it runs as wide as the block, and shows only once
   * dragged away from there. */
  bool hidden;
} NotificationTextRow;

/** @brief The message laid out as it rests at a scroll offset: the rows on screen there, and a
 * few beyond, so that it can be moved as it is, drawn row by row without laying it out again. */
typedef struct {
  /** The scroll offset it is laid out for, or INT16_MIN for none. */
  int16_t scroll;
  /** The height of the message, or 0 if its end is beyond the rows. */
  int16_t height;
  /** Number of rows. */
  uint8_t num_rows;
  /** The rows, top first. */
  NotificationTextRow rows[NOTIFICATION_TEXT_BLOCK_MAX_ROWS];
} NotificationTextLayout;

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
  /** What the text below the block flows inside while measuring it. */
  GPerimeter below;
  /** Where the message's first line is on screen at rest. */
  int16_t rest_top;
  /** Where text stops showing at the top of the screen, beneath the status bar. */
  int16_t top;
  /** Number of lines of the message wrapped at the block's width. */
  uint16_t num_lines;
  /** Where each of those lines starts. */
  uint16_t *line_starts;
  /** The layout shown, and the one the card is moving to. */
  NotificationTextLayout layouts[2];
  /** Which of the layouts is shown. */
  uint8_t shown;
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
 * @param top Where text stops showing at the top of the screen, beneath the status bar.
 * @return The block, or nullptr if out of memory.
 */
NotificationTextBlock *notification_text_block_create(GContext *ctx, const char *text, GFont font,
                                                      int16_t line_spacing_delta,
                                                      GTextAlignment alignment, uint8_t inset,
                                                      int16_t anchor_top, int16_t rest_top,
                                                      int16_t top);

/** @brief Destroy a block; nullptr is ignored. */
void notification_text_block_destroy(NotificationTextBlock *text_block);

/**
 * @brief The height of the message laid out as it rests at a scroll offset, from the text
 * engine's text flow below the block, without keeping that layout.
 */
int16_t notification_text_block_measure(NotificationTextBlock *text_block, GContext *ctx,
                                        int16_t scroll);

/**
 * @brief Lay the message out as it rests at a scroll offset, unless it already is, keeping that
 * layout and the one shown.
 * @param show Whether to show it from now on.
 * @return The layout.
 */
const NotificationTextLayout *notification_text_block_lay_out(NotificationTextBlock *text_block,
                                                              GContext *ctx, int16_t scroll,
                                                              bool show);

/** @brief The layout shown, or nullptr if none is yet. */
const NotificationTextLayout *notification_text_block_shown(
    const NotificationTextBlock *text_block);

/**
 * @brief Draw the layout shown, as it is, wherever the card has moved to.
 * @param origin Where the message's first line is in the drawing box.
 * @param scroll How far the card is scrolled up from rest now.
 */
void notification_text_block_draw(NotificationTextBlock *text_block, GContext *ctx, GPoint origin,
                                  int16_t scroll);

/** @} */
#endif
