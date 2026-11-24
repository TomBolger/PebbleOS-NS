/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <applib/fonts/fonts.h>
#include <applib/graphics/gtypes.h>

/**
 * @file notification_style.h
 * @brief Text sizes and colors shared by the notification list and cards.
 *
 * Sizes follow Settings > Notifications > Text Size, as steps on the Gothic ladder
 * (14, 18, 24, 28) relative to the platform's default content size.
 */

/** Text size: 0 small, 1 default, 2 large. */
uint8_t notification_style_text_size(void);

/** The Gothic size used for @p points at the current text size. */
int16_t notification_style_scaled_points(int16_t points);

/** Gothic font for @p points at the current text size. */
GFont notification_style_scaled_font(int16_t points, bool bold);

/** Gothic font of exactly @p points (14, 18, 24 or 28). */
GFont notification_style_gothic(int16_t points, bool bold);

/** Background, text and secondary (headers, separators) colors for the current theme. */
GColor notification_style_bg(void);
GColor notification_style_fg(void);
GColor notification_style_secondary(void);

/**
 * @brief The body without a leading "Sender: " or "Sender\n" that repeats @p header.
 *
 * Phones often prefix a message with its sender, which the card already shows.
 */
const char *notification_style_body_text(const char *header, const char *body);
