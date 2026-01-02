#pragma once

#include <string>
#include <ctime>
#include <cstdio>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

class Job {
  public:
    static Job& get_instance() {
      static Job instance;
      return instance;
    }

    void start_new() {
      const time_t now = time( nullptr );
      xSemaphoreTake( mutex, portMAX_DELAY );
      punch_count = 0;
      created_on = now;
      updated_on = now;
      timedout_on = 0;
      running = true;
      xSemaphoreGive( mutex );

      log( "JOB( START )" );
    }

    void increment( int delta = 1 ) {
      const time_t now = time( nullptr );
      xSemaphoreTake( mutex, portMAX_DELAY );
      punch_count += delta;
      updated_on = now;
      xSemaphoreGive( mutex );
      log( "JOB( INCREMENT )" );
    }

    void mark_timed_out() {
      const time_t now = time( nullptr );
      xSemaphoreTake( mutex, portMAX_DELAY );
      timedout_on = now;
      running = false;
      xSemaphoreGive( mutex );
      log( "JOB( TIMED OUT )" );
    }

    void log( const char* f ) {
      ESP_LOGI( 
        f, 
        "punch_count=%d, created_on=%s, updated_on=%s, timedout_on=%s",
        punch_count,
        to_mdy_hms_local( created_on ).c_str(),
        to_mdy_hms_local( updated_on ).c_str(),
        to_mdy_hms_local( timedout_on ).c_str()
      );
    }

    static std::string to_mdy_hms_local( time_t t ) {
      if ( t == 0 ) return "N/A";
      struct tm tm_local;
      localtime_r( &t, &tm_local );

      char buf[ 32 ];
      const size_t n = strftime( buf, sizeof( buf ), "%m/%d/%Y %H:%M:%S", &tm_local );
      return ( n > 0 ) ? std::string( buf ) : std::string( "N/A" );
    }

    bool is_running() { return running; }

    time_t get_updated_on() {
      xSemaphoreTake( mutex, portMAX_DELAY );
      const time_t v = updated_on;
      xSemaphoreGive( mutex );
      return v;
    }
    
    time_t get_timedout_on() {
      xSemaphoreTake( mutex, portMAX_DELAY );
      const time_t v = timedout_on;
      xSemaphoreGive( mutex );
      return v;
    }

    int get_punch_count() {
      xSemaphoreTake( mutex, portMAX_DELAY );
      int pc = punch_count;
      xSemaphoreGive( mutex );
      return pc;
    }

    int64_t seconds_since_updated() {
      const time_t now = time( nullptr );
      return difftime( now, get_updated_on() );
    }

  private:
    Job()
    : punch_count( 0 ),
      created_on( time( nullptr ) ),
      updated_on( created_on ),
      timedout_on( 0 ),
      running( false ),
      mutex( xSemaphoreCreateMutex() ) {}

    Job( const Job& ) = delete;
    Job &operator=( const Job& ) = delete;

    int punch_count;
    time_t created_on;
    time_t updated_on;
    time_t timedout_on;
    bool running;

    SemaphoreHandle_t mutex;
};
