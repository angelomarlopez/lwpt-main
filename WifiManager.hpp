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
    char* copy = new char[ 16 ];
    strncpy( copy, ip, 16 );
    return copy;
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

  static void wifi_event_handler( void*, esp_event_base_t, int32_t, void* );
};

#endif
