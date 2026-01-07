#include "WifiManager.hpp"

#include <nvs_flash.h>
#include <esp_log.h>

#include "freertos/task.h"
#include "esp_sntp.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_ip_addr.h>
#include <mdns.h>

#include <lwip/inet.h>

#include <cstring>
#include <assert.h>
#include <inttypes.h>
#include <ctime>
#include <cstdlib>

static const char* TAG = "WIFI MANAGER";

const char* WifiManager::hostname = NULL;
char WifiManager::ip[ 16 ] = {0};
bool WifiManager::connected = false;
bool WifiManager::time_synced = false;
bool WifiManager::time_sync_in_progress = false;

WifiManager::WifiManager( const char* ssid, const char* password ) : ssid( ssid ), password ( password ) {}

void WifiManager::init( const char* machine ) {
  hostname = machine;
  ESP_ERROR_CHECK( nvs_flash_init() );
  ESP_ERROR_CHECK( esp_netif_init() );
  ESP_ERROR_CHECK( esp_event_loop_create_default() );

  esp_netif_t* netif = esp_netif_create_default_wifi_sta();
  assert( netif );
  ESP_ERROR_CHECK( esp_netif_set_hostname( netif, hostname ) );

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init( &cfg ) );

  uint8_t mac[6];
  esp_err_t err = esp_wifi_get_mac( WIFI_IF_STA, mac );
  if ( err == ESP_OK ) {
    char macStr[ 18 ];
    snprintf( macStr, sizeof( macStr ),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[ 0 ], mac[ 1 ], mac[ 2 ], mac[ 3 ], mac[ 4 ], mac[ 5 ]
    );
    ESP_LOGI ( TAG, "ESP32 STA MAC ADDRESS: %s", macStr );
  } else {
    ESP_LOGE( TAG, "Failed to get Mac Address (%s)", esp_err_to_name( err ) );
  }

  esp_event_handler_instance_register( WIFI_EVENT, ESP_EVENT_ANY_ID, reinterpret_cast<esp_event_handler_t>(&WifiManager::wifi_event_handler), this, NULL );
  esp_event_handler_instance_register( IP_EVENT, IP_EVENT_STA_GOT_IP, reinterpret_cast<esp_event_handler_t>(&WifiManager::wifi_event_handler), this, NULL );

  wifi_config_t wifiConfig = {};
  std::strncpy( (char*)wifiConfig.sta.ssid, ssid, sizeof( wifiConfig.sta.ssid ) );
  std::strncpy( (char*)wifiConfig.sta.password, password, sizeof( wifiConfig.sta.password ) );

  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );
  ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &wifiConfig ) );
  ESP_ERROR_CHECK( esp_wifi_start() );

  // ESP_ERROR_CHECK( mdns_init() );
  // ESP_ERROR_CHECK( mdns_hostname_set( machine ) );
  // ESP_LOGI( TAG, "mDNS hostname set to: %s.local", machine );
}

void WifiManager::wifi_event_handler( void* arg, esp_event_base_t eventBase, int32_t eventID, void* eventData ) {
  ESP_LOGI( TAG, "wifi_event_handler triggered: \n\tbase=%s \n\tid=%" PRId32, eventBase, eventID );
  if ( eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_START ) { 
    ESP_LOGI( TAG, "INITIATING..." );
    esp_wifi_connect(); 
  } else if ( eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_CONNECTED ) {
    ESP_LOGI( TAG, "CONNECTED TO AP, WAITING ON DHCP..." );
  } else if ( eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_DISCONNECTED ) {
    ESP_LOGI( TAG, "FAILED TO CONNECT TO WIFI. RECONNECTING..." );
    connected = false;
    esp_wifi_connect();
  } else if ( eventBase == IP_EVENT && eventID == IP_EVENT_STA_LOST_IP ) {
    connected = false;
  } else if ( eventBase == IP_EVENT && eventID == IP_EVENT_STA_GOT_IP ) {
    ESP_LOGI( TAG, "CONNECTION SUCCESSFUL. SETTING UP MDNS..." );

    esp_err_t err = mdns_init();
    if ( err != ESP_OK ) {
      ESP_LOGE( TAG, "mDNS init failed: %s", esp_err_to_name( err ) );
    }
    err = mdns_hostname_set( hostname );
    if ( err != ESP_OK ) {
      ESP_LOGE( TAG, "Setting mDNS hostname failed: %s", esp_err_to_name( err ) );
    } else {
      ESP_LOGI( TAG, "mDNS hostname set to: %s.local", hostname );
    }

    ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
    esp_netif_ip_info_t ip_info = event->ip_info;

    const char* current_ip = ip4addr_ntoa( (const ip4_addr_t*)&ip_info.ip );

    // ip[ INET_ADDRSTRLEN ];
    strncpy( ip, current_ip, sizeof( ip ) );
    ip[ sizeof( ip )-1 ] = '\0';

    // ESP_LOGI( TAG, "GOT IP: " IPSTR "\n", IP2STR( &event->ip_info.ip ) );
    ESP_LOGI( TAG, "GOT IP: %s", ip );
    connected = true;

    start_time_sync();
  } 
}

void WifiManager::start_time_sync() {
  if ( time_synced || time_sync_in_progress ) { return; }
  time_sync_in_progress = true;

  BaseType_t ok = xTaskCreate( 
      &WifiManager::time_sync_task,
      "time_sync",
      4096,
      nullptr,
      5,
      nullptr
  );
  if ( ok != pdPASS ) {
    ESP_LOGE( TAG, "Failed to create time_sync task" );
    time_sync_in_progress = false;
  }
}

void WifiManager::time_sync_task( void* ) {
  setenv( "TZ", "CST6CDT,M3.2.0/2,M11.1.0/2", 1 );
  tzset();

  ESP_LOGI( TAG, "Starting SNTP time sync..." );

  esp_sntp_setoperatingmode( SNTP_OPMODE_POLL );
  esp_sntp_setservername( 0, "pool.ntp.org" );
  esp_sntp_init();

  time_t now = 0;
  struct tm timeinfo = {};

  const int max_retries = 20;
  for ( int i = 0; i < max_retries; ++i ) {
      time( &now );
      localtime_r( &now, &timeinfo );

      if ( ( timeinfo.tm_year + 1900 ) >= 2020 ) {
        char buf[ 32 ];
        strftime( buf, sizeof( buf ), "%m/%d/%Y %H:%M:%S", &timeinfo );
        ESP_LOGI( TAG, "Time synced: %s", buf );
        time_synced = true;
        break;
      }

      ESP_LOGI( TAG, "Waiting for SNTP... (%d/%d)", i + 1, max_retries );
      vTaskDelay( pdMS_TO_TICKS( 1000 ) );
  }

  if ( !time_synced ) {
    ESP_LOGW( TAG, "SNTP sync timed out; time may still be 1970." );
  }

  time_sync_in_progress = false;
  vTaskDelete( nullptr );
}

bool WifiManager::wait_for_time_sync( TickType_t timeout_ticks ) const {
  TickType_t start = xTaskGetTickCount();
  while ( !time_synced && ( xTaskGetTickCount() - start ) < timeout_ticks ) {
    vTaskDelay( pdMS_TO_TICKS( 200 ) );
  }
  return time_synced;
}
