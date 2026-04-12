#pragma once
#if HAS_TFT && USE_MCUI
#include <lvgl.h>
namespace mcui {
// Creates the bottom 4-button tab bar (Chats / Nodes / Maps / Settings).
// Tapping a tab calls mcui::switchTab().
lv_obj_t *tabbar_create(lv_obj_t *parent);
// Visually mark a tab active (highlights its label + top indicator bar).
void tabbar_set_active(int idx);
} // namespace mcui
#endif
