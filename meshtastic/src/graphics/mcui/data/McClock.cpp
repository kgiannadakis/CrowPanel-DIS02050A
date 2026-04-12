#if HAS_TFT && USE_MCUI

#include "McClock.h"
#include "configuration.h"
#include "crowpanel_backlight.h"
#include "gps/RTC.h"

#include <Arduino.h>
#include <Wire.h>
#include <pcf8563.h>

#include <sys/time.h>
#include <time.h>

namespace mcui {

static PCF8563_Class s_rtc;
static bool s_rtc_ok     = false;
static bool s_time_valid = false;

// Probe the chip independently of the library's begin() — begin() always
// returns OK even when nothing acks, so we check the bus ourselves first
// and only poke the library if 0x51 actually replied.
static bool probe_pcf8563()
{
    backlight_i2c_lock();
    Wire.beginTransmission(0x51);
    uint8_t err = Wire.endTransmission();
    backlight_i2c_unlock();
    return err == 0;
}

void mcclock_init()
{
    static bool done = false;
    if (done) return;
    done = true;

    if (!probe_pcf8563()) {
        LOG_INFO("mcclock: PCF8563 not detected on 0x51");
        return;
    }
    s_rtc_ok = true;
    LOG_INFO("mcclock: PCF8563 detected on 0x51");

    backlight_i2c_lock();
    s_rtc.begin(Wire);
    RTC_Date d = s_rtc.getDateTime();
    bool chip_says_valid = s_rtc.isVaild();
    backlight_i2c_unlock();

    // Sanity: range-check the year. The chip will happily return garbage
    // (year 2000, 2165, etc.) if nothing has ever been written to it. We
    // only trust the reading if isVaild() says the oscillator hasn't
    // stopped AND the year is within our expected window.
    if (!chip_says_valid) {
        LOG_INFO("mcclock: PCF8563 integrity flag bad — waiting for NTP");
        return;
    }
    if (d.year < 2025 || d.year > 2099) {
        LOG_INFO("mcclock: PCF8563 year %u out of range — waiting for NTP",
                 (unsigned)d.year);
        return;
    }

    // Hand the restored time to the Meshtastic RTC subsystem. perhapsSetRTC
    // interprets the struct tm as UTC (we store UTC, not local time).
    struct tm t = {};
    t.tm_year = (int)d.year - 1900;
    t.tm_mon  = (int)d.month - 1;
    t.tm_mday = (int)d.day;
    t.tm_hour = (int)d.hour;
    t.tm_min  = (int)d.minute;
    t.tm_sec  = (int)d.second;

    RTCSetResult r = perhapsSetRTC(RTCQualityDevice, t);
    if (r == RTCSetResultSuccess || r == RTCSetResultNotSet) {
        s_time_valid = true;
        LOG_INFO("mcclock: restored UTC %04u-%02u-%02u %02u:%02u:%02u from PCF8563",
                 (unsigned)d.year, (unsigned)d.month, (unsigned)d.day,
                 (unsigned)d.hour, (unsigned)d.minute, (unsigned)d.second);
    } else {
        LOG_WARN("mcclock: perhapsSetRTC rejected restored time (r=%d)", (int)r);
    }
}

void mcclock_save(uint32_t utc_epoch)
{
    if (!s_rtc_ok) {
        LOG_DEBUG("mcclock: save skipped — no RTC");
        return;
    }
    if (utc_epoch < 1700000000) {
        // Obviously-bogus epoch (pre-2023). Don't poison the chip.
        LOG_WARN("mcclock: refusing to save suspicious epoch %u", (unsigned)utc_epoch);
        return;
    }

    // Break the epoch down into UTC components via gmtime_r — the chip
    // stores wall-clock fields without any timezone awareness, so we
    // always persist UTC.
    time_t t = (time_t)utc_epoch;
    struct tm utc;
    gmtime_r(&t, &utc);

    backlight_i2c_lock();
    s_rtc.setDateTime(
        (uint16_t)(utc.tm_year + 1900),
        (uint8_t)(utc.tm_mon + 1),
        (uint8_t)utc.tm_mday,
        (uint8_t)utc.tm_hour,
        (uint8_t)utc.tm_min,
        (uint8_t)utc.tm_sec
    );
    backlight_i2c_unlock();

    s_time_valid = true;
    LOG_INFO("mcclock: saved UTC %04u-%02u-%02u %02u:%02u:%02u to PCF8563",
             (unsigned)(utc.tm_year + 1900), (unsigned)(utc.tm_mon + 1),
             (unsigned)utc.tm_mday, (unsigned)utc.tm_hour,
             (unsigned)utc.tm_min, (unsigned)utc.tm_sec);
}

bool mcclock_has_rtc()        { return s_rtc_ok; }
bool mcclock_has_valid_time() { return s_time_valid; }

} // namespace mcui

#endif
