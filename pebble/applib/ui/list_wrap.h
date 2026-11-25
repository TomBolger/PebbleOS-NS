/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file list_wrap.h
//! A list's wrap around on round displays: past its last row the list carries on the way Down
//! moves it, round an orbit off the screen, and comes back in at its first row from the other
//! edge, as if it were a loop; Up past the first row is the mirror. The list is drawn twice
//! meanwhile: the lap it lands on, resting where it does once the wrap is done and shifted from
//! there, and the lap it leaves, a loop's length behind it. Both move in one step, with the
//! curve and duration of the round lists' steps.

#include <stdbool.h>
#include <stdint.h>

#include <applib/ui/animation.h>

//! Where the two laps of a list are drawn as a wrap starts
typedef struct ListWrapLaps {
  //! How far below where it rests the lap the list lands on is drawn as the wrap starts, above if
  //! negative. The wrap moves it from there to where it rests.
  int16_t from_shift;
  //! Where the lap the list leaves is drawn relative to the one it lands on: a loop's length above
  //! it going down, below it going up
  int16_t loop;
} ListWrapLaps;

//! Where the laps of a list wrapping \a down (or up) are drawn as the wrap starts. The content,
//! \a content_h tall in a frame \a frame_h tall, is at the scroll offset \a from_offset as the wrap
//! starts and rests at \a to_offset once done. The loop is as short as keeps the lap the list
//! lands on off the frame as it starts and the lap it leaves off the frame once done, so both
//! show exactly what the list shows at rest, and never shorter than the content.
ListWrapLaps list_wrap_laps(bool down, int16_t frame_h, int16_t content_h, int16_t from_offset,
                            int16_t to_offset);

//! How long a wrap takes
uint32_t list_wrap_duration(void);

//! The wrap's curve: \a from to \a to, \a normalized of the way through the wrap
int64_t list_wrap_curve(int32_t normalized, int64_t from, int64_t to);

typedef struct ListWrap ListWrap;

//! Called at every frame of a wrap, and once more as it lands
typedef void (*ListWrapUpdateHandler)(ListWrap *wrap, void *context);

//! Called once the wrap has landed, after its last update. The wrap may be freed from here.
typedef void (*ListWrapStoppedHandler)(ListWrap *wrap, void *context);

//! A wrap in progress
struct ListWrap {
  Animation *animation;
  ListWrapLaps laps;
  AnimationProgress progress;
  ListWrapUpdateHandler update;
  ListWrapStoppedHandler stopped;
  void *context;
};

//! Starts \a wrap with \a laps, calling \a update at every frame and \a stopped once it has landed
//! @return false if it could not start; \a update and \a stopped are not called then
bool list_wrap_start(ListWrap *wrap, ListWrapLaps laps, ListWrapUpdateHandler update,
                     ListWrapStoppedHandler stopped, void *context);

//! Lands \a wrap right away, calling its handlers, which may free it
void list_wrap_stop(ListWrap *wrap);

//! How far from where it rests the lap the list lands on is drawn now
int16_t list_wrap_shift(const ListWrap *wrap);

//! Something else moving with the wrap, such as the highlight: \a from to \a to, as far as the
//! wrap has gone
int16_t list_wrap_interpolate(const ListWrap *wrap, int16_t from, int16_t to);
