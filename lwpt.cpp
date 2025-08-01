#include <stdio.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

#include "Job.hpp"
#include "Sensor.hpp"
#include "WifiManager.hpp"
#include "HttpClient.hpp"
#include "HttpManager.hpp"

#include "Secret.hpp"

QueueHandle_t httpQueue;

struct HttpMessage {
  char* url;
  char* payload;
  char* content_type;
};

void send_log( const char* code ) {
  HttpMessage *msg = (HttpMessage*) malloc( sizeof( HttpMessage ) );
  
  std::string log = "{ \"machine\": \"" + std::string( MACHINE ) + "\", \"code\": \"" + std::string( code ) + "\" }";
  
  msg->url = LOG_URL;
  msg->payload = strdup( log.c_str() );
  msg->content_type = "application/json";

  xQueueSend( httpQueue, &msg, portMAX_DELAY );

  // HttpClient::get_instance().send_post( LOG_URL, log.c_str(), "application/json" );
}

void http_task( void* param ) {
  HttpMessage *msg;
  while ( true ) {
    if ( xQueueReceive( httpQueue, &msg, portMAX_DELAY ) == pdTRUE ) {
      HttpClient::get_instance().send_post( msg->url, msg->payload, msg->content_type );
      
      ESP_LOGI( "HTTP TASK", "SUCCESSFUL: %s | %s", msg->url, msg->payload );
      // free( msg->url );
      free( msg->payload );
      // free( msg->content_type );
      free( msg );
    }
  }
}

void sensor_task( void* param ) {
  bool previous_state, current_state = true;
  Sensor sensor;

  while( true ) {
    previous_state = current_state;
    current_state = sensor.get_status();

    if ( current_state && !previous_state ) {
      ESP_LOGI( "MAIN", "Proximity Sensor: TRIGGERED" );

      send_log( "TRIGGER" );
      
      previous_state = current_state;
    }

    vTaskDelay( pdMS_TO_TICKS( 100 ) );
  }
}

extern "C" void wifi_task( void* param ) {
  WifiManager* wifi = static_cast<WifiManager*>(param);
  ESP_LOGI( "WIFI TASK", "BEGINNING LISTENING!" );

  char last_ip[ 16 ];

  auto start_time = std::chrono::high_resolution_clock::now();

  while ( true ) {
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_sec = end_time - start_time;
    if ( wifi->is_connected() ) {
      const char* ip = wifi->get_ip();

      if ( 
        ( strcmp( last_ip, ip ) != 0 ) ||
        ( elapsed_sec > ( std::chrono::seconds( 10 ) ) )
      ) {
        HttpMessage *msg = (HttpMessage*) malloc( sizeof( HttpMessage ) );
        std::string data = "{ \"machine\": \"" + std::string( MACHINE ) + "\", \"ip\": \"" + std::string( ip ) + "\", \"punches\": \"" + std::string( PUNCHES ) + "\" }";
  
        msg->url = REGISTER_URL;
        msg->payload = strdup( data.c_str() );
        msg->content_type = "application/json";

        xQueueSend( httpQueue, &msg, portMAX_DELAY );

        ESP_LOGI( "WIFI TASK", "Registered IP: %s", ip );
        start_time = std::chrono::system_clock::now();
      } 

      strcpy( last_ip, ip );
    } else {
      strcpy( last_ip, "" );
    }

    vTaskDelay( pdMS_TO_TICKS( 1000 ) );
  }
}

extern "C" void app_main(void)
{
  static WifiManager wifi( WIFI_SSID, WIFI_PASS );
  wifi.init( MACHINE_NAME );

  HttpClient::get_instance().init();

  httpQueue = xQueueCreate( 10, sizeof( HttpMessage* ) );

  xTaskCreate( &wifi_task, "Wifi Task", 4096, &wifi, 3, NULL );
  xTaskCreate( &http_task, "Http Task", 8192, NULL, 4, NULL );
  xTaskCreate( &sensor_task, "Sensor Task", 4096, NULL, 5, NULL );
}
