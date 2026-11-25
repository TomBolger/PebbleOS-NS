/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "orbit_card.h"

#if PBL_ROUND

GColor health_orbit_section_color(HealthOrbitSection section) {
  switch (section) {
    case HealthOrbitSectionSteps:
      return GColorIslamicGreen;
    case HealthOrbitSectionSleep:
      return GColorCobaltBlue;
    case HealthOrbitSectionToday:
    case HealthOrbitSectionCount:
      break;
  }
  return GColorVividCerulean;
}

GColor health_orbit_section_bg_color(HealthOrbitSection section) {
  return (section == HealthOrbitSectionSleep) ? GColorOxfordBlue : GColorBlack;
}

#endif
