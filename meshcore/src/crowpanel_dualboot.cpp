// =============================================================================
// crowpanel_dualboot.cpp — Add to MeshCore src/ folder
//
// 1. Erases otadata → boot selector always appears
// 2. Pre-mounts SPIFFS on "mcdata" partition before MeshCore's own
//    SPIFFS.begin() runs
// =============================================================================

#include <Arduino.h>
#include <esp_partition.h>
#include <SPIFFS.h>

extern "C" void initVariant() {
    // Erase otadata → forces boot selector on next reboot
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }

    // Pre-mount SPIFFS on MeshCore's own "mcdata" partition
    SPIFFS.begin(true, "/spiffs", 10, "mcdata");
}
