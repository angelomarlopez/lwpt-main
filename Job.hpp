#pragma once
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Job {
  public:
    static Job& get_instance() {
      static Job instance;
      return instance;
    }

    void set( int p, int pg, int tc )  {
      xSemaphoreTake( mutex, portMAX_DELAY );
      punches = p;
      punchGoal = pg;
      totalCount = tc;
      xSemaphoreGive( mutex );
    }

    void get( int& p, int& pg, int& tc ) {
      xSemaphoreTake( mutex, portMAX_DELAY );
      p = punches;
      pg = punchGoal;
      tc = totalCount;
      xSemaphoreGive( mutex );
    }

    int punches;
    int punchGoal;
    int totalCount;

  private:
    Job() : punches( 0 ), punchGoal( 0 ), totalCount( 0 ) { mutex = xSemaphoreCreateMutex(); }
    Job( const Job& ) = delete;
    Job &operator=( const Job& ) = delete;

    SemaphoreHandle_t mutex;
};
