#pragma once

// The AMOLED burn-in and idle task (DESIGN_V2 §2.6). The timing lives in
// burn_in_policy.hpp; this is the firmware side: it feeds the policy touch,
// motion and navigation, and applies its answer through the board port.

// Starts the task. Call once, after the UI exists and the panel is revealed.
bool burn_in_start();

// The IMU saw vehicle motion. Any task; cheap enough for the sensor task.
void burn_in_note_motion();

// One NavigationSnapshot from the phone. Any task.
void burn_in_note_navigation(bool route_active);

// `burnin ...` bench commands; `args` is the text after "burnin".
void burn_in_command(const char* args);
