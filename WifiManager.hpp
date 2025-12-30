#ifndef WIFI_MANAGER_HPP
#define WIFI_MANAGER_HPP

#include <esp_wifi.h>
#include <esp_event.h>
#include <string.h>

class WifiManager {
public:
  WifiManager( const char* ssid, const char* password );
  void init( const char* machine );

  const char* get_ip() const {
    return ip;
  }

  bool is_connected() const {
    return connected;
  }

private:
  const char* ssid;
  const char* password;
  static const char* hostname;
  static char ip[ 16 ];
  static bool connected;
  static bool time_synced;
  static bool time_sync_in_progress;

  static void wifi_event_handler( void*, esp_event_base_t, int32_t, void* );

  static void start_time_sync();
  static void time_sync_task( void* );
};

#endif
