#pragma once
#if HAS_TFT && USE_MCUI
#include <lvgl.h>
namespace mcui {

// MeshCore-style 5-row on-screen keyboard. Phase 1 ships the English
// lowercase/uppercase/symbols layout only. Phase 2 wires it to chat input.
//
// Usage:
//   lv_obj_t *kb = keyboard_create(lv_screen_active());
//   keyboard_attach(textarea);
//   keyboard_show();
//
// The keyboard is created HIDDEN. It lives in the root screen so the
// bottom-of-screen placement survives tab switching.

constexpr int KB_H = 280;

lv_obj_t *keyboard_create(lv_obj_t *parent);
void keyboard_attach(lv_obj_t *textarea);
void keyboard_show();
void keyboard_hide();
bool keyboard_is_visible();
lv_obj_t *keyboard_get();

} // namespace mcui
#endif
