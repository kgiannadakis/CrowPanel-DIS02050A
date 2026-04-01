// =============================================================================
// pins_arduino.h for CrowPanel DIS05020A v1.1
// Required by Arduino ESP32 core (esp32-hal-gpio.h includes this)
// =============================================================================

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

// USB
#define USB_VID          0x303A
#define USB_PID          0x1001
#define USB_MANUFACTURER "Elecrow"
#define USB_PRODUCT      "CrowPanel-DIS05020A"

// Default Serial (UART0)
static const uint8_t TX = 43;
static const uint8_t RX = 44;

// Default I2C — touch + backlight controller
static const uint8_t SDA = 15;
static const uint8_t SCL = 16;

// Default SPI (FSPI) — LoRa radio SX1262
static const uint8_t SS   = 8;   // LORA_NSS
static const uint8_t MOSI = 6;
static const uint8_t MISO = 4;
static const uint8_t SCK  = 5;

// ADC
static const uint8_t A0 = 1;
static const uint8_t A1 = 2;

// No LED on this board
#define LED_BUILTIN       -1
#define BUILTIN_LED       LED_BUILTIN

// Pin aliases
#define PIN_WIRE_SDA      SDA
#define PIN_WIRE_SCL      SCL
#define PIN_SPI_SS        SS
#define PIN_SPI_MOSI      MOSI
#define PIN_SPI_MISO      MISO
#define PIN_SPI_SCK       SCK

#endif /* Pins_Arduino_h */
