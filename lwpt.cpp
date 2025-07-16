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

void send_request() {
  HttpMessage *msg = (HttpMessage*) malloc(sizeof(HttpMessage));

  std::string requestBody = "{ \"Machine\": \"" + std::string( MACHINE ) + "\" }";
  
  msg->url = SERVER_URL;
  msg->payload = strdup( requestBody.c_str() );
  msg->content_type = "application/json";

  xQueueSend( httpQueue, &msg, portMAX_DELAY );

  // HttpClient::get_instance().send_post( SERVER_URL, requestBody.c_str(), "application/json" );
}

void load_data() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open( "storage", NVS_READONLY, &handle );
  if ( err == ESP_OK ) {
    int32_t value;

    int punches = 0, punchGoal = 0, totalCount = 0;

    if ( nvs_get_i32( handle, "punches", &value ) == ESP_OK ) { punches = value; }
    if ( nvs_get_i32( handle, "punchGoal", &value ) == ESP_OK ) { punchGoal = value; }
    if ( nvs_get_i32( handle, "totalCount", &value ) == ESP_OK ) { totalCount = value; }

    Job::get_instance().set( punches, punchGoal, totalCount );

    ESP_LOGI( "MAIN", "Loaded Data from NVS: punches=%d, punchGoal=%d, totalCount=%d", punches, punchGoal, totalCount );
  } else {
    ESP_LOGE( "MAIN", "Error Opening NVS for load_data: %s", esp_err_to_name( err ) );
  }
}

static void save_data() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open( "storage", NVS_READWRITE, &handle );
  if ( err == ESP_OK ) {
    int punches = 0, punchGoal = 0, totalCount = 0;

    Job::get_instance().get( punches, punchGoal, totalCount );

    nvs_set_i32( handle, "punches", punches );
    nvs_set_i32( handle, "punchGoal", punchGoal );
    nvs_set_i32( handle, "totalCount", totalCount );
    nvs_commit( handle );
    nvs_close( handle );
    ESP_LOGI( "MAIN", "Saved data to NVS: punches=%d, punchGoal=%d, totalCount=%d", punches, punchGoal, totalCount );
  } else {
    ESP_LOGE( "MAIN", "Error Opening NVS for save_data: %s", esp_err_to_name( err ) );
  }
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
    int punches = 0, punchGoal = 0, totalCount = 0;

    Job::get_instance().get( punches, punchGoal, totalCount );

    previous_state = current_state;
    current_state = sensor.get_status();

    if ( current_state && !previous_state ) {
      ESP_LOGI( "MAIN", "Proximity Sensor: TRIGGERED" ); 
      
      if ( punchGoal != 0 ) {
        punches++;
        if ( punches == punchGoal ) {
          punches = 0;
          totalCount += 1;

          ESP_LOGI ( "MAIN", "Goal Reached!\n\tTotal Count: %d", totalCount );

          send_request();
        }

        Job::get_instance().set( punches, punchGoal, totalCount ); 

        save_data();
      }

      send_log( "TRIGGER" );
      ESP_LOGI( "MAIN", "Status: %d | Punches: %d | Punch Goal: %d | Total Count: %d", current_state, punches, punchGoal, totalCount ); 

      previous_state = current_state;
    }

    vTaskDelay( pdMS_TO_TICKS( 100 ) );
  }
}

extern "C" void wifi_task( void* param ) {
  WifiManager* wifi = static_cast<WifiManager*>(param);
  ESP_LOGI( "WIFI TASK", "BEGINNING LISTENING!" );

  char last_ip[ 16 ];

  while ( true ) {
    if ( wifi->is_connected() ) {
      const char* ip = wifi->get_ip();

      if ( 
        ( strcmp( last_ip, ip ) != 0 )
      ) {
        HttpMessage *msg = (HttpMessage*) malloc( sizeof( HttpMessage ) );
        std::string data = "{ \"machine\": \"" + std::string( MACHINE ) + "\", \"ip\": \"" + std::string( ip ) + "\" }";
  
        msg->url = REGISTER_URL;
        msg->payload = strdup( data.c_str() );
        msg->content_type = "application/json";

        xQueueSend( httpQueue, &msg, portMAX_DELAY );

        ESP_LOGI( "WIFI TASK", "Registered IP: %s", ip );
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

  load_data();

  HttpClient::get_instance().init();

  HttpManager http( MACHINE_NAME );
  http.start();

  httpQueue = xQueueCreate( 10, sizeof( HttpMessage* ) );

  xTaskCreate( &wifi_task, "Wifi Task", 4096, &wifi, 3, NULL );
  xTaskCreate( &http_task, "Http Task", 8192, NULL, 4, NULL );
  xTaskCreate( &sensor_task, "Sensor Task", 4096, NULL, 5, NULL );
}
