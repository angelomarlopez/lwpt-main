#pragma once

#include <cstdint>
#include <cstddef>
#include <ctime>

#include "esp_err.h"

struct PersistedState {
  uint32_t magic;     // 'LWPT'
  uint16_t version;   // struct version
  uint16_t size;      // sizeof(PersistedState)

  uint32_t seq;       // monotonic commit sequence
  uint32_t crc32;     // crc of struct w/ crc32 field set to 0

  // Job
  int32_t punch_count;
  int64_t created_on;
  int64_t updated_on;
  int64_t timedout_on;
  uint8_t running;
  uint8_t warning_sent;

  // Relay
  uint8_t relay_on;

  uint8_t _pad;       // alignment
};

namespace Persist {
  // Initialize NVS (call once at boot)
  esp_err_t init();

  // Load newest valid state (A/B), returns ESP_OK or ESP_ERR_NOT_FOUND
  esp_err_t load(PersistedState& out);

  // Save state (writes to alternate slot), returns ESP_OK on success
  esp_err_t save(const PersistedState& in);

  // Helpers
  bool is_valid(const PersistedState& s);
}

