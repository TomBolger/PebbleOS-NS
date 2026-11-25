/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file orbit_view.h
//! The round Health app: a card per section, chosen on an orbit round the timeline's sidebar.

#include "data.h"

typedef struct HealthOrbitView HealthOrbitView;

//! Creates the view, on the section its launch arguments ask for
HealthOrbitView *health_orbit_view_create(HealthData *health_data);

void health_orbit_view_destroy(HealthOrbitView *view);

void health_orbit_view_push(HealthOrbitView *view);

//! Redraws the view, for new data
void health_orbit_view_mark_dirty(HealthOrbitView *view);
