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

// Fitted to the display where the card ends
/////////////////////////////////////////////

int16_t notification_text_fit_row_width(const NotificationTextFit *fit, int16_t top) {
  const NotificationText *nt = &fit->text;
  // Only the bottom of the message is fitted: above the middle of the display it keeps its column,
  // and below it runs edge to edge
  if (top + nt->font_h / 2 <= s_display.h / 2) {
    return fit->column.size_w;
  }
  const int16_t room = notification_text_row_room(nt, top, fit->inset).size_w;
  return PBL_MAX(room, NOTIFICATION_TEXT_MIN_ROW_WIDTH);
}

int16_t notification_text_fit_end_scroll(const NotificationTextFitPlace *place, int16_t height) {
  return PBL_MAX(place->top + height + place->after - place->frame_h, 0);
}

int16_t notification_text_fit_height(const NotificationTextFit *fit) {
  return fit->num_rows * fit->text.line_h;
}

//! Wraps the message into rows whose widths are where they are with its first row's top at
//! `first_top` on screen, or all in its column for INT16_MAX.
static bool prv_fit_wrap(NotificationTextFit *fit, GContext *ctx, int16_t first_top) {
  NotificationText *nt = &fit->text;
  const uint16_t chunk = 16;
  uint16_t capacity = 0;
  fit->num_rows = 0;
  uint16_t start = 0;
  while (start < nt->length) {
    if (fit->num_rows == capacity) {
      capacity += chunk;
      uint16_t *starts = applib_realloc(fit->starts, capacity * sizeof(*starts));
      if (starts) {
        fit->starts = starts;
      }
      int16_t *widths = applib_realloc(fit->widths, capacity * sizeof(*widths));
      if (widths) {
        fit->widths = widths;
      }
      if (!starts || !widths) {
        fit->num_rows = 0;
        return false;
      }
    }
    const int16_t width =
        (first_top == INT16_MAX)
            ? fit->column.size_w
            : notification_text_fit_row_width(fit, first_top + fit->num_rows * nt->line_h);
    fit->starts[fit->num_rows] = start;
    fit->widths[fit->num_rows++] = width;
    start = notification_text_next_row(nt, notification_text_wrap_forward(nt, ctx, start, width));
  }
  return true;
}

bool notification_text_fit_init(NotificationTextFit *fit, GContext *ctx, const char *str,
                                GFont font, int16_t line_spacing_delta, GTextAlignment alignment,
                                GRangeHorizontal column, uint8_t inset) {
  *fit = (NotificationTextFit){.column = column, .inset = inset};
  if (!notification_text_init(&fit->text, str, font, line_spacing_delta, alignment)) {
    return false;
  }
  if (!prv_fit_wrap(fit, ctx, INT16_MAX)) {
    notification_text_fit_deinit(fit);
    return false;
  }
  return true;
}

void notification_text_fit_deinit(NotificationTextFit *fit) {
  notification_text_deinit(&fit->text);
  applib_free(fit->starts);
  applib_free(fit->widths);
  fit->starts = nullptr;
  fit->widths = nullptr;
  fit->num_rows = 0;
}

int16_t notification_text_fit_lay_out(NotificationTextFit *fit, GContext *ctx,
                                      const NotificationTextFitPlace *place) {
  // Where the card ends depends on how tall the message is, which depends on how it is wrapped
  // there. Rows fitted for an end no lower than where it turns out to be only get more room there,
  // so the message is wrapped again until its height settles, else as for the last height that
  // was no taller than it came out.
  const int16_t first_top = place->card_top + place->top;
  int16_t assumed = notification_text_fit_height(fit);
  int16_t fits = assumed;
  for (int i = 0; i < 4; i++) {
    prv_fit_wrap(fit, ctx, first_top - notification_text_fit_end_scroll(place, assumed));
    const int16_t height = notification_text_fit_height(fit);
    if (height == assumed) {
      return height;
    }
    if (height > assumed) {
      fits = assumed;
    }
    assumed = height;
  }
  prv_fit_wrap(fit, ctx, first_top - notification_text_fit_end_scroll(place, fits));
  return notification_text_fit_height(fit);
}

void notification_text_fit_draw(NotificationTextFit *fit, GContext *ctx, GPoint origin) {
  NotificationText *nt = &fit->text;
  const GRect *clip = &ctx->draw_state.clip_box;
  const int16_t to_global = ctx->draw_state.drawing_box.origin.y + origin.y;
  for (uint16_t row = 0; row < fit->num_rows; row++) {
    const int16_t y = row * nt->line_h;
    if ((to_global + y + nt->line_h <= clip->origin.y) ||
        (to_global + y >= clip->origin.y + clip->size.h)) {
      continue;
    }
    const int16_t w = fit->widths[row];
    const GRect box = {
      {fit->column.origin_x + (fit->column.size_w - w) / 2, origin.y + y},
      {w, nt->font_h}
    };
    const uint16_t end = (row + 1 < fit->num_rows) ? fit->starts[row + 1] : nt->length;
    notification_text_draw_row(nt, ctx, fit->starts[row], end, &box);
  }
}
#endif
