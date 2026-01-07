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
    struct Snapshot {
      int punch_count;
      time_t created_on;
      time_t updated_on;
      time_t timedout_on;
      bool running;
      bool warning_sent;
    };

    using ChangeHook = void (*)( void* ctx );

    static Job& get_instance() {
      static Job instance;
      return instance;
    }

    void set_change_hook( ChangeHook hook, void* ctx ) {
      xSemaphoreTake( mutex, portMAX_DELAY );
      change_hook = hook;
      change_hook_ctx = ctx;
      xSemaphoreGive( mutex );
    }

    void start_new() {
      const time_t now = time( nullptr );

      xSemaphoreTake( mutex, portMAX_DELAY );
      punch_count = 0;
      created_on = now;
      updated_on = now;
      timedout_on = 0;
      running = true;
      warning_sent = false;
      xSemaphoreGive( mutex );

      log( "JOB( START )" );
      notify_change();
    }

    void increment( int delta = 1 ) {
      const time_t now = time( nullptr );

      xSemaphoreTake( mutex, portMAX_DELAY );
      punch_count += delta;
      updated_on = now;
      warning_sent = false;
      xSemaphoreGive( mutex );

      log( "JOB( INCREMENT )" );
      notify_change();
    }

    void mark_timed_out() {
      const time_t now = time( nullptr );

      xSemaphoreTake( mutex, portMAX_DELAY );
      timedout_on = now;
      running = false;
      warning_sent = false;
      xSemaphoreGive( mutex );

      log( "JOB( TIMED OUT )" );
      notify_change();
    }

    void set_warning_sent( bool r ) {
      xSemaphoreTake( mutex, portMAX_DELAY );
      warning_sent = r;
      xSemaphoreGive( mutex );
      notify_change();
    }

    // Restore from persisted snapshot (does NOT auto-notify by default)
    void restore( const Snapshot& s, bool notify = false ) {
      xSemaphoreTake( mutex, portMAX_DELAY );
      punch_count = s.punch_count;
      created_on = s.created_on;
      updated_on = s.updated_on;
      timedout_on = s.timedout_on;
      running = s.running;
      warning_sent = s.warning_sent;
      xSemaphoreGive( mutex );

      log( "JOB( RESTORE )" );
      if ( notify ) notify_change();
    }

    // --------- Read-only ---------
    Snapshot snapshot() {
      Snapshot s{};
      xSemaphoreTake( mutex, portMAX_DELAY );
      s.punch_count = punch_count;
      s.created_on = created_on;
      s.updated_on = updated_on;
      s.timedout_on = timedout_on;
      s.running = running;
      s.warning_sent = warning_sent;
      xSemaphoreGive( mutex );
      return s;
    }

    bool is_running() {
      xSemaphoreTake( mutex, portMAX_DELAY );
      bool v = running;
      xSemaphoreGive( mutex );
      return v;
    }

    bool is_warning_sent() {
      xSemaphoreTake( mutex, portMAX_DELAY );
      bool v = warning_sent;
      xSemaphoreGive( mutex );
      return v;
    }

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

    static std::string to_mdy_hms_local( time_t t ) {
      if ( t == 0 ) return "N/A";
      struct tm tm_local;
      localtime_r( &t, &tm_local );

      char buf[ 32 ];
      const size_t n = strftime( buf, sizeof( buf ), "%m/%d/%Y %H:%M:%S", &tm_local );
      return ( n > 0 ) ? std::string( buf ) : std::string( "N/A" );
    }

  private:
    Job()
    : punch_count( 0 ),
      created_on( time( nullptr ) ),
      updated_on( created_on ),
      timedout_on( 0 ),
      running( false ),
      warning_sent( false ),
      mutex( xSemaphoreCreateMutex() ),
      change_hook( nullptr ),
      change_hook_ctx( nullptr ) {}

    Job( const Job& ) = delete;
    Job &operator=( const Job& ) = delete;

    void notify_change() {
      ChangeHook hook;
      void *ctx;
      
      xSemaphoreTake( mutex, portMAX_DELAY );
      hook = change_hook;
      ctx = change_hook_ctx;
      xSemaphoreGive( mutex );
      
      if ( hook ) hook( ctx );
    }

    void log( const char* tag ) {
      ESP_LOGI( 
        tag, 
        "punch_count=%d, created_on=%s, updated_on=%s, timedout_on=%s",
        punch_count,
        to_mdy_hms_local( created_on ).c_str(),
        to_mdy_hms_local( updated_on ).c_str(),
        to_mdy_hms_local( timedout_on ).c_str()
      );
    }

    int punch_count;
    time_t created_on;
    time_t updated_on;
    time_t timedout_on;
    bool running;
    bool warning_sent;

    SemaphoreHandle_t mutex;

    ChangeHook change_hook;
    void* change_hook_ctx;
};
