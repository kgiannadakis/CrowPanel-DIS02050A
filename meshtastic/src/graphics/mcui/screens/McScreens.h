#pragma once
#if HAS_TFT && USE_MCUI
#include <lvgl.h>
namespace mcui {
// Phase 2 screens.
lv_obj_t *chats_screen_create(lv_obj_t *parent);
lv_obj_t *nodes_screen_create(lv_obj_t *parent);
lv_obj_t *maps_screen_create(lv_obj_t *parent);
lv_obj_t *settings_screen_create(lv_obj_t *parent);
void settings_maybe_show_onboarding();

// Called each UI tick; rebuilds the chats list if the message store changed.
void chats_screen_tick();

// Called each UI tick; refreshes the nodes list periodically (and whenever
// the node count changes) so SNR/RSSI/last-heard stay current.
void nodes_screen_tick();

// Called each UI tick; refreshes the Maps screen phone-position instructions
// so the shown URL tracks the current WiFi IP.
void maps_screen_tick();

// Called each UI tick; refreshes the Settings screen's live info (uptime,
// free heap, battery) once per second.
void settings_screen_tick();
} // namespace mcui
#endif
