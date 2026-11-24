/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "app_glance_service.h"

#include <applib/preferred_content_size.h>
#include <process_management/app_menu_data_source.h>
#include <shell/system_theme_style.h>

typedef SystemThemeLauncherStyle LauncherMenuLayerStyle;

typedef struct LauncherMenuLayer {
  Layer container_layer;
  MenuLayer menu_layer;
#if PBL_ROUND
  Layer up_arrow_layer;
  Layer down_arrow_layer;
#endif
  PreferredContentSize content_size;
  AppMenuDataSource *data_source;
  LauncherAppGlanceService glance_service;
  bool selection_animations_enabled;
  AppInstallId app_to_launch_after_next_render;
} LauncherMenuLayer;

typedef struct LauncherMenuLayerSelectionState {
  int16_t scroll_offset_y;
  uint16_t row_index;
  //! Content size the scroll offset was captured with
  PreferredContentSize content_size;
} LauncherMenuLayerSelectionState;

//! @return The style for the user's preferred content size
const LauncherMenuLayerStyle *launcher_menu_layer_get_style(void);

#if PBL_ROUND
//! Round: where the first row, a header at the top of the screen, has its title and subtitle,
//! under its icon, and how far down the row the bubble highlighting it reaches
typedef struct LauncherRoundHeaderLayout {
  int16_t title_top;
  int16_t subtitle_top;
  int16_t bubble_depth;
} LauncherRoundHeaderLayout;

//! Round: the header's layout for a title \a title_h and a subtitle \a subtitle_h tall: the icon
//! box near the top of the screen, the title and subtitle tight under it, and the bubble just
//! around them
LauncherRoundHeaderLayout launcher_menu_layer_round_header_layout(int16_t title_h,
                                                                  int16_t subtitle_h);

//! Round: the header's layout with \a style's fonts
LauncherRoundHeaderLayout launcher_menu_layer_round_header_layout_for_style(
    const LauncherMenuLayerStyle *style);
#endif

void launcher_menu_layer_init(LauncherMenuLayer *launcher_menu_layer,
                              AppMenuDataSource *data_source);

Layer *launcher_menu_layer_get_layer(LauncherMenuLayer *launcher_menu_layer);

void launcher_menu_layer_set_click_config_onto_window(LauncherMenuLayer *launcher_menu_layer,
                                                      Window *window);

void launcher_menu_layer_reload_data(LauncherMenuLayer *launcher_menu_layer);

//! Re-initialize the launcher menu layer if the user's preferred content size changed since it was
//! initialized, keeping the current selection.
void launcher_menu_layer_update_content_size(LauncherMenuLayer *launcher_menu_layer);

void launcher_menu_layer_set_selection_state(LauncherMenuLayer *launcher_menu_layer,
                                             const LauncherMenuLayerSelectionState *new_state);

void launcher_menu_layer_get_selection_state(const LauncherMenuLayer *launcher_menu_layer,
                                             LauncherMenuLayerSelectionState *state_out);

void launcher_menu_layer_get_selection_vertical_range(const LauncherMenuLayer *launcher_menu_layer,
                                                      GRangeVertical *vertical_range_out);

void launcher_menu_layer_set_selection_animations_enabled(LauncherMenuLayer *launcher_menu_layer,
                                                          bool enabled);

void launcher_menu_layer_deinit(LauncherMenuLayer *launcher_menu_layer);
