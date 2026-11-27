/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/services/timeline/notification_text.h>
#include <pbl/util/math.h>

#include <applib/applib_malloc.auto.h>
#include <applib/fonts/fonts.h>
#include <applib/graphics/gcontext.h>

#if PBL_ROUND

static const GSize s_display = {PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT};

static bool prv_is_break(char c) {
  return (c == ' ') || (c == '\n');
}

static uint16_t prv_next_char(const char *text, uint16_t pos, uint16_t length) {
  do {
    pos++;
  } while ((pos < length) && ((text[pos] & 0xC0) == 0x80));
  return pos;
}

static uint16_t prv_prev_char(const char *text, uint16_t pos) {
  do {
    pos--;
  } while ((pos > 0) && ((text[pos] & 0xC0) == 0x80));
  return pos;
}

bool notification_text_init(NotificationText *nt, const char *str, GFont font,
                            int16_t line_spacing_delta, GTextAlignment alignment) {
  const size_t length = strlen(str);
  char *copy = applib_malloc(length + 1);
  if (!copy) {
    return false;
  }
  memcpy(copy, str, length + 1);
  const int16_t font_h = fonts_get_font_height(font);
  *nt = (NotificationText){
    .text = copy,
    .length = length,
    .font = font,
    .font_h = font_h,
    .line_h = font_h + line_spacing_delta,
    .line_spacing_delta = line_spacing_delta,
    .alignment = alignment,
  };
  return true;
}

void notification_text_deinit(NotificationText *nt) {
  applib_free(nt->text);
  nt->text = nullptr;
}

//! Ends the text at `to` while laying out text[from, to) on one row in `box`, drawing it or only
//! telling whether it fits on that one row.
static bool prv_row(NotificationText *nt, GContext *ctx, uint16_t from, uint16_t to,
                    const GRect *box, bool render) {
  while ((to > from) && prv_is_break(nt->text[to - 1])) {
    to--;
  }
  if (to == from) {
    return true;
  }
  if (box->size.w <= 0) {
    return false;
  }
  const char end = nt->text[to];
  nt->text[to] = '\0';
  TextLayoutExtended layout = {.line_spacing_delta = nt->line_spacing_delta};
  bool fits = true;
  if (render) {
    graphics_draw_text(ctx, nt->text + from, nt->font, *box, GTextOverflowModeWordWrap,
                       nt->alignment, (TextLayout *)&layout);
  } else {
    graphics_text_layout_get_max_used_size(ctx, nt->text + from, nt->font, *box,
                                           GTextOverflowModeWordWrap, nt->alignment,
                                           (TextLayout *)&layout);
    fits = (layout.max_used_size.h <= nt->line_h);
  }
  nt->text[to] = end;
  return fits;
}

bool notification_text_fits(NotificationText *nt, GContext *ctx, uint16_t from, uint16_t to,
                            int16_t width) {
  const GRect box = {GPointZero, {width, 4 * nt->line_h}};
  return prv_row(nt, ctx, from, to, &box, false /* render */);
}

void notification_text_draw_row(NotificationText *nt, GContext *ctx, uint16_t from, uint16_t to,
                                const GRect *box) {
  prv_row(nt, ctx, from, to, box, true /* render */);
}

uint16_t notification_text_wrap_forward(NotificationText *nt, GContext *ctx, uint16_t start,
                                        int16_t width) {
  const char *text = nt->text;
  uint16_t end = start;
  while ((end < nt->length) && (text[end] != '\n')) {
    uint16_t next = end;
    while ((next < nt->length) && (text[next] == ' ')) {
      next++;
    }
    while ((next < nt->length) && !prv_is_break(text[next])) {
      next++;
    }
    if (!notification_text_fits(nt, ctx, start, next, width)) {
      if (end == start) {
        // A word wider than the row is broken where it overflows
        end = prv_next_char(text, start, nt->length);
        while (end < next) {
          const uint16_t more = prv_next_char(text, end, nt->length);
          if (!notification_text_fits(nt, ctx, start, more, width)) {
            break;
          }
          end = more;
        }
      }
      break;
    }
    end = next;
  }
  return end;
}

uint16_t notification_text_wrap_backward(NotificationText *nt, GContext *ctx, uint16_t end,
                                         int16_t width) {
  const char *text = nt->text;
  // Not the spaces, nor the one line break, between the row and what follows it
  while ((end > 0) && (text[end - 1] == ' ')) {
    end--;
  }
  if ((end > 0) && (text[end - 1] == '\n')) {
    end--;
  }
  uint16_t start = end;
  while ((start > 0) && (text[start - 1] != '\n')) {
    uint16_t prev = start;
    while ((prev > 0) && (text[prev - 1] == ' ')) {
      prev--;
    }
    while ((prev > 0) && !prv_is_break(text[prev - 1])) {
      prev--;
    }
    if (!notification_text_fits(nt, ctx, prev, end, width)) {
      if (start == end) {
        // A word wider than the row keeps as much of its end as fits
        start = prv_prev_char(text, end);
        while (start > prev) {
          const uint16_t more = prv_prev_char(text, start);
          if (!notification_text_fits(nt, ctx, more, end, width)) {
            break;
          }
          start = more;
        }
      }
      break;
    }
    start = prev;
  }
  return start;
}

uint16_t notification_text_next_row(const NotificationText *nt, uint16_t end) {
  while ((end < nt->length) && (nt->text[end] == ' ')) {
    end++;
  }
  if ((end < nt->length) && (nt->text[end] == '\n')) {
    end++;
  }
  return end;
}

GRangeHorizontal notification_text_row_room(const NotificationText *nt, int16_t top,
                                            uint8_t inset) {
  // Text flow fits the lower half of each row
  const GRangeVertical rows = {top + nt->font_h / 2, nt->font_h - nt->font_h / 2};
  return g_perimeter_for_display->callback(g_perimeter_for_display, &s_display, rows, inset);
}
#endif
