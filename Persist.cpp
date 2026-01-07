#include "Persist.hpp"

#include <cstring>
#include <inttypes.h>

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_log.h"
#include "esp_rom_crc.h"   // esp_rom_crc32_le

static const char* TAG = "PERSIST";
static constexpr uint32_t kMagic = 0x4C575054; // 'LWPT'
static constexpr uint16_t kVersion = 1;

static constexpr char kNs[]   = "lwpt";
static constexpr char kKeyA[] = "state_a";
static constexpr char kKeyB[] = "state_b";

static uint32_t calc_crc(const PersistedState& s) {
  PersistedState tmp = s;
  tmp.crc32 = 0;
  return esp_rom_crc32_le(0, (const uint8_t*)&tmp, sizeof(tmp));
}

bool Persist::is_valid(const PersistedState& s) {
  if (s.magic != kMagic) return false;
  if (s.version != kVersion) return false;
  if (s.size != sizeof(PersistedState)) return false;
  return s.crc32 == calc_crc(s);
}

esp_err_t Persist::init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS init issue (%s), erasing...", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  return err;
}

static esp_err_t load_key(nvs_handle_t h, const char* key, PersistedState& out) {
  size_t len = sizeof(out);
  esp_err_t err = nvs_get_blob(h, key, &out, &len);
  if (err != ESP_OK) return err;
  if (len != sizeof(out)) return ESP_FAIL;
  if (!Persist::is_valid(out)) return ESP_FAIL;
  return ESP_OK;
}

esp_err_t Persist::load(PersistedState& out) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNs, NVS_READONLY, &h);
  if (err != ESP_OK) return err;

  PersistedState a{}, b{};
  bool okA = (load_key(h, kKeyA, a) == ESP_OK);
  bool okB = (load_key(h, kKeyB, b) == ESP_OK);
  nvs_close(h);

  if (!okA && !okB) return ESP_ERR_NOT_FOUND;

  if (okA && (!okB || a.seq >= b.seq)) {
    out = a;
  } else {
    out = b;
  }

  ESP_LOGI(TAG, 
           "Loaded state seq=%" PRIu32 " (running=%u punch=%" PRId32 " relay=%u)",
           out.seq, 
           (unsigned)out.running, 
           out.punch_count, 
           (unsigned)out.relay_on);
  return ESP_OK;
}

esp_err_t Persist::save(const PersistedState& in) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;

  // Decide which slot to overwrite based on latest seq
  PersistedState cur{};
  esp_err_t load_err = Persist::load(cur); // may fail if not found
  uint32_t cur_seq = (load_err == ESP_OK) ? cur.seq : 0;

  PersistedState next = in;
  next.magic = kMagic;
  next.version = kVersion;
  next.size = sizeof(PersistedState);
  next.seq = cur_seq + 1;
  next.crc32 = calc_crc(next);

  const char* target = (cur_seq % 2 == 0) ? kKeyB : kKeyA;

  err = nvs_set_blob(h, target, &next, sizeof(next));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved state -> %s seq=%" PRIu32, target, next.seq);
  } else {
    ESP_LOGE(TAG, "Save failed (%s)", esp_err_to_name(err));
  }
  return err;
}

