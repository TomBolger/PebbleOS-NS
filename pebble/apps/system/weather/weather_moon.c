/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "weather_moon.h"

#include <string.h>

// 2000-01-01 12:00 UTC (J2000.0) as a unix timestamp
#define J2000_UNIX 946728000

// Pixels of outline drawn around the lit part
#define MOON_OUTLINE 2

// Dots in the dotted circle tracing the dark limb
#define MOON_LIMB_DOTS 12

// Largest radius the pixel grid below has room for
#define MOON_MAX_RADIUS 12
#define MOON_GRID_HALF  (MOON_MAX_RADIUS + MOON_OUTLINE)
#define MOON_GRID_SIZE  (MOON_GRID_HALF * 2 + 1)

typedef enum {
  MoonPixelNone = 0,
  MoonPixelLimbDot,
  MoonPixelOutline,
  MoonPixelLit,
  MoonPixelMare,
} MoonPixel;

// A few of the large near-side maria, seen from the northern hemisphere with north up. x/y/r
// are fractions of the moon's radius (y is down), simplified into round blobs that stay
// legible at ~15 px across.
typedef struct {
  float x, y, r;
} Mare;

static const Mare s_maria[] = {
  {-0.30f, -0.34f, 0.26f}, // Imbrium
  {0.28f, -0.22f, 0.22f},  // Serenitatis / Tranquillitatis
  {-0.56f, 0.20f, 0.22f},  // Procellarum
  {0.64f, -0.36f, 0.12f},  // Crisium
  {-0.06f, 0.50f, 0.14f},  // Nubium
};

// Single-precision only: there is no FPU, and soft doubles cost flash.
static float prv_wrap_degrees(float deg) {
  const int32_t turns = (int32_t)(deg / 360.0f);
  deg -= (float)turns * 360.0f;
  if (deg < 0) {
    deg += 360.0f;
  }
  return deg;
}

static int32_t prv_trig_angle(float deg) {
  return (int32_t)(prv_wrap_degrees(deg) * ((float)TRIG_MAX_ANGLE / 360.0f));
}

static float prv_sin_deg(float deg) {
  return (float)sin_lookup(prv_trig_angle(deg)) / (float)TRIG_MAX_RATIO;
}

static float prv_cos_deg(float deg) {
  return (float)cos_lookup(prv_trig_angle(deg)) / (float)TRIG_MAX_RATIO;
}

// base + rate * days, wrapped to [0, 360). Whole and fractional days are scaled separately so a
// float keeps enough precision decades from J2000.
static float prv_mean_angle(float base, float rate, int32_t whole_days, float day_fraction) {
  return prv_wrap_degrees(base + prv_wrap_degrees(rate * (float)whole_days) +
                          rate * day_fraction);
}

static float prv_sqrt(float x) {
  if (x <= 0) {
    return 0;
  }
  float guess = (x > 1) ? x / 2 : 1;
  for (int i = 0; i < 8; i++) {
    guess = (guess + x / guess) / 2;
  }
  return guess;
}

static int prv_round(float x) {
  return (int)((x < 0) ? x - 0.5f : x + 0.5f);
}

float weather_moon_elongation(time_t utc_time) {
  const int32_t seconds = (int32_t)(utc_time - J2000_UNIX);
  const int32_t whole_days = seconds / 86400;
  const float day_fraction = (float)(seconds % 86400) / 86400.0f;

  // Mean elongation of the moon, mean anomalies of the sun and of the moon
  const float d = prv_mean_angle(297.8501921f, 12.19074912f, whole_days, day_fraction);
  const float m = prv_mean_angle(357.5291092f, 0.98560028f, whole_days, day_fraction);
  const float mp = prv_mean_angle(134.9633964f, 13.06499295f, whole_days, day_fraction);

  // Largest periodic terms of the moon's longitude relative to the sun's
  const float elongation = d + 6.289f * prv_sin_deg(mp) - 2.100f * prv_sin_deg(m) +
                           1.274f * prv_sin_deg(2 * d - mp) + 0.658f * prv_sin_deg(2 * d) +
                           0.214f * prv_sin_deg(2 * mp) - 0.110f * prv_sin_deg(d);
  return prv_wrap_degrees(elongation);
}

void weather_moon_draw(GContext *ctx, GPoint center, int radius, float elongation,
                       GColor outline) {
  if (radius > MOON_MAX_RADIUS) {
    radius = MOON_MAX_RADIUS;
  }
  const int half = radius + MOON_OUTLINE;

  // grid[y][x] for pixels at center + (x - MOON_GRID_HALF, y - MOON_GRID_HALF)
  uint8_t grid[MOON_GRID_SIZE][MOON_GRID_SIZE];
  memset(grid, MoonPixelNone, sizeof(grid));

  // The terminator is a half ellipse: on each row of the disk, of half width w, it sits at
  // x = w * cos(elongation). Waxing moons are lit on its right, waning ones on its left.
  const bool waxing = elongation < 180.0f;
  const float terminator = prv_cos_deg(elongation);
  // The lit disk is a touch smaller than radius so that, with its outline, it fills radius.
  const float disk_r2 = (float)(radius * radius - radius);

  for (int y = -radius; y <= radius; y++) {
    const float w = prv_sqrt(disk_r2 - (float)(y * y));
    for (int x = -radius; x <= radius; x++) {
      if ((float)(x * x + y * y) > disk_r2) {
        continue;
      }
      const bool lit = waxing ? (x > w * terminator) : (x < -w * terminator);
      if (!lit) {
        continue;
      }
      uint8_t px = MoonPixelLit;
      for (unsigned int i = 0; i < sizeof(s_maria) / sizeof(s_maria[0]); i++) {
        const float dx = x - s_maria[i].x * radius;
        const float dy = y - s_maria[i].y * radius;
        const float mr = s_maria[i].r * radius;
        if (dx * dx + dy * dy <= mr * mr) {
          px = MoonPixelMare;
          break;
        }
      }
      grid[y + MOON_GRID_HALF][x + MOON_GRID_HALF] = px;
    }
  }

  // Outline: any unlit pixel within MOON_OUTLINE of a lit one
  for (int y = -half; y <= half; y++) {
    for (int x = -half; x <= half; x++) {
      uint8_t *px = &grid[y + MOON_GRID_HALF][x + MOON_GRID_HALF];
      if (*px != MoonPixelNone) {
        continue;
      }
      for (int dy = -MOON_OUTLINE; (dy <= MOON_OUTLINE) && !*px; dy++) {
        for (int dx = -MOON_OUTLINE; dx <= MOON_OUTLINE; dx++) {
          const int nx = x + dx;
          const int ny = y + dy;
          if ((dx * dx + dy * dy > MOON_OUTLINE * MOON_OUTLINE) || (nx < -radius) ||
              (nx > radius) || (ny < -radius) || (ny > radius)) {
            continue;
          }
          const uint8_t n = grid[ny + MOON_GRID_HALF][nx + MOON_GRID_HALF];
          if ((n == MoonPixelLit) || (n == MoonPixelMare)) {
            *px = MoonPixelOutline;
            break;
          }
        }
      }
    }
  }

  // Dotted circle around the whole disk, left showing only on the dark side
  const float limb_r = radius - 0.5f;
  for (int i = 0; i < MOON_LIMB_DOTS; i++) {
    const int32_t angle = i * TRIG_MAX_ANGLE / MOON_LIMB_DOTS;
    const int x = prv_round(limb_r * sin_lookup(angle) / TRIG_MAX_RATIO);
    const int y = prv_round(-limb_r * cos_lookup(angle) / TRIG_MAX_RATIO);
    uint8_t *px = &grid[y + MOON_GRID_HALF][x + MOON_GRID_HALF];
    if (*px == MoonPixelNone) {
      *px = MoonPixelLimbDot;
    }
  }

  const GColor colors[] = {
    [MoonPixelNone] = GColorClear,
    [MoonPixelLimbDot] = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite),
    [MoonPixelOutline] = outline,
    [MoonPixelLit] = GColorWhite,
    [MoonPixelMare] = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite),
  };

  // Paint the grid as horizontal runs of each colour
  for (int y = -half; y <= half; y++) {
    const uint8_t *row = grid[y + MOON_GRID_HALF];
    int x = -half;
    while (x <= half) {
      const uint8_t px = row[x + MOON_GRID_HALF];
      const int start = x;
      while ((x <= half) && (row[x + MOON_GRID_HALF] == px)) {
        x++;
      }
      if (px == MoonPixelNone) {
        continue;
      }
      graphics_context_set_fill_color(ctx, colors[px]);
      graphics_fill_rect(ctx, GRect(center.x + start, center.y + y, x - start, 1), 0,
                         GCornerNone);
    }
  }
}
