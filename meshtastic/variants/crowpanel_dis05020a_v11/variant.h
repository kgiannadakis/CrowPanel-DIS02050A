// =============================================================================
// Meshtastic variant.h for Elecrow CrowPanel DIS05020A v1.1
// ESP32-S3 + SX1262 + 800x480 RGB TFT + GT911 Touch
//
// Pin mapping derived from a known-working MeshCore build (v1.14)
//
// NOTE: Many defines (USE_SX1262, LORA_SCK, HAS_SCREEN, etc.) are set
//       via -D flags in platformio.ini. Only put things HERE that are
//       not already in build_flags, to avoid "redefined" warnings.
// =============================================================================

#ifndef _VARIANT_CROWPANEL_DIS05020A_V11_
#define _VARIANT_CROWPANEL_DIS05020A_V11_

// ---------------------------------------------------------------------------
// Hardware model — PRIVATE_HW for community / DIY builds
// ---------------------------------------------------------------------------
#define PRIVATE_HW_MODEL meshtastic_HardwareModel_PRIVATE_HW

// ---------------------------------------------------------------------------
// SX1262 — DIO1 quirk (uncomment if you get -706 radio init errors)
// ---------------------------------------------------------------------------
// #define SX126X_DIO1_QUIRK 1

// ---------------------------------------------------------------------------
// Display — 800x480 RGB parallel panel
// The RGB bus pins are configured inside the LovyanGFX driver class,
// not via defines. We just list them here for reference.
// ---------------------------------------------------------------------------
// RGB TFT B[0:4] = 21, 47, 48, 45, 38
// RGB TFT G[0:5] = 9, 10, 11, 12, 13, 14
// RGB TFT R[0:4] = 7, 17, 18, 3, 46
// HENABLE=42  VSYNC=41  HSYNC=40  PCLK=39

// ---------------------------------------------------------------------------
// No GPS on this board
// ---------------------------------------------------------------------------
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// ---------------------------------------------------------------------------
// Battery — not wired on DIS05020A.
// Meshtastic's Power.cpp unconditionally declares an adc_channel variable
// using ADC_CHANNEL, so we must define a dummy value even though
// BATTERY_PIN is -1 and it will never be read.
// ---------------------------------------------------------------------------
#define ADC_CHANNEL ADC1_CHANNEL_0

// ---------------------------------------------------------------------------
// Buzzer / Speaker — SPK connector (uncomment if wired)
// ---------------------------------------------------------------------------
// #define PIN_BUZZER 0

// ---------------------------------------------------------------------------
// USB
// ---------------------------------------------------------------------------
#define HAS_USB 1

// ---------------------------------------------------------------------------
// Bluetooth + WiFi
// ---------------------------------------------------------------------------
#define HAS_BLUETOOTH 1
#define HAS_WIFI      1

// Default to 20 dBm; higher power remains a deliberate manual setting.
#define DEFAULT_LORA_TX_POWER 20

#endif // _VARIANT_CROWPANEL_DIS05020A_V11_
