#if HAS_TFT && USE_MCUI

#include "McDisplay.h"
#include "McUI.h"
#include "configuration.h"
#include "crowpanel_backlight.h"

// LovyanGFX driver for the DIS05020A v1.1 800x480 RGB panel + GT911 touch.
#include "graphics/LGFX/LGFX_ELECROW70.h"

#include "mesh/NodeDB.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace mcui {

// Native panel = 800x480 landscape. We ask LovyanGFX to rotate it 90° via
// setRotation(1) so the whole stack (drawing + touch) sees 480x800 portrait
// natively. LVGL then draws portrait without any rotation layer, and the
// flush just calls pushImageDMA() — LGFX handles the pixel transform into
// the RGB panel's internal framebuffer.
static constexpr uint16_t NATIVE_W = 800;
static constexpr uint16_t NATIVE_H = 480;

static LGFX_ELECROW70 *gfx = nullptr;
static lv_display_t *disp = nullptr;
static lv_indev_t *indev = nullptr;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

// ---- LVGL flush callback ---------------------------------------------------
// Area is in logical 480x800 portrait space. LovyanGFX's rotation (set via
// setRotation(1)) transforms the coordinates into the physical 800x480
// framebuffer automatically.
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    gfx->pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<uint16_t *>(px_map));
    lv_display_flush_ready(d);
}

// ---- LVGL indev (touch) callback ------------------------------------------
// LovyanGFX's getTouch() returns coordinates in the rotated (logical) space
// when setRotation() is applied, so we can feed them straight to LVGL.
//
// This callback is ALSO the single reader of the GT911 touch register — the
// backlight task used to poll it independently and would sometimes race us
// to clearing the touch-status flag, swallowing wake-up taps. See
// crowpanel_backlight.h for the full story.
//
// Wake-tap semantics: if the screen is currently asleep, a tap is consumed
// purely to wake the screen — we do NOT forward it as a press to LVGL,
// otherwise the first touch would accidentally activate whatever widget
// was sitting under the user's finger before the screen went dark.
static void touch_cb(lv_indev_t *drv, lv_indev_data_t *data)
{
    int32_t x = 0, y = 0;
    // GT911 touch lives on the same Wire bus as the 0x30 backlight
    // controller that the backlight task writes to from core 1.
    // Concurrent Arduino-Wire access from two cores corrupts the bus,
    // so we serialize against the backlight task through the shared
    // mutex it exposes. Lock held for a single short I2C transaction.
    backlight_i2c_lock();
    bool pressed = gfx->getTouch(&x, &y);
    backlight_i2c_unlock();
    if (pressed) {
        // Always note activity so the idle timer resets and the screen
        // wakes even if we end up swallowing this particular press event.
        backlight_notify_activity();

        if (!backlight_is_screen_on()) {
            // Screen was off — this tap's only job is to wake it.
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= SCR_W) x = SCR_W - 1;
        if (y >= SCR_H) y = SCR_H - 1;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void display_init()
{
    LOG_INFO("mcui: display_init() starting");

    // ---- LovyanGFX bringup ----
    gfx = new LGFX_ELECROW70();
    gfx->init();
    gfx->setRotation(1);        // portrait via 90° CW software rotation
    gfx->setSwapBytes(true);    // LVGL outputs native-endian RGB565; panel wants swapped
    gfx->setBrightness(255);
    gfx->fillScreen(TFT_BLACK); // clear the whole panel framebuffer

    LOG_INFO("mcui: LovyanGFX ready, logical %dx%d (rotated)",
             (int)gfx->width(), (int)gfx->height());

    // ---- LVGL core ----
    lv_init();
    lv_tick_set_cb(reinterpret_cast<lv_tick_get_cb_t>(millis));

    // ---- Draw buffers ----
    // Two partial buffers, each 480 wide × 200 rows = 192 KB in PSRAM. This
    // matches MeshCore's crowpanel_lvgl_chat reference (~96000 pixels each).
    // A bigger buffer means fewer flush round-trips per refresh, which is
    // the dominant cost on this panel — notably the keyboard redraw shrinks
    // from ~4 flushes to 2, and textarea edits land in a single flush. This
    // is THE critical perf knob for typing responsiveness.
    constexpr uint32_t BUF_LINES = 200;
    size_t bufBytes = (size_t)SCR_W * BUF_LINES * sizeof(lv_color_t);

    buf1 = static_cast<lv_color_t *>(heap_caps_aligned_alloc(32, bufBytes, MALLOC_CAP_SPIRAM));
    buf2 = static_cast<lv_color_t *>(heap_caps_aligned_alloc(32, bufBytes, MALLOC_CAP_SPIRAM));
    if (!buf1 || !buf2) {
        LOG_ERROR("mcui: PSRAM draw buffer alloc failed (%u bytes each), falling back",
                  (unsigned)bufBytes);
        // Release whichever side did succeed (don't leak a half-allocation).
        if (buf1) { heap_caps_free(buf1); buf1 = nullptr; }
        if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
        // Fallback: 40 rows × 480 × 2 ≈ 38 KB in internal DMA-capable RAM.
        bufBytes = (size_t)SCR_W * 40 * sizeof(lv_color_t);
        buf1 = static_cast<lv_color_t *>(heap_caps_aligned_alloc(32, bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        buf2 = static_cast<lv_color_t *>(heap_caps_aligned_alloc(32, bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        if (!buf1 || !buf2) {
            LOG_ERROR("mcui: internal draw buffer alloc failed (%u bytes each)",
                      (unsigned)bufBytes);
            if (buf1) { heap_caps_free(buf1); buf1 = nullptr; }
            if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
            return;
        }
    }

    // ---- Display object ----
    // Create with the LOGICAL (rotated) dimensions — LVGL thinks the screen
    // is 480 × 800 portrait, which matches what LovyanGFX reports after
    // setRotation(1).
    disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // No lv_display_set_rotation() — the panel is already rotated at the
    // LovyanGFX level, so LVGL renders directly into 480×800.

    // ---- Touch input device ----
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);
    lv_indev_set_display(indev, disp);
    // Long-press threshold is short enough for node context menus to feel
    // responsive, but still comfortably above normal tap/typing timing.
    // Keeps quick taps from ever entering long-press state — important for keyboards,
    // where LVGL's default 400 ms would fire LONG_PRESSED on any held key
    // and interact badly with auto-repeat.
    lv_indev_set_long_press_time(indev, 700);

    // Tune the display refresh timer to 20 ms period (50 Hz). LVGL's default
    // LV_DEF_REFR_PERIOD is 33 ms (~30 Hz) which feels sluggish on this
    // panel for typing-speed edits. Avoid pushing faster than the RGB panel
    // scanout; that causes visible tearing/ghosting on this hardware.
    lv_timer_t *refr_timer = lv_display_get_refr_timer(disp);
    if (refr_timer) {
        lv_timer_set_period(refr_timer, 20);
    }

    // Apply the persisted screen-sleep timeout from Meshtastic config to the
    // backlight task now that config is loaded. The backlight task started
    // earlier in initVariant() with the default 60 s timeout.
    {
        uint32_t secs = config.display.screen_on_secs;
        // Meshtastic semantics: 0 = default (60 s handled inside backlight),
        // UINT32_MAX = always on. Pass straight through.
        backlight_set_timeout_secs(secs);
    }

    LOG_INFO("mcui: LVGL ready, logical %dx%d portrait", SCR_W, SCR_H);
}

} // namespace mcui

#endif // HAS_TFT && USE_MCUI
