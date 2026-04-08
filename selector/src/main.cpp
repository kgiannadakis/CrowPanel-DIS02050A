// =============================================================================
// Dual-Boot Selector — CrowPanel DIS05020A v1.1
// Flashed to "factory" partition. Shows two touch buttons on boot.
// Saves last choice to NVS for auto-boot with 3-second countdown.
// OTA: downloads meshcore.bin and meshtastic.bin from GitHub Releases
//      using WiFi credentials saved by MeshCore.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>

// ---------------------------------------------------------------------------
// LovyanGFX driver
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
static const int BTN_Y = 140, BTN_H = 180, BTN_GAP = 40;
static const int BTN_W = 350;
static const int BTN_A_X = (800 - 2*BTN_W - BTN_GAP) / 2;
static const int BTN_B_X = BTN_A_X + BTN_W + BTN_GAP;

// Bottom buttons (Update + WiFi side by side)
static const int UPD_Y = 340, UPD_H = 50, UPD_W = 240;
static const int WIFI_BTN_W = 240;
static const int BTN_TOTAL_W = UPD_W + 20 + WIFI_BTN_W;
static const int UPD_X = (800 - BTN_TOTAL_W) / 2;
static const int WIFI_BTN_X = UPD_X + UPD_W + 20;

// Colors
static const uint32_t COL_BG         = 0x0B1A30;
static const uint32_t COL_MESHCORE   = 0x2ECC71;
static const uint32_t COL_MESHTASTIC = 0x3498DB;
static const uint32_t COL_DISABLED   = 0x1A1A2E;
static const uint32_t COL_TEXT       = 0xFFFFFF;
static const uint32_t COL_SUB        = 0xA0B4C8;
static const uint32_t COL_AMBER      = 0xFFAA00;
static const uint32_t COL_UPDATE     = 0x9B59B6;

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
    return h[0] == 0xE9;
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
// Status bar at bottom
// ---------------------------------------------------------------------------
static void draw_status(const char* msg, uint32_t col = COL_SUB) {
    gfx.fillRect(0, 410, 800, 30, COL_BG);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(col);
    gfx.drawString(msg, 400, 425);
}

// ---------------------------------------------------------------------------
// Progress bar
// ---------------------------------------------------------------------------
static void draw_progress(int percent, const char* label) {
    gfx.fillRect(100, 395, 600, 45, COL_BG);
    // Bar background
    gfx.fillRoundRect(150, 400, 500, 20, 6, 0x1A1A2E);
    // Bar fill
    int fill_w = (int)(500.0f * percent / 100.0f);
    if (fill_w > 0) gfx.fillRoundRect(150, 400, fill_w, 20, 6, COL_MESHCORE);
    // Label
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %d%%", label, percent);
    gfx.drawString(buf, 400, 430);
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
    gfx.drawString(title, x+w/2, y+h/2 - 20);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(valid ? 0xD0D0D0 : 0x444444);
    gfx.drawString(sub, x+w/2, y+h/2 + 15);
    if (!valid) {
        gfx.setTextColor(0xFF4444);
        gfx.drawString("[empty slot]", x+w/2, y+h/2 + 40);
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
    // Firmware buttons
    draw_button(BTN_A_X, BTN_Y, BTN_W, BTN_H, COL_MESHCORE,
                "MeshCore", "LoRa Mesh Chat", ota0_ok);
    draw_button(BTN_B_X, BTN_Y, BTN_W, BTN_H, COL_MESHTASTIC,
                "Meshtastic", "LoRa Mesh Chat", ota1_ok);
    // Update button
    gfx.fillRoundRect(UPD_X, UPD_Y, UPD_W, UPD_H, 12, COL_UPDATE);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("Update Firmware", UPD_X + UPD_W/2, UPD_Y + UPD_H/2);
    // WiFi button
    gfx.fillRoundRect(WIFI_BTN_X, UPD_Y, WIFI_BTN_W, UPD_H, 12, 0x2980B9);
    gfx.drawString("WiFi Setup", WIFI_BTN_X + WIFI_BTN_W/2, UPD_Y + UPD_H/2);
    // Footer
    gfx.setTextDatum(lgfx::bottom_center);
    gfx.setTextColor(0x555555);
    gfx.drawString("Version 1.1, 07/04/2026  |  Baked by Kostis Giannadakis", 400, 470);
}

static void draw_countdown(int secs, int slot) {
    gfx.fillRect(150, 400, 500, 35, COL_BG);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_AMBER);
    char buf[80];
    snprintf(buf, sizeof(buf), "Auto-booting %s in %d...  (tap to cancel)",
             slot == 0 ? "MeshCore" : "Meshtastic", secs);
    gfx.drawString(buf, 400, 417);
}

// ---------------------------------------------------------------------------
// WiFi Setup: scan, select network, enter password, connect & save
// ---------------------------------------------------------------------------

// On-screen keyboard layout
static const char* KB_ROW1 = "1234567890";
static const char* KB_ROW2 = "qwertyuiop";
static const char* KB_ROW3 = "asdfghjkl";
static const char* KB_ROW4 = "zxcvbnm.,";
static const int KB_KEY_W = 62, KB_KEY_H = 40, KB_GAP = 3;
static const int KB_START_Y = 210;
static bool kb_shift = false;

static void draw_kb_key(int x, int y, int w, int h, char c, bool highlight) {
    gfx.fillRoundRect(x, y, w, h, 6, highlight ? 0x2ECC71 : 0x2B3B4D);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    char s[2] = {c, 0};
    if (kb_shift && c >= 'a' && c <= 'z') s[0] = c - 32;
    gfx.drawString(s, x + w/2, y + h/2);
}

static void draw_keyboard(const char* pwd) {
    int y = KB_START_Y;
    int row_w, start_x;

    // Password field
    gfx.fillRect(50, KB_START_Y - 45, 700, 36, 0x1A1A2E);
    gfx.drawRoundRect(50, KB_START_Y - 45, 700, 36, 8, 0x3390EC);
    gfx.setTextDatum(lgfx::middle_left);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    // Show password as dots with last char visible
    int plen = strlen(pwd);
    char display[65];
    if (plen == 0) {
        strcpy(display, "Enter password...");
        gfx.setTextColor(0x666666);
    } else {
        for (int i = 0; i < plen - 1 && i < 62; i++) display[i] = '*';
        if (plen > 0) display[plen - 1] = pwd[plen - 1];
        display[plen < 63 ? plen : 63] = '\0';
    }
    gfx.drawString(display, 60, KB_START_Y - 27);

    // Row 1: numbers
    row_w = 10 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    for (int i = 0; KB_ROW1[i]; i++)
        draw_kb_key(start_x + i * (KB_KEY_W + KB_GAP), y, KB_KEY_W, KB_KEY_H, KB_ROW1[i], false);
    y += KB_KEY_H + KB_GAP;

    // Row 2: qwerty
    row_w = 10 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    for (int i = 0; KB_ROW2[i]; i++)
        draw_kb_key(start_x + i * (KB_KEY_W + KB_GAP), y, KB_KEY_W, KB_KEY_H, KB_ROW2[i], false);
    y += KB_KEY_H + KB_GAP;

    // Row 3: asdf + Enter
    row_w = 9 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    for (int i = 0; KB_ROW3[i]; i++)
        draw_kb_key(start_x + i * (KB_KEY_W + KB_GAP), y, KB_KEY_W, KB_KEY_H, KB_ROW3[i], false);
    y += KB_KEY_H + KB_GAP;

    // Row 4: zxcv
    row_w = 9 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    // Shift key
    gfx.fillRoundRect(start_x - KB_KEY_W - KB_GAP, y, KB_KEY_W, KB_KEY_H, 6, kb_shift ? 0x2ECC71 : 0x2B3B4D);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.drawString("Aa", start_x - KB_KEY_W/2 - KB_GAP, y + KB_KEY_H/2);
    for (int i = 0; KB_ROW4[i]; i++)
        draw_kb_key(start_x + i * (KB_KEY_W + KB_GAP), y, KB_KEY_W, KB_KEY_H, KB_ROW4[i], false);
    y += KB_KEY_H + KB_GAP;

    // Bottom row: Space + Backspace + Connect
    int space_w = 300, bksp_w = 120, conn_w = 160;
    int total_bottom = space_w + KB_GAP + bksp_w + KB_GAP + conn_w;
    int bx = (800 - total_bottom) / 2;
    // Space
    gfx.fillRoundRect(bx, y, space_w, KB_KEY_H, 6, 0x2B3B4D);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.drawString("Space", bx + space_w/2, y + KB_KEY_H/2);
    // Backspace
    gfx.fillRoundRect(bx + space_w + KB_GAP, y, bksp_w, KB_KEY_H, 6, 0xE74C3C);
    gfx.drawString("Del", bx + space_w + KB_GAP + bksp_w/2, y + KB_KEY_H/2);
    // Connect
    gfx.fillRoundRect(bx + space_w + KB_GAP + bksp_w + KB_GAP, y, conn_w, KB_KEY_H, 6, 0x2ECC71);
    gfx.drawString("Connect", bx + space_w + KB_GAP + bksp_w + KB_GAP + conn_w/2, y + KB_KEY_H/2);
}

// Returns: char pressed, or special: '\b'=backspace, ' '=space, '\n'=connect, 'S'=shift, 0=nothing
static char kb_check_touch() {
    if (!gfx.getTouch(&tx, &ty)) return 0;
    int y = KB_START_Y;
    int row_w, start_x;

    // Row 1
    row_w = 10 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    if (ty >= y && ty < y + KB_KEY_H) {
        for (int i = 0; KB_ROW1[i]; i++) {
            int kx = start_x + i * (KB_KEY_W + KB_GAP);
            if (tx >= kx && tx < kx + KB_KEY_W) return KB_ROW1[i];
        }
    }
    y += KB_KEY_H + KB_GAP;

    // Row 2
    row_w = 10 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    if (ty >= y && ty < y + KB_KEY_H) {
        for (int i = 0; KB_ROW2[i]; i++) {
            int kx = start_x + i * (KB_KEY_W + KB_GAP);
            if (tx >= kx && tx < kx + KB_KEY_W) {
                char c = KB_ROW2[i];
                return kb_shift ? (c - 32) : c;
            }
        }
    }
    y += KB_KEY_H + KB_GAP;

    // Row 3
    row_w = 9 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    if (ty >= y && ty < y + KB_KEY_H) {
        for (int i = 0; KB_ROW3[i]; i++) {
            int kx = start_x + i * (KB_KEY_W + KB_GAP);
            if (tx >= kx && tx < kx + KB_KEY_W) {
                char c = KB_ROW3[i];
                return kb_shift ? (c - 32) : c;
            }
        }
    }
    y += KB_KEY_H + KB_GAP;

    // Row 4 + Shift
    row_w = 9 * (KB_KEY_W + KB_GAP) - KB_GAP;
    start_x = (800 - row_w) / 2;
    // Shift key
    if (ty >= y && ty < y + KB_KEY_H && tx >= start_x - KB_KEY_W - KB_GAP && tx < start_x) return 'S';
    if (ty >= y && ty < y + KB_KEY_H) {
        for (int i = 0; KB_ROW4[i]; i++) {
            int kx = start_x + i * (KB_KEY_W + KB_GAP);
            if (tx >= kx && tx < kx + KB_KEY_W) {
                char c = KB_ROW4[i];
                return kb_shift ? (c - 32) : c;
            }
        }
    }
    y += KB_KEY_H + KB_GAP;

    // Bottom row
    int space_w = 300, bksp_w = 120, conn_w = 160;
    int total_bottom = space_w + KB_GAP + bksp_w + KB_GAP + conn_w;
    int bx = (800 - total_bottom) / 2;
    if (ty >= y && ty < y + KB_KEY_H) {
        if (tx >= bx && tx < bx + space_w) return ' ';
        if (tx >= bx + space_w + KB_GAP && tx < bx + space_w + KB_GAP + bksp_w) return '\b';
        if (tx >= bx + space_w + KB_GAP + bksp_w + KB_GAP) return '\n';
    }

    return 0;
}

#define MAX_SCAN_RESULTS 15
static String s_scan_ssids[MAX_SCAN_RESULTS];
static int s_scan_count = 0;

static void do_wifi_setup() {
    gfx.fillScreen(COL_BG);
    gfx.fillRect(0, 0, 800, 50, 0x1A1A2E);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("WiFi Setup", 400, 25);

    // Show saved credentials if any
    Preferences wp;
    wp.begin("wifi", true);
    String saved_ssid = wp.getString("ssid", "");
    wp.end();
    if (saved_ssid.length()) {
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(COL_SUB);
        char info[80];
        snprintf(info, sizeof(info), "Current: %s", saved_ssid.c_str());
        gfx.drawString(info, 400, 45);
    }

    // Scan
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_AMBER);
    gfx.drawString("Scanning...", 400, 80);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    s_scan_count = 0;

    // Deduplicate and collect
    for (int i = 0; i < n && s_scan_count < MAX_SCAN_RESULTS; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        bool dup = false;
        for (int j = 0; j < s_scan_count; j++) {
            if (s_scan_ssids[j] == ssid) { dup = true; break; }
        }
        if (!dup) {
            s_scan_ssids[s_scan_count] = ssid;
            s_scan_count++;
        }
    }

    if (s_scan_count == 0) {
        gfx.fillRect(0, 60, 800, 30, COL_BG);
        gfx.setTextColor(0xFF4444);
        gfx.drawString("No networks found. Tap to go back.", 400, 80);
        while (!gfx.getTouch(&tx, &ty)) delay(50);
        while (gfx.getTouch(&tx, &ty)) delay(50);
        return;
    }

    // Draw network list
    gfx.fillRect(0, 55, 800, 420, COL_BG);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_SUB);
    gfx.setTextDatum(lgfx::middle_left);
    gfx.drawString("Tap a network:", 20, 70);

    // Back button
    gfx.fillRoundRect(680, 55, 100, 30, 8, 0xE74C3C);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("Back", 730, 70);

    int list_y = 90;
    for (int i = 0; i < s_scan_count; i++) {
        int rssi = 0;
        for (int j = 0; j < n; j++) {
            if (WiFi.SSID(j) == s_scan_ssids[i]) { rssi = WiFi.RSSI(j); break; }
        }
        gfx.fillRoundRect(20, list_y, 760, 34, 8, 0x17212B);
        gfx.setTextDatum(lgfx::middle_left);
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(COL_TEXT);
        char entry[60];
        snprintf(entry, sizeof(entry), "%s  (%d dBm)", s_scan_ssids[i].c_str(), rssi);
        gfx.drawString(entry, 30, list_y + 17);
        list_y += 38;
    }

    // Wait for network selection
    int selected = -1;
    while (selected < 0) {
        if (gfx.getTouch(&tx, &ty)) {
            // Back button
            if (tx >= 680 && tx <= 780 && ty >= 55 && ty <= 85) {
                while (gfx.getTouch(&tx, &ty)) delay(50);
                return;
            }
            // Network list
            int ly = 90;
            for (int i = 0; i < s_scan_count; i++) {
                if (ty >= ly && ty < ly + 34) { selected = i; break; }
                ly += 38;
            }
            while (gfx.getTouch(&tx, &ty)) delay(50);
        }
        delay(20);
    }

    // Highlight selected
    gfx.fillRoundRect(20, 90 + selected * 38, 760, 34, 8, 0x2980B9);
    gfx.setTextDatum(lgfx::middle_left);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString(s_scan_ssids[selected].c_str(), 30, 90 + selected * 38 + 17);
    delay(200);

    // Password entry screen
    gfx.fillRect(0, 55, 800, 420, COL_BG);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_SUB);
    char hdr[80];
    snprintf(hdr, sizeof(hdr), "Enter password for: %s", s_scan_ssids[selected].c_str());
    gfx.drawString(hdr, 400, 75);

    // Back button on password screen
    gfx.fillRoundRect(680, 55, 100, 30, 8, 0xE74C3C);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("Back", 730, 70);

    char pwd[65] = "";
    kb_shift = false;
    draw_keyboard(pwd);

    bool done = false;
    while (!done) {
        // Check back button first (above keyboard area)
        if (gfx.getTouch(&tx, &ty)) {
            if (ty >= 55 && ty <= 85 && tx >= 680 && tx <= 780) {
                while (gfx.getTouch(&tx, &ty)) delay(30);
                return;
            }
        }

        char c = kb_check_touch();
        if (c == 0) { delay(20); continue; }

        // Debounce: wait for release
        while (gfx.getTouch(&tx, &ty)) delay(30);

        if (c == '\n') {
            // Connect
            done = true;
        } else if (c == '\b') {
            int len = strlen(pwd);
            if (len > 0) pwd[len - 1] = '\0';
            draw_keyboard(pwd);
        } else if (c == 'S') {
            kb_shift = !kb_shift;
            draw_keyboard(pwd);
        } else if (strlen(pwd) < 63) {
            int len = strlen(pwd);
            pwd[len] = c;
            pwd[len + 1] = '\0';
            if (kb_shift) kb_shift = false;
            draw_keyboard(pwd);
        }
        delay(50);
    }

    // Connect — clear entire area below header
    gfx.fillRect(0, 55, 800, 425, COL_BG);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_AMBER);
    char conn_msg[80];
    snprintf(conn_msg, sizeof(conn_msg), "Connecting to %s...", s_scan_ssids[selected].c_str());
    gfx.drawString(conn_msg, 400, 240);

    WiFi.begin(s_scan_ssids[selected].c_str(), pwd);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 15000) {
            gfx.setTextColor(0xFF4444);
            gfx.drawString("Connection failed! Check password.", 400, 270);
            delay(3000);
            WiFi.disconnect();
            return;
        }
        delay(200);
    }

    // Save credentials
    Preferences sp;
    sp.begin("wifi", false);
    sp.putString("ssid", s_scan_ssids[selected]);
    sp.putString("pass", pwd);
    sp.end();

    gfx.fillRect(0, 220, 800, 60, COL_BG);
    gfx.setTextColor(COL_MESHCORE);
    char ok_msg[80];
    snprintf(ok_msg, sizeof(ok_msg), "Connected! IP: %s", WiFi.localIP().toString().c_str());
    gfx.drawString(ok_msg, 400, 240);
    gfx.setTextColor(COL_SUB);
    gfx.drawString("Credentials saved. Tap to go back.", 400, 270);

    WiFi.disconnect();

    while (!gfx.getTouch(&tx, &ty)) delay(50);
    while (gfx.getTouch(&tx, &ty)) delay(50);
}

// ---------------------------------------------------------------------------
// OTA: download a .bin from GitHub and flash to a partition
// ---------------------------------------------------------------------------
static bool ota_flash_one(const String& body, const char* asset_name,
                          const esp_partition_t* part, const char* label) {
    // Find asset URL
    char download_url[256] = "";
    int pos = 0;
    while (true) {
        int dl_pos = body.indexOf("\"browser_download_url\"", pos);
        if (dl_pos < 0) break;
        int url_start = body.indexOf('"', dl_pos + 21);
        int url_end = body.indexOf('"', url_start + 1);
        if (url_start >= 0 && url_end >= 0 && (url_end - url_start) < 250) {
            String asset_url = body.substring(url_start + 1, url_end);
            if (asset_url.endsWith(asset_name)) {
                strncpy(download_url, asset_url.c_str(), sizeof(download_url) - 1);
                break;
            }
        }
        pos = dl_pos + 21;
    }

    if (!download_url[0]) {
        char msg[80];
        snprintf(msg, sizeof(msg), "No %s in release", asset_name);
        draw_status(msg, 0xFF4444);
        delay(2000);
        return false;
    }

    // Download
    draw_status("Connecting...");
    WiFiClientSecure dl_client;
    dl_client.setInsecure();
    HTTPClient dl_http;
    dl_http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    dl_http.setTimeout(15000);

    if (!dl_http.begin(dl_client, download_url)) {
        draw_status("Download connect failed", 0xFF4444);
        delay(2000);
        return false;
    }

    esp_task_wdt_reset();
    int code = dl_http.GET();
    if (code != 200) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Download HTTP %d", code);
        draw_status(msg, 0xFF4444);
        dl_http.end();
        delay(2000);
        return false;
    }

    int total_size = dl_http.getSize();
    if (total_size <= 0 || (size_t)total_size > part->size) {
        draw_status("File too large or unknown size", 0xFF4444);
        dl_http.end();
        delay(2000);
        return false;
    }

    // Erase
    draw_progress(0, label);
    esp_task_wdt_reset();
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        draw_status("Erase failed!", 0xFF4444);
        dl_http.end();
        delay(2000);
        return false;
    }

    // Write chunks
    WiFiClient* stream = dl_http.getStreamPtr();
    int written = 0;
    uint8_t buf[4096];
    uint32_t last_data_ms = millis();

    while (written < total_size) {
        esp_task_wdt_reset();
        int avail = stream->available();
        if (avail > 0) {
            int len = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            if (len > 0) {
                err = esp_partition_write(part, written, buf, len);
                if (err != ESP_OK) {
                    draw_status("Write error!", 0xFF4444);
                    dl_http.end();
                    delay(2000);
                    return false;
                }
                written += len;
                int pct = (int)((uint64_t)written * 100 / total_size);
                draw_progress(pct, label);
                last_data_ms = millis();
            }
        } else if ((millis() - last_data_ms) > 30000) {
            draw_status("Download timeout", 0xFF4444);
            dl_http.end();
            delay(2000);
            return false;
        }
    }

    dl_http.end();
    return true;
}

static void do_ota_update() {
    // Read WiFi credentials from MeshCore's NVS
    Preferences wp;
    wp.begin("wifi", true);
    String ssid = wp.getString("ssid", "");
    String pass = wp.getString("pass", "");
    wp.end();

    if (ssid.length() == 0) {
        draw_status("No WiFi credentials. Configure WiFi in MeshCore first.", 0xFF4444);
        delay(3000);
        return;
    }

    // Read OTA repo from MeshCore's NVS
    Preferences op;
    op.begin("ota", true);
    String repo = op.getString("repo", "kgiannadakis/CrowPanel-DIS02050A");
    op.end();

    // Connect WiFi
    draw_status("Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t wifi_start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - wifi_start > 15000) {
            draw_status("WiFi connection failed!", 0xFF4444);
            WiFi.disconnect();
            delay(3000);
            return;
        }
        delay(200);
    }

    char ip_msg[64];
    snprintf(ip_msg, sizeof(ip_msg), "WiFi connected: %s", WiFi.localIP().toString().c_str());
    draw_status(ip_msg, COL_MESHCORE);
    delay(1000);

    // Fetch latest release metadata
    draw_status("Checking for updates...");
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", repo.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);
    http.setUserAgent("ESP32-OTA/1.0");

    esp_task_wdt_reset();
    if (!http.begin(client, url)) {
        draw_status("Failed to connect to GitHub", 0xFF4444);
        WiFi.disconnect();
        delay(3000);
        return;
    }

    int code = http.GET();
    esp_task_wdt_reset();
    if (code != 200) {
        char msg[48];
        snprintf(msg, sizeof(msg), "GitHub HTTP %d", code);
        draw_status(msg, 0xFF4444);
        http.end();
        WiFi.disconnect();
        delay(3000);
        return;
    }

    String body = http.getString();
    http.end();
    esp_task_wdt_reset();

    // Find partitions
    const esp_partition_t* ota0 = find_ota(0);
    const esp_partition_t* ota1 = find_ota(1);

    bool mc_ok = false, mt_ok = false;

    // Flash MeshCore
    if (ota0) {
        draw_status("Updating MeshCore...", COL_MESHCORE);
        mc_ok = ota_flash_one(body, "meshcore.bin", ota0, "MeshCore");
        if (mc_ok) {
            draw_status("MeshCore updated!", COL_MESHCORE);
            delay(1000);
        }
    }

    // Flash Meshtastic
    if (ota1) {
        draw_status("Updating Meshtastic...", COL_MESHTASTIC);
        mt_ok = ota_flash_one(body, "meshtastic.bin", ota1, "Meshtastic");
        if (mt_ok) {
            draw_status("Meshtastic updated!", COL_MESHTASTIC);
            delay(1000);
        }
    }

    WiFi.disconnect();

    // Summary
    if (mc_ok && mt_ok) {
        draw_status("Both firmwares updated! Tap to boot.", COL_MESHCORE);
    } else if (mc_ok) {
        draw_status("MeshCore updated. Meshtastic skipped.", COL_AMBER);
    } else if (mt_ok) {
        draw_status("Meshtastic updated. MeshCore skipped.", COL_AMBER);
    } else {
        draw_status("No updates applied.", 0xFF4444);
    }
    delay(3000);
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n===== BOOT SELECTOR RUNNING =====");
    Serial.printf("Partition: %s\n", esp_ota_get_running_partition()->label);

    // I2C + backlight wake
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
    bl_cmd(0x10);

    // Check partitions
    bool ota0_ok = has_firmware(find_ota(0));
    bool ota1_ok = has_firmware(find_ota(1));

    draw_ui(ota0_ok, ota1_ok);

    // Auto-boot countdown
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
                        gfx.fillRect(150, 400, 500, 35, COL_BG);
                        draw_status("Auto-boot cancelled. Tap a firmware.");
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
    while (true) {
        if (gfx.getTouch(&tx, &ty)) {
            delay(80);
            if (touch_in(BTN_A_X, BTN_Y, BTN_W, BTN_H) && ota0_ok)
                boot_slot(0);
            else if (touch_in(BTN_B_X, BTN_Y, BTN_W, BTN_H) && ota1_ok)
                boot_slot(1);
            else if (touch_in(UPD_X, UPD_Y, UPD_W, UPD_H)) {
                do_ota_update();
                ota0_ok = has_firmware(find_ota(0));
                ota1_ok = has_firmware(find_ota(1));
                draw_ui(ota0_ok, ota1_ok);
            }
            else if (touch_in(WIFI_BTN_X, UPD_Y, WIFI_BTN_W, UPD_H)) {
                do_wifi_setup();
                ota0_ok = has_firmware(find_ota(0));
                ota1_ok = has_firmware(find_ota(1));
                draw_ui(ota0_ok, ota1_ok);
            }
            while (gfx.getTouch(&tx, &ty)) delay(50);
        }
        delay(20);
    }
}

void loop() {}
