// =============================================================================
// Dual-Boot Selector — CrowPanel DIS05020A v1.1
// Flashed to "factory" partition. Shows two touch buttons on boot.
// Saves last choice to NVS for auto-boot with 3-second countdown.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>

// ---------------------------------------------------------------------------
// LovyanGFX driver — identical to working MeshCore config
// ---------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB      _bus_instance;
    lgfx::Panel_RGB    _panel_instance;
    lgfx::Touch_GT911  _touch_instance;
    LGFX(void) {
        { auto c = _panel_instance.config();
          c.memory_width=800; c.memory_height=480;
          c.panel_width=800; c.panel_height=480;
          _panel_instance.config(c); }
        { auto c = _panel_instance.config_detail(); c.use_psram=1; _panel_instance.config_detail(c); }
        { auto c = _bus_instance.config();
          c.panel=&_panel_instance;
          c.pin_d0=GPIO_NUM_21; c.pin_d1=GPIO_NUM_47; c.pin_d2=GPIO_NUM_48;
          c.pin_d3=GPIO_NUM_45; c.pin_d4=GPIO_NUM_38;
          c.pin_d5=GPIO_NUM_9;  c.pin_d6=GPIO_NUM_10; c.pin_d7=GPIO_NUM_11;
          c.pin_d8=GPIO_NUM_12; c.pin_d9=GPIO_NUM_13; c.pin_d10=GPIO_NUM_14;
          c.pin_d11=GPIO_NUM_7; c.pin_d12=GPIO_NUM_17; c.pin_d13=GPIO_NUM_18;
          c.pin_d14=GPIO_NUM_3; c.pin_d15=GPIO_NUM_46;
          c.pin_henable=GPIO_NUM_42; c.pin_vsync=GPIO_NUM_41;
          c.pin_hsync=GPIO_NUM_40; c.pin_pclk=GPIO_NUM_39;
          c.freq_write=14000000;
          c.hsync_polarity=0; c.hsync_front_porch=8; c.hsync_pulse_width=4; c.hsync_back_porch=8;
          c.vsync_polarity=0; c.vsync_front_porch=8; c.vsync_pulse_width=4; c.vsync_back_porch=8;
          c.pclk_idle_high=1;
          _bus_instance.config(c); }
        _panel_instance.setBus(&_bus_instance);
        { auto c = _touch_instance.config();
          c.x_min=0; c.x_max=800; c.y_min=0; c.y_max=480;
          c.pin_int=-1; c.bus_shared=false; c.offset_rotation=0;
          c.i2c_port=I2C_NUM_0; c.pin_sda=GPIO_NUM_15; c.pin_scl=GPIO_NUM_16;
          c.pin_rst=-1; c.freq=400000; c.i2c_addr=0x5D;
          _touch_instance.config(c);
          _panel_instance.setTouch(&_touch_instance); }
        setPanel(&_panel_instance);
    }
};

static LGFX gfx;
static Preferences prefs;
static uint16_t tx, ty;

// I2C helpers
static bool i2c_probe(uint8_t a) { Wire.beginTransmission(a); return !Wire.endTransmission(); }
static void bl_cmd(uint8_t c) { Wire.beginTransmission(0x30); Wire.write(c); Wire.endTransmission(); }

// Button geometry
static const int BTN_Y = 140, BTN_H = 220, BTN_GAP = 40;
static const int BTN_W = 350;
static const int BTN_A_X = (800 - 2*BTN_W - BTN_GAP) / 2;
static const int BTN_B_X = BTN_A_X + BTN_W + BTN_GAP;

// Colors
static const uint32_t COL_BG         = 0x0B1A30;
static const uint32_t COL_MESHCORE   = 0x2ECC71;
static const uint32_t COL_MESHTASTIC = 0x3498DB;
static const uint32_t COL_DISABLED   = 0x1A1A2E;
static const uint32_t COL_TEXT       = 0xFFFFFF;
static const uint32_t COL_SUB        = 0xA0B4C8;
static const uint32_t COL_AMBER      = 0xFFAA00;

// ---------------------------------------------------------------------------
// Partition helpers
// ---------------------------------------------------------------------------
static const esp_partition_t* find_ota(int slot) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        slot == 0 ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
}

static bool has_firmware(const esp_partition_t* p) {
    if (!p) return false;
    uint8_t h[4]; esp_partition_read(p, 0, h, 4);
    return h[0] == 0xE9; // ESP32 app magic
}

static void boot_slot(int slot) {
    const esp_partition_t* p = find_ota(slot);
    if (!p || !has_firmware(p)) {
        gfx.setTextDatum(lgfx::middle_center);
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(0xFF4444);
        gfx.drawString("No firmware in this slot!", 400, 430);
        delay(2000);
        return;
    }
    prefs.begin("dualboot", false);
    prefs.putInt("last", slot);
    prefs.end();
    // Flash button white
    int x = (slot == 0) ? BTN_A_X : BTN_B_X;
    gfx.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 16, COL_TEXT);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(0x000000);
    gfx.drawString("Booting...", x + BTN_W/2, BTN_Y + BTN_H/2);
    delay(300);
    esp_ota_set_boot_partition(p);
    esp_restart();
}

static bool touch_in(int rx, int ry, int rw, int rh) {
    return tx >= rx && tx <= rx+rw && ty >= ry && ty <= ry+rh;
}

// ---------------------------------------------------------------------------
// Draw UI
// ---------------------------------------------------------------------------
static void draw_button(int x, int y, int w, int h, uint32_t col,
                        const char* title, const char* sub, bool valid) {
    gfx.fillRoundRect(x, y, w, h, 16, valid ? col : COL_DISABLED);
    if (!valid) gfx.drawRoundRect(x, y, w, h, 16, 0x333333);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(valid ? COL_TEXT : 0x666666);
    gfx.drawString(title, x+w/2, y+h/2 - 30);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(valid ? 0xD0D0D0 : 0x444444);
    gfx.drawString(sub, x+w/2, y+h/2 + 15);
    if (!valid) {
        gfx.setTextColor(0xFF4444);
        gfx.drawString("[empty slot]", x+w/2, y+h/2 + 50);
    }
}

static void draw_ui(bool ota0_ok, bool ota1_ok) {
    gfx.fillScreen(COL_BG);
    // Header
    gfx.fillRect(0, 0, 800, 70, 0x1A1A2E);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("MeshCore/Meshtastic Dual-Boot by KaA", 400, 25);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_SUB);
    gfx.drawString("Tap a firmware to boot  |  Auto-boots last choice in 3s", 400, 55);
    // Buttons
    draw_button(BTN_A_X, BTN_Y, BTN_W, BTN_H, COL_MESHCORE,
                "MeshCore", "LoRa Mesh Chat", ota0_ok);
    draw_button(BTN_B_X, BTN_Y, BTN_W, BTN_H, COL_MESHTASTIC,
                "Meshtastic", "LoRa Mesh Chat", ota1_ok);
    // Footer
    gfx.setTextDatum(lgfx::bottom_center);
    gfx.setTextColor(0x555555);
    gfx.drawString("Version 1.0, 22-03-2026  |  Copyright 2026, Kostis Giannadakis", 400, 470);
}

static void draw_countdown(int secs, int slot) {
    gfx.fillRect(150, 390, 500, 35, COL_BG);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_AMBER);
    char buf[80];
    snprintf(buf, sizeof(buf), "Auto-booting %s in %d...  (tap to cancel)",
             slot == 0 ? "MeshCore" : "Meshtastic", secs);
    gfx.drawString(buf, 400, 407);
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n===== BOOT SELECTOR RUNNING =====");
    Serial.printf("Partition: %s\n", esp_ota_get_running_partition()->label);
    Serial.println("\n[DualBoot] Starting boot selector...");

    // I2C + backlight wake (proven MeshCore sequence)
    Wire.begin(15, 16);
    Wire.setClock(400000);
    delay(50);
    for (int i = 0; i < 20; i++) {
        if (i2c_probe(0x30) && i2c_probe(0x5D)) break;
        bl_cmd(0x19);
        pinMode(1, OUTPUT); digitalWrite(1, LOW); delay(120);
        pinMode(1, INPUT); delay(100);
    }

    // Display init
    gfx.init();
    gfx.setRotation(0);
    gfx.fillScreen(0);
    bl_cmd(0x10); // backlight max

    // Check partitions
    bool ota0_ok = has_firmware(find_ota(0));
    bool ota1_ok = has_firmware(find_ota(1));
    Serial.printf("[DualBoot] MeshCore (ota_0): %s\n", ota0_ok ? "OK" : "EMPTY");
    Serial.printf("[DualBoot] Meshtastic (ota_1): %s\n", ota1_ok ? "OK" : "EMPTY");

    draw_ui(ota0_ok, ota1_ok);

    // Auto-boot countdown if we have a saved preference
    prefs.begin("dualboot", true);
    int last = prefs.getInt("last", -1);
    prefs.end();

    if (last >= 0 && last <= 1) {
        const esp_partition_t* saved = find_ota(last);
        if (has_firmware(saved)) {
            for (int i = 3; i > 0; i--) {
                draw_countdown(i, last);
                for (int t = 0; t < 20; t++) {
                    if (gfx.getTouch(&tx, &ty)) {
                        Serial.println("[DualBoot] Cancelled auto-boot");
                        gfx.fillRect(150, 390, 500, 35, COL_BG);
                        gfx.setTextDatum(lgfx::middle_center);
                        gfx.setFont(&fonts::Font2);
                        gfx.setTextColor(COL_SUB);
                        gfx.drawString("Auto-boot cancelled. Tap a firmware.", 400, 407);
                        while (gfx.getTouch(&tx, &ty)) delay(50);
                        goto selector;
                    }
                    delay(50);
                }
            }
            boot_slot(last);
        }
    }

selector:
    Serial.println("[DualBoot] Waiting for selection...");
    while (true) {
        if (gfx.getTouch(&tx, &ty)) {
            delay(80);
            if (touch_in(BTN_A_X, BTN_Y, BTN_W, BTN_H) && ota0_ok)
                boot_slot(0);
            else if (touch_in(BTN_B_X, BTN_Y, BTN_W, BTN_H) && ota1_ok)
                boot_slot(1);
            while (gfx.getTouch(&tx, &ty)) delay(50);
        }
        delay(20);
    }
}

void loop() {}
