/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <string.h>

#include <pbl/services/timeline/notification_text_block.h>
#include <pbl/util/math.h>

#include <applib/applib_malloc.auto.h>
#include <applib/fonts/fonts.h>

#if PBL_ROUND

//! Rows narrower than this are left empty, at the very top of the display
#define MIN_ROW_WIDTH 60

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

static uint16_t prv_line_start(const NotificationTextBlock *tb, uint16_t line) {
  return (line < tb->num_lines) ? tb->line_starts[line] : tb->length;
}

//! Ends the text at `to` while laying out text[from, to) on one line in `box`, drawing it or only
//! telling whether it fits on that one line.
static bool prv_row(NotificationTextBlock *tb, GContext *ctx, uint16_t from, uint16_t to,
                    const GRect *box, bool render) {
  while ((to > from) && prv_is_break(tb->text[to - 1])) {
    to--;
  }
  if (to == from) {
    return true;
  }
  if (box->size.w <= 0) {
    return false;
  }
  const char end = tb->text[to];
  tb->text[to] = '\0';
  TextLayoutExtended layout = {.line_spacing_delta = tb->line_spacing_delta};
  bool fits = true;
  if (render) {
    graphics_draw_text(ctx, tb->text + from, tb->font, *box, GTextOverflowModeWordWrap,
                       tb->alignment, (TextLayout *)&layout);
  } else {
    graphics_text_layout_get_max_used_size(ctx, tb->text + from, tb->font, *box,
                                           GTextOverflowModeWordWrap, tb->alignment,
                                           (TextLayout *)&layout);
    fits = (layout.max_used_size.h <= tb->line_h);
  }
  tb->text[to] = end;
  return fits;
}

static bool prv_fits(NotificationTextBlock *tb, GContext *ctx, uint16_t from, uint16_t to,
                     int16_t width) {
  const GRect box = {GPointZero, {width, 4 * tb->line_h}};
  return prv_row(tb, ctx, from, to, &box, false /* render */);
}

//! Where the line starting at `start` ends, `width` wide: as many words as fit
static uint16_t prv_wrap_forward(NotificationTextBlock *tb, GContext *ctx, uint16_t start,
                                 int16_t width) {
  const char *text = tb->text;
  uint16_t end = start;
  while ((end < tb->length) && (text[end] != '\n')) {
    uint16_t next = end;
    while ((next < tb->length) && (text[next] == ' ')) {
      next++;
    }
    while ((next < tb->length) && !prv_is_break(text[next])) {
      next++;
    }
    if (!prv_fits(tb, ctx, start, next, width)) {
      if (end == start) {
        // A word wider than the line is broken where it overflows
        end = prv_next_char(text, start, tb->length);
        while (end < next) {
          const uint16_t more = prv_next_char(text, end, tb->length);
          if (!prv_fits(tb, ctx, start, more, width)) {
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

//! Where the row ending at `end` starts, `width` wide: as many words before it as fit
static uint16_t prv_wrap_backward(NotificationTextBlock *tb, GContext *ctx, uint16_t end,
                                  int16_t width) {
  const char *text = tb->text;
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
    if (!prv_fits(tb, ctx, prev, end, width)) {
      if (start == end) {
        // A word wider than the row keeps as much of its end as fits
        start = prv_prev_char(text, end);
        while (start > prev) {
          const uint16_t more = prv_prev_char(text, start);
          if (!prv_fits(tb, ctx, more, end, width)) {
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

//! Where a line whose top is at `top` on screen runs, inside the display
static GRangeHorizontal prv_row_range(const NotificationTextBlock *tb, int16_t top) {
  static const GSize s_display = {PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT};
  // Text flow fits the lower half of each line
  const GRangeVertical rows = {top + tb->font_h / 2, tb->font_h - tb->font_h / 2};
  return g_perimeter_for_display->callback(g_perimeter_for_display, &s_display, rows, tb->inset);
}

//! Below the block: inside the display, leaving rows too narrow there empty. Past the display the
//! text runs on as wide as the block, out of sight, so that it has a height.
static GRangeHorizontal prv_below_perimeter(const GPerimeter *perimeter, const GSize *ctx_size,
                                            GRangeVertical rows, uint16_t inset) {
  const NotificationTextBlock *tb =
      (const NotificationTextBlock *)((const char *)perimeter -
                                      offsetof(NotificationTextBlock, below));
  const GRangeHorizontal room =
      g_perimeter_for_display->callback(g_perimeter_for_display, ctx_size, rows, inset);
  if (room.size_w >= MIN_ROW_WIDTH) {
    return room;
  }
  const bool past_display = (rows.origin_y >= ctx_size->h);
  return past_display ? tb->block.range : (GRangeHorizontal){0, 0};
}

static int16_t prv_rows_bottom(const GRangeVertical *rows) {
  return rows->origin_y + rows->size_h;
}

//! Rewraps the text before line `line` into the rows above it, the first of which has its top at
//! `top` on screen, down to the row beneath `limit`.
static void prv_wrap_above(NotificationTextBlock *tb, GContext *ctx, uint16_t line, int16_t top,
                           int16_t limit) {
  GRangeHorizontal ranges[NOTIFICATION_TEXT_BLOCK_MAX_ROWS_ABOVE];
  uint8_t num_rows = 0;
  for (; (num_rows < NOTIFICATION_TEXT_BLOCK_MAX_ROWS_ABOVE) && (top + tb->line_h > limit);
       num_rows++, top -= tb->line_h) {
    ranges[num_rows] = prv_row_range(tb, top);
    if (ranges[num_rows].size_w < MIN_ROW_WIDTH) {
      break;
    }
  }
  // The rows only change when the line does or one of them gets wider or narrower
  const bool exhausted = (tb->num_above == 0) || (tb->above_starts[tb->num_above - 1] == 0);
  bool same = (tb->above_for == line);
  for (uint8_t row = 0; same && (row < num_rows); row++) {
    same = (row < tb->num_above) ? (tb->above_ranges[row].size_w == ranges[row].size_w) : exhausted;
  }
  if (same) {
    return;
  }
  tb->above_for = line;
  tb->num_above = 0;
  uint16_t end = prv_line_start(tb, line);
  while ((end > 0) && (tb->num_above < num_rows)) {
    tb->above_ranges[tb->num_above] = ranges[tb->num_above];
    end = prv_wrap_backward(tb, ctx, end, ranges[tb->num_above].size_w);
    tb->above_starts[tb->num_above++] = end;
  }
}

NotificationTextBlock *notification_text_block_create(GContext *ctx, const char *text, GFont font,
                                                      int16_t line_spacing_delta,
                                                      GTextAlignment alignment, uint8_t inset,
                                                      int16_t anchor_top, int16_t rest_top) {
  NotificationTextBlock *tb = applib_zalloc(sizeof(NotificationTextBlock));
  const size_t length = strlen(text);
  char *copy = tb ? applib_malloc(length + 1) : nullptr;
  if (!copy) {
    applib_free(tb);
    return nullptr;
  }
  memcpy(copy, text, length + 1);
  const int16_t font_h = fonts_get_font_height(font);
  *tb = (NotificationTextBlock){
    .text = copy,
    .length = length,
    .font = font,
    .font_h = font_h,
    .line_h = font_h + line_spacing_delta,
    .line_spacing_delta = line_spacing_delta,
    .alignment = alignment,
    .inset = inset,
    .rest_top = rest_top,
    .below.callback = prv_below_perimeter,
    .above_for = -1,
  };
  static const GSize s_display = {PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT};
  const GRangeVertical anchor = {anchor_top + font_h / 2, font_h - font_h / 2};
  tb->block = perimeter_text_block(&s_display, anchor, inset);

  // Wrap it once at the block's width
  const uint16_t chunk = 16;
  uint16_t start = 0;
  while (start < tb->length) {
    if ((tb->num_lines % chunk) == 0) {
      uint16_t *starts = applib_realloc(tb->line_starts, (tb->num_lines + chunk) * sizeof(*starts));
      if (!starts) {
        notification_text_block_destroy(tb);
        return nullptr;
      }
      tb->line_starts = starts;
    }
    tb->line_starts[tb->num_lines++] = start;
    start = prv_wrap_forward(tb, ctx, start, tb->block.range.size_w);
    while ((start < tb->length) && (tb->text[start] == ' ')) {
      start++;
    }
    if ((start < tb->length) && (tb->text[start] == '\n')) {
      start++;
    }
  }
  return tb;
}

void notification_text_block_destroy(NotificationTextBlock *tb) {
  if (!tb) {
    return;
  }
  applib_free(tb->line_starts);
  applib_free(tb->text);
  applib_free(tb);
}

int16_t notification_text_block_draw(NotificationTextBlock *tb, GContext *ctx, GPoint origin,
                                     int16_t scroll, int16_t top, bool render) {
  const uint16_t num_lines = tb->num_lines;
  const int16_t line_h = tb->line_h;
  const int16_t first_top = tb->rest_top - scroll;
  const int16_t rows_top = tb->block.rows.origin_y;
  const int16_t rows_bottom = prv_rows_bottom(&tb->block.rows);

  // The block's lines: those whose part that text flow fits is inside its rows
  uint16_t first = 0;
  while ((first < num_lines) && (first_top + first * line_h + tb->font_h / 2 < rows_top)) {
    first++;
  }
  uint16_t below = first;
  while ((below < num_lines) && (first_top + below * line_h + tb->font_h <= rows_bottom)) {
    below++;
  }

  if (render) {
    prv_wrap_above(tb, ctx, first, first_top + (first - 1) * line_h, top);
    // While what is above the message shows, the rows above the block can't take more room than
    // its lines there do: they then keep their width, where it about fits.
    const bool above_shows = (first_top > top + line_h / 2);
    const bool rewrap =
        !above_shows || ((tb->num_above <= first) &&
                         ((tb->num_above == 0) || (tb->above_starts[tb->num_above - 1] == 0)));
    const int num_above = rewrap ? tb->num_above : first;
    for (int row = 0; row < num_above; row++) {
      const int line = first - 1 - row;
      // Beneath the status bar, or over what is left of what is above the message
      if ((first_top + (line + 1) * line_h <= top) || ((line < 0) && (first_top > top))) {
        break;
      }
      const int16_t y = origin.y + line * line_h;
      if (rewrap) {
        const GRangeHorizontal range = tb->above_ranges[row];
        const GRect box = {{range.origin_x, y}, {range.size_w, tb->font_h}};
        const uint16_t to = row ? tb->above_starts[row - 1] : prv_line_start(tb, first);
        prv_row(tb, ctx, tb->above_starts[row], to, &box, true /* render */);
      } else {
        const GRect box = {{tb->block.range.origin_x, y}, {tb->block.range.size_w, tb->font_h}};
        prv_row(tb, ctx, prv_line_start(tb, line), prv_line_start(tb, line + 1), &box, true);
      }
    }
    for (uint16_t line = first; line < below; line++) {
      const GRect box = {
        {tb->block.range.origin_x, origin.y + line * line_h},
        {tb->block.range.size_w, tb->font_h}
      };
      prv_row(tb, ctx, prv_line_start(tb, line), prv_line_start(tb, line + 1), &box, true);
    }
  }

  if (below >= num_lines) {
    return num_lines * line_h;
  }
  // Below the block the text flows inside the display where it is
  const int16_t flow_top = first_top + below * line_h;
  TextLayoutExtended layout = {
    .line_spacing_delta = tb->line_spacing_delta,
    .flow_data = {
      .perimeter = {.impl = &tb->below, .inset = tb->inset},
      // One page as tall as can be, so that lines are fitted where they are on screen
      .paging = {
        .origin_on_screen = {0, flow_top},
        .page_on_screen = {INT16_MIN / 2, INT16_MAX},
      },
    },
  };
  const GRect box = {{0, origin.y + below * line_h}, {PBL_DISPLAY_WIDTH, INT16_MAX / 2}};
  const char *text = tb->text + prv_line_start(tb, below);
  if (render) {
    graphics_draw_text(ctx, text, tb->font, box, GTextOverflowModeWordWrap, tb->alignment,
                       (TextLayout *)&layout);
  } else {
    graphics_text_layout_get_max_used_size(ctx, text, tb->font, box, GTextOverflowModeWordWrap,
                                           tb->alignment, (TextLayout *)&layout);
  }
  return below * line_h + layout.max_used_size.h;
}
#endif
