// Display + touch bringup for mcui (LovyanGFX + LVGL 9 in portrait)
#pragma once
#if HAS_TFT && USE_MCUI

namespace mcui {
// Initialize LovyanGFX panel, LVGL core, draw buffers, and touch indev.
// Safe to call once from the mcui FreeRTOS task before building the UI.
void display_init();
} // namespace mcui

#endif
