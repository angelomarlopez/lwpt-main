#include <stdio.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "Job.hpp"
#include "Sensor.hpp"
#include "Relay.hpp"
#include "WifiManager.hpp"
#include "HttpClient.hpp"
#include "HttpManager.hpp"

#include "Persist.hpp"
#include "Secret.hpp"

#ifndef HTTP_QUEUE_LEN
#define HTTP_QUEUE_LEN 20
#endif

QueueHandle_t httpQueue = nullptr;

struct HttpMessage {
  char* url;
  char* payload;
  const char* content_type;
};

// -------------------- Persistence Wiring -------------------
static volatile bool s_persist_dirty = false;

static void persist_mark_dirty( void* ) {
  s_persist_dirty = true;
}

static PersistedState build_persisted_state( Relay& relay ) {
  Job& job = Job::get_instance();
  Job::Snapshot js = job.snapshot();

  PersistedState ps{};
  ps.magic = 0;
  ps.version = 0;
  ps.size = 0;
  ps.seq = 0;
  ps.crc32 = 0;

  ps.punch_count = (int32_t)js.punch_count;
  ps.created_on = (int64_t)js.created_on;
  ps.updated_on = (int64_t)js.updated_on;
  ps.timedout_on = (int64_t)js.timedout_on;
  ps.running = js.running ? 1 : 0;
  ps.warning_sent = js.warning_sent ? 1 : 0;

  ps.relay_on = relay.is_on() ? 1 : 0;
  ps._pad = 0;

  return ps;
}

static void apply_persisted_state( const PersistedState& ps, Relay& relay ) {
  Job& job = Job::get_instance();

  Job::Snapshot js{};
  js.punch_count = (int)ps.punch_count;
  js.created_on = (time_t)ps.created_on;
  js.updated_on = (time_t)ps.updated_on;
  js.timedout_on = (time_t)ps.timedout_on;
  js.running = ps.running != 0;
  js.warning_sent = ps.warning_sent != 0;

  job.restore( js, false );
  relay.set( ps.relay_on != 0 );
}

static void persist_task( void* param ) {
  Relay& relay = *static_cast<Relay*>(param);

  // Debounce + min write interval (flash wear)
  const int64_t kMinIntervalUs = 2LL * 1000LL * 1000LL; // 2s
  int64_t last_save_us = 0;

  // If anything changes very early, we'll catch it 
  s_persist_dirty = false;

  while ( true ) {
    if ( s_persist_dirty ) {
      const int64_t now_us = esp_timer_get_time();
      if ( ( now_us - last_save_us ) >= kMinIntervalUs ) {
        PersistedState ps = build_persisted_state( relay );
        if ( Persist::save( ps ) == ESP_OK ) {
          last_save_us = now_us;
          s_persist_dirty = false;
        }
      }
    }

    vTaskDelay( pdMS_TO_TICKS( 200 ) );
  }
}

// -----------------------------------------------------------

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

      free( msg->url );
      free( msg->payload );
      free( msg );
    }
  }
}

void main_task( void* param ) {
  Relay& relay = *static_cast<Relay*>( param );

  bool previous_state, current_state = true;
  Sensor sensor;

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
        ( delta > 900 && delta < 1200 ) &&
        !job.is_warning_sent()
      ) {
        send_job_update( "warning" );
        job.set_warning_sent( true );
      } 
      else if ( delta > 1200 ) {
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
          "\", \"work_center\": \"" + WORK_CENTER + 
          "\" }";
  
        msg->url = strdup( SERVER_URL "register" );
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
  ESP_LOGW( "BOOT", "Reset Reason: %d", (int)esp_reset_reason() );
  
  // 1) init NVS + persistence
  ESP_ERROR_CHECK( Persist::init() );

  // 2) Create Relay once and pass it to tasks
  static Relay relay;

  // 3) Hook job + relay changes to mark flash-save dirty
  Job::get_instance().set_change_hook( &persist_mark_dirty, nullptr );
  relay.set_change_hook( &persist_mark_dirty, nullptr );

  // 4) Restore persisted state (if present)
  PersistedState ps{};
  if ( Persist::load( ps ) == ESP_OK ) {
    apply_persisted_state( ps, relay );
    // Important: don't immediately re-save right after restore
    s_persist_dirty = false;
  }

  static WifiManager wifi( WIFI_SSID, WIFI_PASS );
  wifi.init( MACHINE_NAME );

  bool ok = wifi.wait_for_time_sync( pdMS_TO_TICKS( 25000 ) );
  ESP_LOGI( "BOOT", "Time synced? %s", ok ? "YES" : "NO" );

  HttpManager http( MACHINE_NAME );
  http.start();

  HttpClient::get_instance().init();

  httpQueue = xQueueCreate( HTTP_QUEUE_LEN, sizeof( HttpMessage* ) );

  xTaskCreate( &wifi_task, "Wifi Task", 4096, &wifi, 3, NULL );
  xTaskCreate( &http_task, "Http Task", 8192, NULL, 4, NULL );
  xTaskCreate( &main_task, "Main Task", 4096, &relay, 5, NULL );

  // Persist Task (low priority)
  xTaskCreate( &persist_task, "Persist Task", 4096, &relay, 2, NULL );
}
