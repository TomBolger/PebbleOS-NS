/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <string.h>

#include <pbl/services/timeline/notification_text_block.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <applib/applib_malloc.auto.h>
#include <applib/fonts/fonts.h>
#include <applib/graphics/gcontext.h>

#if PBL_ROUND

//! Rows narrower than this, at the very top and bottom of the display, are too narrow to show
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

//! Rows a scroll step may still show beyond the screen either way, and that a drag moves into
//! view before it is laid out again
#define MARGIN_ROWS 4

static const GSize s_display = {PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT};

//! Where a line runs, given the part of it that text flow fits: inside the display. A line too
//! narrow there, at the display's very top or bottom, or past the display runs as wide as the
//! block, so that the text has a height and can be dragged into view.
static GRangeHorizontal prv_room(const NotificationTextBlock *tb, GRangeVertical rows) {
  const GRangeHorizontal room =
      g_perimeter_for_display->callback(g_perimeter_for_display, &s_display, rows, tb->inset);
  return (room.size_w >= MIN_ROW_WIDTH) ? room : tb->block.range;
}

//! Where a line whose top is at `top` on screen runs; `hidden` tells whether any of it is on the
//! display while it is too narrow to show there
static GRangeHorizontal prv_row_range(const NotificationTextBlock *tb, int16_t top, bool *hidden) {
  // Text flow fits the lower half of each line
  const GRangeVertical rows = {top + tb->font_h / 2, tb->font_h - tb->font_h / 2};
  const GRangeHorizontal room =
      g_perimeter_for_display->callback(g_perimeter_for_display, &s_display, rows, tb->inset);
  const bool fits = (room.size_w >= MIN_ROW_WIDTH);
  *hidden = !fits && (top < s_display.h) && (top + tb->line_h > 0);
  return fits ? room : tb->block.range;
}

static GRangeHorizontal prv_below_perimeter(const GPerimeter *perimeter, const GSize *ctx_size,
                                            GRangeVertical rows, uint16_t inset) {
  const NotificationTextBlock *tb =
      (const NotificationTextBlock *)((const char *)perimeter -
                                      offsetof(NotificationTextBlock, below));
  return prv_room(tb, rows);
}

NotificationTextBlock *notification_text_block_create(GContext *ctx, const char *text, GFont font,
                                                      int16_t line_spacing_delta,
                                                      GTextAlignment alignment, uint8_t inset,
                                                      int16_t anchor_top, int16_t rest_top,
                                                      int16_t top) {
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
    .top = top,
    .below.callback = prv_below_perimeter,
    .layouts = {{.scroll = INT16_MIN}, {.scroll = INT16_MIN}},
  };
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

//! The block's lines as the message rests at `scroll`: `first` to `below`, those whose part that
//! text flow fits is inside the block's rows
static void prv_block_lines(const NotificationTextBlock *tb, int16_t scroll, uint16_t *first,
                            uint16_t *below) {
  const int16_t first_top = tb->rest_top - scroll;
  const GRangeVertical *rows = &tb->block.rows;
  uint16_t line = 0;
  while ((line < tb->num_lines) &&
         (first_top + line * tb->line_h + tb->font_h / 2 < rows->origin_y)) {
    line++;
  }
  *first = line;
  while ((line < tb->num_lines) &&
         (first_top + line * tb->line_h + tb->font_h <= rows->origin_y + rows->size_h)) {
    line++;
  }
  *below = line;
}

int16_t notification_text_block_measure(NotificationTextBlock *tb, GContext *ctx, int16_t scroll) {
  uint16_t first;
  uint16_t below;
  prv_block_lines(tb, scroll, &first, &below);
  if (below >= tb->num_lines) {
    return tb->num_lines * tb->line_h;
  }
  TextLayoutExtended layout = {
    .line_spacing_delta = tb->line_spacing_delta,
    .flow_data = {
      .perimeter = {.impl = &tb->below, .inset = tb->inset},
      // One page as tall as can be, so that lines are fitted where they are on screen
      .paging = {
        .origin_on_screen = {0, tb->rest_top - scroll + below * tb->line_h},
        .page_on_screen = {INT16_MIN / 2, INT16_MAX},
      },
    },
  };
  const GRect box = {GPointZero, {PBL_DISPLAY_WIDTH, INT16_MAX / 2}};
  graphics_text_layout_get_max_used_size(ctx, tb->text + prv_line_start(tb, below), tb->font, box,
                                         GTextOverflowModeWordWrap, tb->alignment,
                                         (TextLayout *)&layout);
  return below * tb->line_h + layout.max_used_size.h;
}

static bool prv_add_row(NotificationTextLayout *layout, uint16_t start, uint16_t end, int16_t y,
                        GRangeHorizontal range, bool hidden) {
  if (layout->num_rows >= NOTIFICATION_TEXT_BLOCK_MAX_ROWS) {
    return false;
  }
  layout->rows[layout->num_rows++] = (NotificationTextRow){
    .start = start,
    .end = end,
    .y = y,
    .x = range.origin_x,
    .w = range.size_w,
    .hidden = hidden,
  };
  return true;
}

//! Rewraps the text before the block's first line into the rows above it, from where it starts
//! back, up to a few rows above the screen. While what is above the message shows, rows can't
//! run into it: then the lines above the block keep their width, where they about fit, unless
//! the text fits below it rewrapped.
static void prv_lay_out_above(NotificationTextBlock *tb, GContext *ctx,
                              NotificationTextLayout *layout, uint16_t first, int16_t first_top) {
  uint16_t starts[NOTIFICATION_TEXT_BLOCK_MAX_ROWS / 2];
  GRangeHorizontal ranges[NOTIFICATION_TEXT_BLOCK_MAX_ROWS / 2];
  bool hidden[NOTIFICATION_TEXT_BLOCK_MAX_ROWS / 2];
  int num_rows = 0;
  uint16_t end = prv_line_start(tb, first);
  const bool above_shows = (first_top > tb->top);
  for (int line = first - 1;
       (end > 0) && (num_rows < (int)PBL_ARRAY_SIZE(starts)) &&
       (first_top + (line + 1 + MARGIN_ROWS) * tb->line_h > tb->top) && (!above_shows || line >= 0);
       line--) {
    const GRangeHorizontal range =
        prv_row_range(tb, first_top + line * tb->line_h, &hidden[num_rows]);
    end = prv_wrap_backward(tb, ctx, end, range.size_w);
    starts[num_rows] = end;
    ranges[num_rows++] = range;
  }
  if ((end > 0) && above_shows && (first_top > tb->top + tb->line_h / 2)) {
    for (uint16_t line = 0; line < first; line++) {
      prv_add_row(layout, prv_line_start(tb, line), prv_line_start(tb, line + 1), line * tb->line_h,
                  tb->block.range, false /* hidden */);
    }
    return;
  }
  for (int row = num_rows - 1; row >= 0; row--) {
    const uint16_t row_end = row ? starts[row - 1] : prv_line_start(tb, first);
    prv_add_row(layout, starts[row], row_end, (first - 1 - row) * tb->line_h, ranges[row],
                hidden[row]);
  }
}

//! Wraps the text from the first line below the block into rows that fit inside the display, down
//! to a few rows below the screen.
static void prv_lay_out_below(NotificationTextBlock *tb, GContext *ctx,
                              NotificationTextLayout *layout, uint16_t below, int16_t first_top) {
  uint16_t start = prv_line_start(tb, below);
  int16_t y = below * tb->line_h;
  while ((start < tb->length) && (first_top + y < s_display.h + MARGIN_ROWS * tb->line_h)) {
    bool hidden;
    const GRangeHorizontal range = prv_row_range(tb, first_top + y, &hidden);
    const uint16_t end = prv_wrap_forward(tb, ctx, start, range.size_w);
    if (!prv_add_row(layout, start, end, y, range, hidden)) {
      return;
    }
    y += tb->line_h;
    start = end;
    while ((start < tb->length) && (tb->text[start] == ' ')) {
      start++;
    }
    if ((start < tb->length) && (tb->text[start] == '\n')) {
      start++;
    }
  }
  if (start >= tb->length) {
    layout->height = y;
  }
}

static void prv_lay_out(NotificationTextBlock *tb, GContext *ctx, NotificationTextLayout *layout,
                        int16_t scroll) {
  *layout = (NotificationTextLayout){.scroll = scroll};
  const int16_t first_top = tb->rest_top - scroll;
  uint16_t first;
  uint16_t below;
  prv_block_lines(tb, scroll, &first, &below);
  prv_lay_out_above(tb, ctx, layout, first, first_top);
  for (uint16_t line = first; line < below; line++) {
    prv_add_row(layout, prv_line_start(tb, line), prv_line_start(tb, line + 1), line * tb->line_h,
                tb->block.range, false /* hidden */);
  }
  if (below >= tb->num_lines) {
    layout->height = tb->num_lines * tb->line_h;
  } else {
    prv_lay_out_below(tb, ctx, layout, below, first_top);
  }
}

const NotificationTextLayout *notification_text_block_lay_out(NotificationTextBlock *tb,
                                                              GContext *ctx, int16_t scroll,
                                                              bool show) {
  uint8_t slot = tb->shown;
  if (tb->layouts[slot].scroll != scroll) {
    // The other slot, unless nothing is shown yet
    if (tb->layouts[slot].scroll != INT16_MIN) {
      slot = !slot;
    }
    if (tb->layouts[slot].scroll != scroll) {
      prv_lay_out(tb, ctx, &tb->layouts[slot], scroll);
    }
  }
  if (show) {
    tb->shown = slot;
  }
  return &tb->layouts[slot];
}

const NotificationTextLayout *notification_text_block_shown(const NotificationTextBlock *tb) {
  const NotificationTextLayout *layout = &tb->layouts[tb->shown];
  return (layout->scroll != INT16_MIN) ? layout : nullptr;
}

void notification_text_block_draw(NotificationTextBlock *tb, GContext *ctx, GPoint origin,
                                  int16_t scroll) {
  const NotificationTextLayout *layout = notification_text_block_shown(tb);
  if (!layout) {
    return;
  }
  // Rows too narrow to show where they rest show once dragged away, not in a step's lead-in
  const bool dragged = (PBL_ABS(scroll - layout->scroll) > tb->line_h);
  const GRect *clip = &ctx->draw_state.clip_box;
  const int16_t to_global = ctx->draw_state.drawing_box.origin.y + origin.y;
  for (uint8_t i = 0; i < layout->num_rows; i++) {
    const NotificationTextRow *row = &layout->rows[i];
    const int16_t y = to_global + row->y;
    if ((row->hidden && !dragged) || (y + tb->line_h <= clip->origin.y) ||
        (y >= clip->origin.y + clip->size.h)) {
      continue;
    }
    const GRect box = {{row->x, origin.y + row->y}, {row->w, tb->font_h}};
    prv_row(tb, ctx, row->start, row->end, &box, true /* render */);
  }
}
#endif
