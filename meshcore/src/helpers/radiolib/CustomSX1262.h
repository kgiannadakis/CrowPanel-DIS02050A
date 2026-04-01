#pragma once

#include <RadioLib.h>

#define SX126X_IRQ_HEADER_VALID                     0b0000010000  // 4 valid LoRa header received
#define SX126X_IRQ_PREAMBLE_DETECTED                0x04

class CustomSX1262 : public SX1262 {
public:
  CustomSX1262(Module *mod) : SX1262(mod) { }

  // Cache last set SF so wrappers don't touch RadioLib private fields
  uint8_t getSpreadingFactorCached() const { return _sf_cache; }

  // Expose protected setDioIrqParams via a public wrapper (RadioLib changed access)
  int16_t setDioIrqParamsPublic(uint16_t irqMask,
                                uint16_t dio1Mask,
                                uint16_t dio2Mask = RADIOLIB_SX126X_IRQ_NONE,
                                uint16_t dio3Mask = RADIOLIB_SX126X_IRQ_NONE) {
    return SX1262::setDioIrqParams(irqMask, dio1Mask, dio2Mask, dio3Mask);
  }

  // Hide base method and cache SF (works even if base is not virtual)
  int16_t setSpreadingFactor(uint8_t sf) {
    _sf_cache = sf;
    return SX1262::setSpreadingFactor(sf);
  }

#ifdef RP2040_PLATFORM
  bool std_init(SPIClassRP2040* spi = NULL)
#else
  bool std_init(SPIClass* spi = NULL)
#endif
  {
#ifdef SX126X_DIO3_TCXO_VOLTAGE
    float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
#else
    float tcxo = 1.6f;
#endif

#ifdef LORA_CR
    uint8_t cr = LORA_CR;
#else
    uint8_t cr = 5;
#endif

#if defined(P_LORA_SCLK)
  #ifdef NRF52_PLATFORM
    if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
  #elif defined(RP2040_PLATFORM)
    if (spi) {
      spi->setMISO(P_LORA_MISO);
      spi->setSCK(P_LORA_SCLK);
      spi->setMOSI(P_LORA_MOSI);
      spi->begin();
    }
  #else
    if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  #endif
#endif

    int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                       RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                       LORA_TX_POWER, 8, tcxo);

    // Cache the initial SF too
    _sf_cache = (uint8_t)LORA_SF;

    // If radio init fails with -707/-706, try again with tcxo voltage set to 0.0f
    if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
      tcxo = 0.0f;
      status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                     RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                     LORA_TX_POWER, 8, tcxo);
      _sf_cache = (uint8_t)LORA_SF;
    }

    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: radio init failed: ");
      Serial.println(status);
      return false;
    }

    setCRC(1);
    explicitHeader();

#ifdef SX126X_CURRENT_LIMIT
    setCurrentLimit(SX126X_CURRENT_LIMIT);
#endif
#ifdef SX126X_DIO2_AS_RF_SWITCH
    setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
#endif
#ifdef SX126X_RX_BOOSTED_GAIN
    setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
#endif
#if defined(SX126X_RXEN) || defined(SX126X_TXEN)
#ifndef SX126X_RXEN
#define SX126X_RXEN RADIOLIB_NC
#endif
#ifndef SX126X_TXEN
#define SX126X_TXEN RADIOLIB_NC
#endif
    setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
#endif

#ifdef SX126X_REGISTER_PATCH
    uint8_t r_data = 0;
    readRegister(0x8B5, &r_data, 1);
    r_data |= 0x01;
    writeRegister(0x8B5, &r_data, 1);
#endif

    return true;
  }

  bool isReceiving() {
    uint16_t irq = getIrqFlags();
    bool detected = (irq & SX126X_IRQ_HEADER_VALID) || (irq & SX126X_IRQ_PREAMBLE_DETECTED);
    return detected;
  }

private:
  uint8_t _sf_cache = 10;
};