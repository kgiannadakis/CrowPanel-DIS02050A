#pragma once
#if HAS_TFT && USE_MCUI

// =============================================================================
// McClock — persistence layer for the on-board PCF8563 RTC at I2C 0x51
//
// The CrowPanel DIS05020A v1.1 has a PCF8563 real-time clock chip on the
// same Wire bus as the GT911 touch (0x5D) and the backlight controller
// (0x30). It is NOT wired up by Meshtastic's own RTC layer (no coin-cell
// holder is populated on the stock board, but the chip itself still keeps
// time as long as it has ESP32 power), so this module handles the two
// things Meshtastic is missing for it:
//
//   1. On boot, read the chip. If the stored time looks sensible (year
//      >= 2025 AND the chip's internal "integrity" bit says the oscillator
//      has kept running since last set), hand it to Meshtastic's
//      perhapsSetRTC(RTCQualityDevice, tm). From that point on every mesh
//      packet and every chat message is tagged with the real UTC epoch —
//      without needing WiFi at all.
//
//   2. After a successful NTP sync (from WiFiAPClient.cpp), write the
//      UTC epoch back to the chip via mcclock_save(). That's the only
//      time we ever hit the internet for time — subsequent boots restore
//      from the chip in step 1.
//
// All I2C access is bracketed by the shared `backlight_i2c_lock` so we
// don't collide with the GT911 touch reads from LVGL on core 0.
// =============================================================================

#include <stdint.h>

namespace mcui {

// Probe the PCF8563 at 0x51, and if it's present and holds a valid UTC
// time, seed the Meshtastic RTC subsystem with it via perhapsSetRTC.
// Safe to call multiple times; the second call is a cheap no-op.
// Must be called after Wire is initialised (so after initVariant()).
void mcclock_init();

// Write a UTC epoch (seconds since 1970-01-01 UTC) to the PCF8563. No-op
// if the chip wasn't detected at init time. Call this once, from the NTP
// success path in WiFiAPClient.cpp, so the time survives reboots.
void mcclock_save(uint32_t utc_epoch);

// True if the chip acked at address 0x51 during mcclock_init().
bool mcclock_has_rtc();

// True if mcclock_init() successfully restored a valid time from the
// chip, OR if mcclock_save() has been called this boot (i.e. the chip
// now holds a fresh NTP-sourced time). Used by the WiFi NTP code to
// decide whether a periodic re-sync is still necessary.
bool mcclock_has_valid_time();

} // namespace mcui

#endif
