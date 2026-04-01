// =============================================================================
// crowpanel_backlight.cpp — CrowPanel DIS05020A v1.1 init for Meshtastic
//
// 1. Erases otadata → boot selector always appears
// 2. Pre-mounts LittleFS on "mtdata" partition
// 3. Wakes the I2C backlight controller + GT911 touch
// 4. Runs a background task that monitors touch and controls backlight
//    (turns off after 30s idle, wakes on touch)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <esp_partition.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ---------------------------------------------------------------------------
// I2C helpers for backlight controller at 0x30
// ---------------------------------------------------------------------------
static bool _i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

static void _bl_cmd(uint8_t cmd) {
    Wire.beginTransmission(0x30);
    Wire.write(cmd);
    Wire.endTransmission();
}

static const uint8_t BL_OFF = 0x05;
static const uint8_t BL_MAX = 0x10;

// ---------------------------------------------------------------------------
// Soft wake: ramp brightness from ~10% to 100% over ~1.5 seconds
// Uses quadratic ease-in so lower levels (where the eye is most sensitive)
// get more dwell time, making the ramp feel smoother despite only 10 hw steps.
// Uses delay() so it works both during initVariant() and inside RTOS tasks.
// ---------------------------------------------------------------------------
static void _bl_soft_wake() {
    const uint8_t start = BL_OFF + 1; // lowest visible level
    const uint8_t end   = BL_MAX;
    const uint8_t steps = end - start; // 10 steps
    const uint32_t total_ms = 1500;

    for (uint8_t i = 0; i <= steps; i++) {
        _bl_cmd(start + i);
        if (i < steps) {
            // Quadratic ease-in: early steps get longer delays, later steps shorter
            // t(i) = total * [(i+1)^2 - i^2] / steps^2 = total * (2i+1) / steps^2
            uint32_t dt = total_ms * (2 * i + 1) / (steps * steps);
            delay(dt);
        }
    }
}

// ---------------------------------------------------------------------------
// Background task: monitor GT911 touch and control backlight
// ---------------------------------------------------------------------------
static const uint32_t SCREEN_TIMEOUT_MS = 30000; // 30 seconds

static void backlight_task(void* param) {
    uint32_t last_touch_ms = millis();
    bool screen_on = true;

    // Wait for display init to complete
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (true) {
        // Check GT911 for touch by reading register 0x814E (touch status)
        Wire.beginTransmission(0x5D);
        Wire.write(0x81);
        Wire.write(0x4E);
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom((uint8_t)0x5D, (uint8_t)1);
            if (Wire.available()) {
                uint8_t status = Wire.read();
                bool touching = (status & 0x80) && (status & 0x0F);

                // Clear touch flag
                if (touching) {
                    Wire.beginTransmission(0x5D);
                    Wire.write(0x81);
                    Wire.write(0x4E);
                    Wire.write(0x00);
                    Wire.endTransmission();
                }

                if (touching) {
                    last_touch_ms = millis();
                    if (!screen_on) {
                        _bl_soft_wake();
                        screen_on = true;
                    }
                }
            }
        }

        // Check timeout
        if (screen_on && (millis() - last_touch_ms > SCREEN_TIMEOUT_MS)) {
            _bl_cmd(BL_OFF);
            screen_on = false;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ---------------------------------------------------------------------------
// initVariant() — called by Arduino core before setup()
// ---------------------------------------------------------------------------
extern "C" void initVariant() {

    // ---- 1. Erase otadata so boot selector runs on next reboot ----
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }

    // ---- 2. Pre-mount LittleFS on Meshtastic's own "mtdata" partition ----
    LittleFS.begin(true, "/littlefs", 10, "mtdata");

    // ---- 3. I2C backlight + GT911 wake sequence (from MeshCore) ----
    Wire.begin(15, 16);
    Wire.setClock(400000);
    delay(50);

    for (int i = 0; i < 20; i++) {
        if (_i2c_probe(0x30) && _i2c_probe(0x5D)) break;
        _bl_cmd(0x19);
        pinMode(1, OUTPUT);
        digitalWrite(1, LOW);
        delay(120);
        pinMode(1, INPUT);
        delay(100);
    }

    // Backlight ON (soft ramp to reduce inrush current on battery)
    _bl_soft_wake();

    // ---- 4. Start backlight monitor task ----
    xTaskCreatePinnedToCore(
        backlight_task,
        "blTask",
        2048,
        NULL,
        1,
        NULL,
        1  // Core 1
    );
}
