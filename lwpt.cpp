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
#include "Relay.hpp"
#include "WifiManager.hpp"
#include "HttpClient.hpp"
#include "HttpManager.hpp"

#include "Secret.hpp"

#ifndef HTTP_QUEUE_LEN
#define HTTP_QUEUE_LEN 20
#endif

QueueHandle_t httpQueue = nullptr;

struct HttpMessage {
  char* url;
  char* payload;
  char* content_type;
};

void send_job_update( const char* route ) {
  Job& job = Job::get_instance();

  HttpMessage* msg = (HttpMessage*) malloc( sizeof( HttpMessage ) );
  std::string data = "{ \"machine_id\": \"" + std::string( MACHINE ) + 
    "\", \"updated_on\": \"" + job.to_mdy_hms_local( job.get_updated_on() ) +
    "\", \"timedout_on\": \"" + job.to_mdy_hms_local( job.get_timedout_on() ) +
    "\", \"punch_count\": " + std::to_string( job.get_punch_count() ) +
    " }";

  msg->url = strdup( std::string( std::string( SERVER_URL ) + std::string( route ) ).c_str() );
  msg->payload = strdup( data.c_str() );
  msg->content_type = "application/json";

  xQueueSend( httpQueue, &msg, portMAX_DELAY );
}

void http_task( void* param ) {
  HttpMessage *msg;
  while ( true ) {
    if ( xQueueReceive( httpQueue, &msg, portMAX_DELAY ) == pdTRUE ) {
      auto res = HttpClient::get_instance()
                    .send_post( msg->url, msg->payload, msg->content_type );
      
      if ( res.transport == ESP_OK && res.status >= 200 && res.status < 300 ) {
        ESP_LOGI( "HTTP TASK", "OK: %s | %s", msg->url, msg->payload );
      } else {
        ESP_LOGE( "HTTP TASK", "FAIL %s | transport=%s status=%d", msg->url, esp_err_to_name( res.transport ), res.status );
        // TODO: enqueue to persistant retry
      }

      free( msg->payload );
      free( msg );
    }
  }
}

void main_task( void* param ) {
  bool previous_state, current_state = true;
  Sensor sensor;
  Relay relay;

  while( true ) {
    previous_state = current_state;
    current_state = sensor.get_status();

    Job& job = Job::get_instance();

    if ( job.is_running() ) {
      if ( current_state && !previous_state ) {
        job.increment();

        send_job_update( "update" );
      
        previous_state = current_state;
      }

      int delta = job.seconds_since_updated();
      
      if ( 
        ( delta > 9 && delta < 12 ) &&
        !job.is_warning_sent()
      ) {
        // TODO: Send HTTP Warning on 15min(900 sec)
        // Note: Should only send once
        send_job_update( "warning" );
        job.set_warning_sent( true );
      } 
      else if ( delta > 12 ) {
        job.mark_timed_out();

        send_job_update( "timedout" );
      }

      if ( relay.is_on() ) { relay.off(); }
    } else {
      if ( !relay.is_on() ) { relay.on(); }
    }

    vTaskDelay( pdMS_TO_TICKS( 100 ) );
  }
}

extern "C" void wifi_task( void* param ) {
  WifiManager* wifi = static_cast<WifiManager*>(param);
  ESP_LOGI( "WIFI TASK", "BEGINNING LISTENING!" );

  char last_ip[ 16 ] = "";

  auto start_time = std::chrono::steady_clock::now();

  while ( true ) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - start_time;

    if ( wifi->is_connected() ) {
      char ip_copy[ 16 ];
      strncpy( ip_copy, wifi->get_ip(), sizeof( ip_copy ) );
      ip_copy[ sizeof( ip_copy ) - 1 ] = '\0';

      if ( 
        ( strcmp( last_ip, ip_copy ) != 0 ) ||
        ( elapsed > ( std::chrono::seconds( 60 ) ) )
      ) {
        HttpMessage *msg = (HttpMessage*)malloc( sizeof( HttpMessage ) );
        std::string data = std::string( "{ \"machine\": \"" ) + MACHINE + 
          "\", \"ip\": \"" + ip_copy + 
          "\", \"punches\": \"" + PUNCHES + 
          "\", \"department\": \"" + DEPARTMENT +
          "\" }";
  
        msg->url = SERVER_URL "register";
        msg->payload = strdup( data.c_str() );
        msg->content_type = "application/json";

        xQueueSend( httpQueue, &msg, portMAX_DELAY );

        ESP_LOGI( "WIFI TASK", "Registered IP: %s", ip_copy );
        start_time = std::chrono::steady_clock::now();
      } 
      strncpy( last_ip, ip_copy, sizeof( last_ip ) );
      last_ip[ sizeof( last_ip ) - 1 ] = '\0';
    } else {
      last_ip[ 0 ] = '\0';
    }

    vTaskDelay( pdMS_TO_TICKS( 1000 ) );
  }
}

extern "C" void app_main(void)
{
  static WifiManager wifi( WIFI_SSID, WIFI_PASS );
  wifi.init( MACHINE_NAME );

  HttpManager http( MACHINE_NAME );
  http.start();

  HttpClient::get_instance().init();

  httpQueue = xQueueCreate( HTTP_QUEUE_LEN, sizeof( HttpMessage* ) );

  xTaskCreate( &wifi_task, "Wifi Task", 4096, &wifi, 3, NULL );
  xTaskCreate( &http_task, "Http Task", 8192, NULL, 4, NULL );
  xTaskCreate( &main_task, "Main Task", 4096, NULL, 5, NULL );
}
