/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <applib/graphics/gtypes.h>

typedef struct GPerimeter GPerimeter;

//! @internal
typedef GRangeHorizontal (*GPerimeterCallback)(const GPerimeter *perimeter, const GSize *ctx_size,
                                               GRangeVertical vertical_range, uint16_t inset);

//! @internal
typedef struct GPerimeter {
  GPerimeterCallback callback;
} GPerimeter;

//! @internal
extern const GPerimeter *const g_perimeter_for_display;

#if PBL_ROUND
//! @internal
//! Lines of text of one width on a round display: from an anchor row down to the row where the
//! display is as wide again.
typedef struct {
  //! Where the lines run: as wide as the display is at the anchor, inside the inset
  GRangeHorizontal range;
  //! Rows the part of a line that text flow fits (see perimeter callbacks) stays within
  GRangeVertical rows;
} GTextBlock;

//! @internal
//! The block of text anchored at the given row, the part of its first line that text flow fits.
GTextBlock perimeter_text_block(const GSize *ctx_size, GRangeVertical anchor, uint16_t inset);
#endif
