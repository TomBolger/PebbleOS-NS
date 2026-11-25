/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <process_management/app_install_types.h>
#include <process_management/pebble_process_md.h>

//! Get the very first app we should run at startup.
const PebbleProcessMd *system_app_state_machine_system_start(void);

//! Retrieve the app that was last launched prior to the current one.
AppInstallId system_app_state_machine_get_last_registered_app(void);

//! Arguments to launch the app system_app_state_machine_get_last_registered_app() returns with.
const void *system_app_state_machine_get_last_registered_app_args(void);

//! Closing the app launched next returns to an app, launched with arguments, instead of to the
//! watchface or the launcher it is rooted in.
//! @param app_id The app to return to.
//! @param args Its launch arguments, which must outlive its launch.
void system_app_state_machine_return_next_app_to(AppInstallId app_id, const void *args);

//! Get the app we should launch after an app has crashed or has been force quit.
const PebbleProcessMd *system_app_state_machine_get_default_app(void);

//! Tell the state machine we just launched an app.
void system_app_state_machine_register_app_launch(AppInstallId app_id);

//! Stop the normal state machine! Just show the panic app.
void system_app_state_machine_panic(void);
