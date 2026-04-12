// =============================================================================
// Dual-Boot Selector — CrowPanel DIS02050A v1.1
// Flashed to "factory" partition. Shows two touch buttons on boot.
// Saves last choice to NVS for auto-boot with 3-second countdown.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

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
static bool s_update_backlight_dimmed = false;

// I2C helpers
static bool i2c_probe(uint8_t a) { Wire.beginTransmission(a); return !Wire.endTransmission(); }
static void bl_cmd(uint8_t c) { Wire.beginTransmission(0x30); Wire.write(c); Wire.endTransmission(); }

// CrowPanel backlight controller commands.
static const uint8_t BL_OFF_CMD = 0x05;
static const uint8_t BL_ON_CMD = 0x10;
static const uint8_t BL_TOUCH_WAKE_CMD = 0x19;

static void selector_backlight_on() {
    bl_cmd(BL_ON_CMD);
    s_update_backlight_dimmed = false;
}

static void selector_backlight_off() {
    bl_cmd(BL_OFF_CMD);
    s_update_backlight_dimmed = true;
}

// Button geometry
static const int BTN_Y = 120, BTN_H = 170, BTN_GAP = 40;
static const int BTN_W = 350;
static const int BTN_A_X = (800 - 2*BTN_W - BTN_GAP) / 2;
static const int BTN_B_X = BTN_A_X + BTN_W + BTN_GAP;
static const int ACTION_Y = 320, ACTION_H = 58, ACTION_W = 260, ACTION_GAP = 30;
static const int UPDATE_X = (800 - 2*ACTION_W - ACTION_GAP) / 2;
static const int WIFI_X = UPDATE_X + ACTION_W + ACTION_GAP;
static const int BACK_X = 20, BACK_Y = 410, BACK_W = 150, BACK_H = 50;
static const int CONTINUE_X = 560, CONTINUE_Y = 410, CONTINUE_W = 210, CONTINUE_H = 50;

// Colors
static const uint32_t COL_BG         = 0x0B1A30;
static const uint32_t COL_HEADER     = 0x1A1A2E;
static const uint32_t COL_MESHCORE   = 0x2ECC71;
static const uint32_t COL_MESHTASTIC = 0x3498DB;
static const uint32_t COL_UPDATE     = 0x9B7BFF;
static const uint32_t COL_WIFI       = 0xE74C3C;
static const uint32_t COL_DISABLED   = 0x1A1A2E;
static const uint32_t COL_TEXT       = 0xFFFFFF;
static const uint32_t COL_SUB        = 0xA0B4C8;
static const uint32_t COL_WARN       = 0xFFB020;
static const uint32_t COL_ERR        = 0xFF4444;
static const uint32_t COL_OK         = 0x33DD77;

static const char *WIFI_NS = "selwifi";
static const char *DUALBOOT_NS = "dualboot";
static const char *GITHUB_API = "https://api.github.com/repos/kgiannadakis/CrowPanel-DIS02050A/releases/latest";
static const char *UA = "CrowPanel-DIS02050A-Selector/2.0";

struct FirmwareAssets {
    String tag;
    String meshcoreUrl;
    String meshtasticUrl;
};

static void status_screen(const char *title, const String &line1,
                          const String &line2, uint32_t color, bool back);

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

static bool firmware_version(const esp_partition_t* p, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return false;
    out[0] = '\0';
    if (!p || !has_firmware(p)) return false;

    esp_app_desc_t desc = {};
    if (esp_ota_get_partition_description(p, &desc) != ESP_OK) return false;
    if (!desc.version[0]) return false;

    snprintf(out, out_sz, "v%s", desc.version);
    return true;
}

static void boot_slot(int slot) {
    const esp_partition_t* p = find_ota(slot);
    if (!p || !has_firmware(p)) {
        status_screen("Boot", "No firmware in this slot.", "", COL_ERR, false);
        delay(1800);
        return;
    }
    prefs.begin(DUALBOOT_NS, false);
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

static void wait_touch_release() {
    while (gfx.getTouch(&tx, &ty)) delay(40);
}

static bool wait_for_touch(uint32_t timeoutMs = 0) {
    uint32_t start = millis();
    while (timeoutMs == 0 || millis() - start < timeoutMs) {
        if (gfx.getTouch(&tx, &ty)) {
            delay(70);
            return true;
        }
        delay(20);
    }
    return false;
}

static void draw_rect_button(int x, int y, int w, int h, uint32_t col,
                             const char *label, bool enabled = true,
                             const char *sub = nullptr) {
    gfx.fillRoundRect(x, y, w, h, 14, enabled ? col : COL_DISABLED);
    gfx.drawRoundRect(x, y, w, h, 14, enabled ? 0xFFFFFF : 0x333333);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(enabled ? COL_TEXT : 0x666666);
    gfx.drawString(label, x + w / 2, y + h / 2 - (sub ? 9 : 0));
    if (sub) {
        gfx.setTextColor(enabled ? 0xE0E0E0 : 0x555555);
        gfx.drawString(sub, x + w / 2, y + h / 2 + 15);
    }
}

static void draw_back_button() {
    draw_rect_button(BACK_X, BACK_Y, BACK_W, BACK_H, 0x34495E, "Back");
}

static void draw_continue_button() {
    draw_rect_button(CONTINUE_X, CONTINUE_Y, CONTINUE_W, CONTINUE_H, COL_OK, "Continue");
}

static void draw_title(const char *title, const char *subtitle = nullptr) {
    gfx.fillScreen(COL_BG);
    gfx.fillRect(0, 0, 800, 70, COL_HEADER);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString(title, 400, 24);
    if (subtitle) {
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(COL_SUB);
        gfx.drawString(subtitle, 400, 54);
    }
}

static void status_screen(const char *title, const String &line1,
                          const String &line2 = String(),
                          uint32_t color = COL_SUB,
                          bool back = true) {
    bool reveal_after_draw = s_update_backlight_dimmed;
    draw_title(title);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(color);
    gfx.drawString(line1, 400, 205);
    if (line2.length()) gfx.drawString(line2, 400, 235);
    if (back) draw_back_button();
    if (reveal_after_draw) {
        selector_backlight_on();
        delay(80);
    }
}

static void wait_back() {
    while (true) {
        if (wait_for_touch()) {
            if (touch_in(BACK_X, BACK_Y, BACK_W, BACK_H)) {
                wait_touch_release();
                return;
            }
            wait_touch_release();
        }
    }
}

// ---------------------------------------------------------------------------
// Draw UI
// ---------------------------------------------------------------------------
static void draw_button(int x, int y, int w, int h, uint32_t col,
                        const char* title, const char* sub, bool valid) {
    bool has_sub = sub && sub[0];
    gfx.fillRoundRect(x, y, w, h, 16, valid ? col : COL_DISABLED);
    if (!valid) gfx.drawRoundRect(x, y, w, h, 16, 0x333333);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(valid ? COL_TEXT : 0x666666);
    gfx.drawString(title, x+w/2, y+h/2 - (has_sub ? 20 : 0));
    if (has_sub) {
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(valid ? 0xD0D0D0 : 0x444444);
        gfx.drawString(sub, x+w/2, y+h/2 + 15);
    }
    if (!valid) {
        gfx.setTextColor(0xFF4444);
        gfx.setFont(&fonts::Font2);
        gfx.drawString("[empty slot]", x+w/2, y+h/2 + (has_sub ? 40 : 35));
    }
}

static void draw_ui(bool ota0_ok, bool ota1_ok) {
    draw_title("MeshCore/Meshtastic Dual-Boot by KaA",
               "Tap a firmware to boot  |  Auto-boots last choice in 3s");
    char meshcoreVer[40];
    char meshtasticVer[40];
    const char *meshcoreSub = firmware_version(find_ota(0), meshcoreVer, sizeof(meshcoreVer)) ? meshcoreVer : nullptr;
    const char *meshtasticSub = firmware_version(find_ota(1), meshtasticVer, sizeof(meshtasticVer)) ? meshtasticVer : nullptr;

    // Firmware buttons
    draw_button(BTN_A_X, BTN_Y, BTN_W, BTN_H, COL_MESHCORE,
                "MeshCore", meshcoreSub, ota0_ok);
    draw_button(BTN_B_X, BTN_Y, BTN_W, BTN_H, COL_MESHTASTIC,
                "Meshtastic", meshtasticSub, ota1_ok);
    // Maintenance buttons
    draw_rect_button(UPDATE_X, ACTION_Y, ACTION_W, ACTION_H, COL_UPDATE,
                     "Update Firmware");
    draw_rect_button(WIFI_X, ACTION_Y, ACTION_W, ACTION_H, COL_WIFI,
                     "WiFi Setup");

    // Footer
    gfx.setTextDatum(lgfx::bottom_center);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("Bootloader version: 2.0 12/04/2026 | Baked by Kostis Giannadakis", 400, 470);
}

// ---------------------------------------------------------------------------
// Auto-boot countdown
// ---------------------------------------------------------------------------
static void draw_countdown(int secs, int slot) {
    gfx.fillRect(150, 418, 500, 28, COL_BG);
    char msg[64];
    snprintf(msg, sizeof(msg), "Auto-booting %s in %ds...",
             slot == 0 ? "MeshCore" : "Meshtastic", secs);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_SUB);
    gfx.drawString(msg, 400, 432);
}

// ---------------------------------------------------------------------------
// WiFi setup
// ---------------------------------------------------------------------------
static bool load_wifi(String &ssid, String &pass) {
    prefs.begin(WIFI_NS, true);
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
    prefs.end();
    return ssid.length() > 0;
}

static void save_wifi(const String &ssid, const String &pass) {
    prefs.begin(WIFI_NS, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

static void forget_wifi() {
    prefs.begin(WIFI_NS, false);
    prefs.clear();
    prefs.end();
    WiFi.disconnect(true, true);
}

static bool connect_wifi(const String &ssid, const String &pass,
                         bool saveOnSuccess, bool waitOnSuccess = true) {
    status_screen("WiFi", "Connecting to " + ssid + "...", "", COL_SUB, false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(200);
    WiFi.begin(ssid.c_str(), pass.c_str());

    for (int i = 0; i < 30; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            if (saveOnSuccess) save_wifi(ssid, pass);
            status_screen("WiFi", "Connected to " + ssid,
                          WiFi.localIP().toString(), COL_OK, true);
            if (waitOnSuccess) wait_back();
            else delay(500);
            return true;
        }
        gfx.fillRect(250, 260, 300, 28, COL_BG);
        gfx.setTextDatum(lgfx::middle_center);
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(COL_SUB);
        gfx.drawString("Please wait " + String(15 - i / 2) + "s", 400, 275);
        delay(500);
    }

    status_screen("WiFi", "Connection failed.", "Check the password and try again.", COL_ERR, true);
    wait_back();
    return false;
}

static bool connect_saved_wifi(bool showErrors, bool waitOnSuccess = true) {
    if (WiFi.status() == WL_CONNECTED) return true;
    String ssid, pass;
    if (!load_wifi(ssid, pass)) {
        if (showErrors) {
            status_screen("WiFi", "No saved WiFi network.", "Open WiFi Setup first.", COL_WARN, true);
            wait_back();
        }
        return false;
    }
    return connect_wifi(ssid, pass, false, waitOnSuccess);
}

static bool connect_saved_wifi_silent() {
    if (WiFi.status() == WL_CONNECTED) return true;
    String ssid, pass;
    if (!load_wifi(ssid, pass)) return false;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(200);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(500);
    }
    return false;
}

enum KeyboardAction : uint8_t {
    KB_CHAR,
    KB_BACKSPACE,
    KB_SHIFT,
    KB_ALPHA,
    KB_SYMBOLS,
    KB_SPACE,
    KB_SHOW,
    KB_CONNECT,
};

struct KeyboardKey {
    int x;
    int y;
    int w;
    int h;
    const char *label;
    KeyboardAction action;
};

static const int KB_TOP = 142;
static const int KB_ROW_H = 36;
static const int KB_ROW_GAP = 5;
static const int KB_KEY_GAP = 6;
static const int KB_MAX_KEYS = 64;

static int add_key(KeyboardKey keys[], int count, int x, int y, int w,
                   const char *label, KeyboardAction action) {
    if (count >= KB_MAX_KEYS) return count;
    keys[count++] = {x, y, w, KB_ROW_H, label, action};
    return count;
}

static int add_uniform_row(KeyboardKey keys[], int count, int y,
                           const char * const labels[], int keyCount,
                           int keyW, KeyboardAction defaultAction) {
    int totalW = keyCount * keyW + (keyCount - 1) * KB_KEY_GAP;
    int x = (800 - totalW) / 2;
    for (int i = 0; i < keyCount; i++) {
        KeyboardAction action = defaultAction;
        if (strcmp(labels[i], "Del") == 0) action = KB_BACKSPACE;
        count = add_key(keys, count, x + i * (keyW + KB_KEY_GAP), y, keyW, labels[i], action);
    }
    return count;
}

static int add_meshcore_style_letters(KeyboardKey keys[], int count, int mode, bool showPass) {
    static const char * const digits[] = {"1","2","3","4","5","6","7","8","9","0","Del"};
    static const char * const lower1[] = {"q","w","e","r","t","y","u","i","o","p"};
    static const char * const lower2[] = {"a","s","d","f","g","h","j","k","l"};
    static const char * const lower3[] = {"z","x","c","v","b","n","m",".",",","!"};
    static const char * const upper1[] = {"Q","W","E","R","T","Y","U","I","O","P"};
    static const char * const upper2[] = {"A","S","D","F","G","H","J","K","L"};
    static const char * const upper3[] = {"Z","X","C","V","B","N","M",".",",","!"};
    static const char * const sym1[] = {"+","-","*","/","=","%","!","?","@","#"};
    static const char * const sym2[] = {"(",")","{","}","[","]","\\",";","\"","'"};
    static const char * const sym3[] = {"_","~","<",">","$","^","&",".",",",":"};

    int y0 = KB_TOP;
    int y1 = y0 + KB_ROW_H + KB_ROW_GAP;
    int y2 = y1 + KB_ROW_H + KB_ROW_GAP;
    int y3 = y2 + KB_ROW_H + KB_ROW_GAP;
    int y4 = y3 + KB_ROW_H + KB_ROW_GAP;

    count = add_uniform_row(keys, count, y0, digits, 11, 63, KB_CHAR);

    const char * const *row1 = (mode == 1) ? upper1 : (mode == 2 ? sym1 : lower1);
    const char * const *row2 = (mode == 1) ? upper2 : (mode == 2 ? sym2 : lower2);
    const char * const *row3 = (mode == 1) ? upper3 : (mode == 2 ? sym3 : lower3);

    count = add_uniform_row(keys, count, y1, row1, 10, 68, KB_CHAR);

    int row2KeyW = 58;
    int enterW = 130;
    int row2TotalW = 9 * row2KeyW + enterW + 9 * KB_KEY_GAP;
    int x = (800 - row2TotalW) / 2;
    for (int i = 0; i < 9; i++) {
        count = add_key(keys, count, x + i * (row2KeyW + KB_KEY_GAP), y2, row2KeyW, row2[i], KB_CHAR);
    }
    count = add_key(keys, count, x + 9 * (row2KeyW + KB_KEY_GAP), y2, enterW, "Connect", KB_CONNECT);

    count = add_uniform_row(keys, count, y3, row3, 10, 63, KB_CHAR);

    x = 36;
    count = add_key(keys, count, x, y4, 76, mode == 1 ? "aa" : "Aa", KB_SHIFT);
    x += 76 + KB_KEY_GAP;
    count = add_key(keys, count, x, y4, 90, showPass ? "Hide" : "Show", KB_SHOW);
    x += 90 + KB_KEY_GAP;
    count = add_key(keys, count, x, y4, 76, mode == 2 ? "abc" : "1#", mode == 2 ? KB_ALPHA : KB_SYMBOLS);
    x += 76 + KB_KEY_GAP;
    count = add_key(keys, count, x, y4, 250, "Space", KB_SPACE);
    x += 250 + KB_KEY_GAP;
    count = add_key(keys, count, x, y4, 76, "?", KB_CHAR);
    x += 76 + KB_KEY_GAP;
    count = add_key(keys, count, x, y4, 130, "Connect", KB_CONNECT);

    return count;
}

static int build_password_keyboard(KeyboardKey keys[], int mode, bool showPass) {
    return add_meshcore_style_letters(keys, 0, mode, showPass);
}

static void draw_password_keyboard(const String &ssid, const String &pass,
                                   bool showPass, int mode) {
    draw_title("WiFi Password", ssid.c_str());
    gfx.setTextDatum(lgfx::middle_left);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_SUB);
    gfx.drawString("Password:", 50, 95);
    gfx.fillRoundRect(170, 78, 580, 42, 8, 0x101F38);
    gfx.setTextColor(COL_TEXT);
    String shown = showPass ? pass : String("");
    if (!showPass) {
        for (uint32_t i = 0; i < pass.length(); i++) shown += "*";
    }
    if (!shown.length()) shown = "(empty/open network)";
    if (shown.length() > 43) shown = shown.substring(shown.length() - 43);
    gfx.drawString(shown, 185, 100);

    KeyboardKey keys[KB_MAX_KEYS];
    int keyCount = build_password_keyboard(keys, mode, showPass);
    for (int i = 0; i < keyCount; i++) {
        uint32_t color = 0x233A5E;
        if (keys[i].action == KB_CONNECT) color = COL_OK;
        else if (keys[i].action != KB_CHAR) color = 0x34495E;
        draw_rect_button(keys[i].x, keys[i].y, keys[i].w, keys[i].h, color, keys[i].label);
    }

    draw_back_button();
}

static bool password_screen(const String &ssid, String &password) {
    bool showPass = false;
    int mode = 0;
    while (true) {
        draw_password_keyboard(ssid, password, showPass, mode);
        if (!wait_for_touch()) continue;

        if (touch_in(BACK_X, BACK_Y, BACK_W, BACK_H)) {
            wait_touch_release();
            return false;
        }

        KeyboardKey keys[KB_MAX_KEYS];
        int keyCount = build_password_keyboard(keys, mode, showPass);
        for (int i = 0; i < keyCount; i++) {
            if (!touch_in(keys[i].x, keys[i].y, keys[i].w, keys[i].h)) continue;

            switch (keys[i].action) {
                case KB_CHAR:
                    if (password.length() < 63) password += keys[i].label;
                    break;
                case KB_BACKSPACE:
                    if (password.length()) password.remove(password.length() - 1);
                    break;
                case KB_SHIFT:
                    mode = (mode == 1) ? 0 : 1;
                    break;
                case KB_ALPHA:
                    mode = 0;
                    break;
                case KB_SYMBOLS:
                    mode = 2;
                    break;
                case KB_SPACE:
                    if (password.length() < 63) password += " ";
                    break;
                case KB_SHOW:
                    showPass = !showPass;
                    break;
                case KB_CONNECT:
                    wait_touch_release();
                    return true;
            }
            break;
        }
        wait_touch_release();
    }
}

static int scan_wifi(String ssids[], int32_t rssis[], uint8_t enc[], int maxItems) {
    status_screen("WiFi Setup", "Scanning networks...", "", COL_SUB, false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(200);
    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) return 0;
    int count = n < maxItems ? n : maxItems;
    for (int i = 0; i < count; i++) {
        ssids[i] = WiFi.SSID(i);
        rssis[i] = WiFi.RSSI(i);
        enc[i] = WiFi.encryptionType(i);
    }
    WiFi.scanDelete();
    return count;
}

static void draw_network_list(String ssids[], int32_t rssis[], uint8_t enc[], int count) {
    draw_title("WiFi Setup", "Select a network");
    for (int i = 0; i < count; i++) {
        int y = 92 + i * 44;
        gfx.fillRoundRect(55, y, 690, 38, 8, 0x162A48);
        gfx.setTextDatum(lgfx::middle_left);
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(COL_TEXT);
        String label = ssids[i].length() ? ssids[i] : "(hidden network)";
        if (label.length() > 34) label = label.substring(0, 34) + "...";
        gfx.drawString(label, 75, y + 19);
        gfx.setTextDatum(lgfx::middle_right);
        gfx.setTextColor(COL_SUB);
        String meta = String(rssis[i]) + " dBm";
        if (enc[i] == WIFI_AUTH_OPEN) meta += "  open";
        gfx.drawString(meta, 725, y + 19);
    }
    draw_rect_button(560, 410, 150, 50, 0x34495E, "Rescan");
    draw_back_button();
}

static void wifi_setup_screen() {
    while (true) {
        String savedSsid, savedPass;
        bool hasSaved = load_wifi(savedSsid, savedPass);

        draw_title("WiFi Setup", "Connect the selector to your network");
        draw_rect_button(95, 125, 285, 70, COL_WIFI, "Scan Networks");
        draw_rect_button(420, 125, 285, 70, 0x27AE60, "Connect Saved", hasSaved,
                         hasSaved ? savedSsid.c_str() : "none saved");
        draw_rect_button(95, 225, 285, 70, 0x7F8C8D, "Forget Saved", hasSaved);
        draw_rect_button(420, 225, 285, 70, 0x34495E, "Back");

        gfx.setTextDatum(lgfx::middle_center);
        gfx.setFont(&fonts::Font2);
        gfx.setTextColor(WiFi.status() == WL_CONNECTED ? COL_OK : COL_SUB);
        String line = WiFi.status() == WL_CONNECTED
            ? "WiFi: " + WiFi.SSID() + "  |  IP: " + WiFi.localIP().toString()
            : "WiFi is not connected";
        gfx.drawString(line, 400, 350);

        if (!wait_for_touch()) continue;
        if (touch_in(420, 225, 285, 70)) {
            wait_touch_release();
            return;
        } else if (touch_in(420, 125, 285, 70) && hasSaved) {
            wait_touch_release();
            connect_wifi(savedSsid, savedPass, false);
        } else if (touch_in(95, 225, 285, 70) && hasSaved) {
            wait_touch_release();
            forget_wifi();
            status_screen("WiFi Setup", "Saved WiFi credentials removed.", "", COL_OK, true);
            wait_back();
        } else if (touch_in(95, 125, 285, 70)) {
            wait_touch_release();
            const int maxNets = 7;
            String ssids[maxNets];
            int32_t rssis[maxNets] = {0};
            uint8_t enc[maxNets] = {0};
            int count = scan_wifi(ssids, rssis, enc, maxNets);
            if (count == 0) {
                status_screen("WiFi Setup", "No networks found.", "Move closer or try Rescan.", COL_WARN, true);
                wait_back();
                continue;
            }
            bool selecting = true;
            while (selecting) {
                draw_network_list(ssids, rssis, enc, count);
                if (!wait_for_touch()) continue;
                if (touch_in(BACK_X, BACK_Y, BACK_W, BACK_H)) {
                    wait_touch_release();
                    selecting = false;
                } else if (touch_in(560, 410, 150, 50)) {
                    wait_touch_release();
                    count = scan_wifi(ssids, rssis, enc, maxNets);
                } else {
                    for (int i = 0; i < count; i++) {
                        int y = 92 + i * 44;
                        if (touch_in(55, y, 690, 38)) {
                            String pass = "";
                            wait_touch_release();
                            if (enc[i] == WIFI_AUTH_OPEN || password_screen(ssids[i], pass)) {
                                connect_wifi(ssids[i], pass, true);
                                selecting = false;
                            }
                            break;
                        }
                    }
                }
            }
        }
        wait_touch_release();
    }
}

static void draw_update_confirm() {
    draw_title("Update Firmware");
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("You are about to update the device's firmware.", 400, 115);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(COL_ERR);
    gfx.drawString("Your settings might be lost!", 400, 165);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("The screen will turn dark for a few minutes.", 400, 220);
    gfx.drawString("Do not turn it off or unplug it until", 400, 255);
    gfx.drawString("you see the successfully updated screen.", 400, 285);
    gfx.setTextColor(COL_WARN);
    gfx.drawString("Press Continue to proceed.", 400, 335);
    draw_back_button();
    draw_continue_button();
}

static bool confirm_update_screen() {
    draw_update_confirm();
    while (true) {
        if (!wait_for_touch()) continue;
        if (touch_in(BACK_X, BACK_Y, BACK_W, BACK_H)) {
            wait_touch_release();
            return false;
        }
        if (touch_in(CONTINUE_X, CONTINUE_Y, CONTINUE_W, CONTINUE_H)) {
            wait_touch_release();
            return true;
        }
        wait_touch_release();
    }
}

static void draw_update_in_progress() {
    draw_title("Update Firmware");
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(COL_WARN);
    gfx.drawString("Update in progress.", 400, 170);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("The device will reboot automatically.", 400, 225);
    gfx.setTextColor(COL_ERR);
    gfx.drawString("Do not unplug the device during the update.", 400, 265);
}

static void draw_update_success() {
    draw_title("Update Firmware");
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setFont(&fonts::Font4);
    gfx.setTextColor(COL_OK);
    gfx.drawString("Update successful.", 400, 200);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COL_TEXT);
    gfx.drawString("Rebooting in 10 seconds...", 400, 255);
}

// ---------------------------------------------------------------------------
// GitHub release update
// ---------------------------------------------------------------------------
static bool extract_json_string(const String &json, const String &key,
                                int start, String &out, int *endPos = nullptr) {
    int keyPos = json.indexOf("\"" + key + "\"", start);
    if (keyPos < 0) return false;
    int colon = json.indexOf(':', keyPos);
    int q1 = json.indexOf('"', colon + 1);
    if (colon < 0 || q1 < 0) return false;

    out = "";
    bool esc = false;
    for (int i = q1 + 1; i < json.length(); i++) {
        char c = json[i];
        if (esc) {
            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else out += c;
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            if (endPos) *endPos = i + 1;
            return true;
        } else {
            out += c;
        }
    }
    return false;
}

static int asset_score(const String &name, bool meshcore) {
    String n = name;
    n.toLowerCase();
    if (!n.endsWith(".bin")) return -1000;
    if (n.indexOf("bootloader") >= 0 || n.indexOf("partition") >= 0 ||
        n.indexOf("selector") >= 0 || n.indexOf("factory") >= 0 ||
        n.indexOf("littlefs") >= 0 || n.indexOf("spiffs") >= 0) {
        return -1000;
    }
    if (meshcore) {
        if (n == "meshcore.bin") return 100;
        int s = 0;
        if (n.indexOf("meshcore") >= 0) s += 50;
        if (n.indexOf("crowpanel") >= 0) s += 10;
        if (n.indexOf("v11") >= 0 || n.indexOf("lvgl") >= 0) s += 5;
        return s;
    }
    if (n == "meshtastic.bin") return 100;
    int s = 0;
    if (n.indexOf("meshtastic") >= 0) s += 50;
    if (n.indexOf("elecrow-adv1-43-50-70-tft") >= 0) s += 25;
    if (n.indexOf("crowpanel-dis05020a") >= 0) s += 20;
    if (n.indexOf("firmware") >= 0) s += 5;
    return s;
}

static bool fetch_latest_assets(FirmwareAssets &assets) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, GITHUB_API)) return false;
    http.addHeader("User-Agent", UA);
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        status_screen("Update Firmware", "GitHub request failed.",
                      "HTTP " + String(code), COL_ERR, true);
        wait_back();
        return false;
    }
    String json = http.getString();
    http.end();

    extract_json_string(json, "tag_name", 0, assets.tag);

    int bestMesh = -1000;
    int bestMt = -1000;
    int pos = 0;
    while (true) {
        int urlKey = json.indexOf("\"browser_download_url\"", pos);
        if (urlKey < 0) break;

        String url;
        int next = 0;
        if (!extract_json_string(json, "browser_download_url", urlKey, url, &next)) break;

        int nameKey = json.lastIndexOf("\"name\"", urlKey);
        String name;
        if (nameKey >= 0 && extract_json_string(json, "name", nameKey, name)) {
            int ms = asset_score(name, true);
            int mts = asset_score(name, false);
            if (ms > bestMesh) {
                bestMesh = ms;
                assets.meshcoreUrl = url;
            }
            if (mts > bestMt) {
                bestMt = mts;
                assets.meshtasticUrl = url;
            }
        }
        pos = next > urlKey ? next : urlKey + 1;
    }

    return bestMesh > 0 && bestMt > 0;
}

static bool write_url_to_partition(const char *label, const String &url,
                                   const esp_partition_t *part) {
    if (!part) {
        status_screen("Update Firmware", String(label) + " partition not found.", "", COL_ERR, true);
        wait_back();
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    if (!http.begin(client, url)) {
        status_screen("Update Firmware", String(label) + " download could not start.", "", COL_ERR, true);
        wait_back();
        return false;
    }
    http.addHeader("User-Agent", UA);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        status_screen("Update Firmware", String(label) + " download failed.",
                      "HTTP " + String(code), COL_ERR, true);
        wait_back();
        return false;
    }

    int len = http.getSize();
    if (len > 0 && (uint32_t)len > part->size) {
        http.end();
        status_screen("Update Firmware", String(label) + " is too large.",
                      String(len) + " bytes > slot " + String(part->size), COL_ERR, true);
        wait_back();
        return false;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        http.end();
        status_screen("Update Firmware", String(label) + " erase failed.",
                      "esp_ota_begin: " + String((int)err), COL_ERR, true);
        wait_back();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t *buf = (uint8_t *)malloc(4096);
    if (!buf) {
        esp_ota_abort(handle);
        http.end();
        status_screen("Update Firmware", "Out of memory.", "", COL_ERR, true);
        wait_back();
        return false;
    }

    size_t written = 0;
    uint32_t lastData = millis();
    bool ok = true;

    while (http.connected() && (len < 0 || written < (size_t)len)) {
        size_t avail = stream->available();
        if (avail) {
            int rd = stream->readBytes(buf, avail > 4096 ? 4096 : avail);
            if (rd <= 0) continue;
            err = esp_ota_write(handle, buf, rd);
            if (err != ESP_OK) {
                ok = false;
                break;
            }
            written += rd;
            lastData = millis();
        } else {
            if (millis() - lastData > 25000) {
                ok = false;
                break;
            }
            delay(10);
        }
    }

    free(buf);
    http.end();

    if (!ok || written == 0) {
        esp_ota_abort(handle);
        status_screen("Update Firmware", String(label) + " write failed.",
                      String(written / 1024) + " KB written", COL_ERR, true);
        wait_back();
        return false;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK || !has_firmware(part)) {
        status_screen("Update Firmware", String(label) + " validation failed.",
                      "esp_ota_end: " + String((int)err), COL_ERR, true);
        wait_back();
        return false;
    }

    return true;
}

static void update_firmware_screen() {
    if (!confirm_update_screen()) return;

    draw_update_in_progress();
    delay(2000);
    selector_backlight_off();
    delay(80);

    if (!connect_saved_wifi_silent()) {
        status_screen("Update Firmware", "WiFi connection failed.",
                      "Open WiFi Setup and try again.", COL_ERR, true);
        wait_back();
        return;
    }

    FirmwareAssets assets;
    if (!fetch_latest_assets(assets)) {
        status_screen("Update Firmware", "Could not find release assets.",
                      "Expected meshcore.bin and meshtastic.bin.", COL_ERR, true);
        wait_back();
        return;
    }

    bool mc = write_url_to_partition("MeshCore", assets.meshcoreUrl, find_ota(0));
    if (!mc) return;
    bool mt = write_url_to_partition("Meshtastic", assets.meshtasticUrl, find_ota(1));
    if (!mt) return;

    draw_update_success();
    selector_backlight_on();
    delay(10000);
    esp_restart();
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    // I2C + backlight wake
    Wire.begin(15, 16);
    Wire.setClock(400000);
    delay(50);
    for (int i = 0; i < 20; i++) {
        if (i2c_probe(0x30) && i2c_probe(0x5D)) break;
        bl_cmd(BL_TOUCH_WAKE_CMD);
        pinMode(1, OUTPUT); digitalWrite(1, LOW); delay(120);
        pinMode(1, INPUT); delay(100);
    }

    // Display init
    gfx.init();
    gfx.setRotation(0);
    gfx.fillScreen(0);
    selector_backlight_on();

    // Try saved WiFi briefly so WiFi Setup and Update can reuse the connection.
    String ssid, pass;
    if (load_wifi(ssid, pass)) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 1800) delay(50);
    }

    // Check partitions
    bool ota0_ok = has_firmware(find_ota(0));
    bool ota1_ok = has_firmware(find_ota(1));

    draw_ui(ota0_ok, ota1_ok);

    // Auto-boot countdown
    prefs.begin(DUALBOOT_NS, true);
    int last = prefs.getInt("last", -1);
    prefs.end();

    if (last >= 0 && last <= 1) {
        const esp_partition_t* saved = find_ota(last);
        if (has_firmware(saved)) {
            for (int i = 3; i > 0; i--) {
                draw_countdown(i, last);
                for (int t = 0; t < 20; t++) {
                    if (gfx.getTouch(&tx, &ty)) {
                        gfx.fillRect(120, 415, 560, 35, COL_BG);
                        gfx.setTextDatum(lgfx::middle_center);
                        gfx.setFont(&fonts::Font2);
                        gfx.setTextColor(COL_SUB);
                        gfx.drawString("Auto-boot cancelled. Tap a button.", 400, 432);
                        wait_touch_release();
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
            else if (touch_in(UPDATE_X, ACTION_Y, ACTION_W, ACTION_H)) {
                wait_touch_release();
                update_firmware_screen();
                ota0_ok = has_firmware(find_ota(0));
                ota1_ok = has_firmware(find_ota(1));
                draw_ui(ota0_ok, ota1_ok);
            } else if (touch_in(WIFI_X, ACTION_Y, ACTION_W, ACTION_H)) {
                wait_touch_release();
                wifi_setup_screen();
                ota0_ok = has_firmware(find_ota(0));
                ota1_ok = has_firmware(find_ota(1));
                draw_ui(ota0_ok, ota1_ok);
            }
            wait_touch_release();
        }
        delay(20);
    }
}

void loop() {}
