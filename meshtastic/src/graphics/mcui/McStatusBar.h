#pragma once
#if HAS_TFT && USE_MCUI
#include <lvgl.h>
namespace mcui {
// Top status bar: device short name on left, time on right.
lv_obj_t *statusbar_create(lv_obj_t *parent);
// Called periodically from the UI task to refresh time/battery/etc.
void statusbar_refresh();
// Show or hide the shared top status bar.
void statusbar_set_visible(bool visible);
} // namespace mcui
#endif
