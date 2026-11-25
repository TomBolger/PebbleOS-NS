/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "list_wrap.h"

#include <pbl/util/math.h>

#include <applib/ui/animation_interpolate.h>

//! Linear frames between the Moook ease in and out, as the round lists' steps have, so the rows
//! are seen travelling round the orbit instead of cutting to the other end
#define LIST_WRAP_FRAMES_MID 3

ListWrapLaps list_wrap_laps(bool down, int16_t frame_h, int16_t content_h, int16_t from_offset,
                            int16_t to_offset) {
  // Going down, the lap landed on comes in from below: it must start below the frame, and the lap
  // left must end above it. Going up, the reverse.
  const int16_t rest_top = down ? to_offset : from_offset;
  const int16_t rest_bottom = down ? from_offset : to_offset;
  const int16_t length = PBL_MAX(PBL_MAX(frame_h - rest_bottom, rest_top + content_h), content_h);
  return (ListWrapLaps){
    .from_shift = (int16_t)(from_offset - to_offset + (down ? length : -length)),
    .loop = down ? -length : length,
  };
}

uint32_t list_wrap_duration(void) {
  return interpolate_moook_soft_duration(LIST_WRAP_FRAMES_MID);
}

int64_t list_wrap_curve(int32_t normalized, int64_t from, int64_t to) {
  return interpolate_moook_soft(normalized, from, to, LIST_WRAP_FRAMES_MID);
}

static void prv_update(Animation *animation, const AnimationProgress progress) {
  ListWrap *wrap = animation_get_context(animation);
  wrap->progress = progress;
  wrap->update(wrap, wrap->context);
}

static void prv_land(ListWrap *wrap) {
  wrap->animation = nullptr;
  wrap->progress = ANIMATION_NORMALIZED_MAX;
  wrap->update(wrap, wrap->context);
  wrap->stopped(wrap, wrap->context);
}

static void prv_stopped(Animation *animation, bool finished, void *context) {
  ListWrap *wrap = context;
  // A wrap stopped early has landed already
  if (wrap->animation == animation) {
    prv_land(wrap);
  }
}

bool list_wrap_start(ListWrap *wrap, ListWrapLaps laps, ListWrapUpdateHandler update,
                     ListWrapStoppedHandler stopped, void *context) {
  static const AnimationImplementation s_implementation = {
    .update = prv_update,
  };
  Animation *animation = animation_create();
  if (!animation) {
    return false;
  }
  *wrap = (ListWrap){
    .animation = animation,
    .laps = laps,
    .update = update,
    .stopped = stopped,
    .context = context,
  };
  animation_set_implementation(animation, &s_implementation);
  animation_set_duration(animation, list_wrap_duration());
  animation_set_curve(animation, AnimationCurveLinear);
  animation_set_handlers(animation, (AnimationHandlers){.stopped = prv_stopped}, wrap);
  if (!animation_schedule(animation)) {
    animation_destroy(animation);
    wrap->animation = nullptr;
    return false;
  }
  return true;
}

void list_wrap_stop(ListWrap *wrap) {
  Animation *animation = wrap->animation;
  if (animation) {
    wrap->animation = nullptr;
    animation_unschedule(animation);
    prv_land(wrap);
  }
}

int16_t list_wrap_shift(const ListWrap *wrap) {
  return list_wrap_interpolate(wrap, wrap->laps.from_shift, 0);
}

int16_t list_wrap_interpolate(const ListWrap *wrap, int16_t from, int16_t to) {
  return (int16_t)list_wrap_curve(wrap->progress, from, to);
}
