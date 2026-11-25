/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_card.h
//! The cards of the round Health app, one per section of the orbit.

#include <applib/graphics/gtypes.h>

//! The sections on the orbit, from the top
typedef enum HealthOrbitSection {
  HealthOrbitSectionToday,
  HealthOrbitSectionSteps,
  HealthOrbitSectionSleep,
  HealthOrbitSectionCount,
} HealthOrbitSection;

//! Color of the sidebar on a section's card
GColor health_orbit_section_color(HealthOrbitSection section);

//! Background color of a section's card
GColor health_orbit_section_bg_color(HealthOrbitSection section);
