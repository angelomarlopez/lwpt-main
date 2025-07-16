#include "WifiManager.hpp"

#include <nvs_flash.h>
#include <esp_log.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_ip_addr.h>
#include <mdns.h>

#include <lwip/inet.h>

#include <cstring>
#include <assert.h>
#include <inttypes.h>

static const char* TAG = "WIFI MANAGER";

const char* WifiManager::hostname = NULL;
char WifiManager::ip[ 16 ] = {0};
bool WifiManager::connected = false;

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
  } 
}
