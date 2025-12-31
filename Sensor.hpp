#ifndef SENSOR_HPP
#define SENSOR_HPP

#include "driver/gpio.h"

#define SENSOR_PIN GPIO_NUM_32

class Sensor {
  gpio_config_t ioConf;

public:
  Sensor() {
    ioConf = {
      .pin_bit_mask = ( 1ULL << SENSOR_PIN ),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config( &ioConf );
  }

  bool get_status() {
    return !gpio_get_level( SENSOR_PIN );
  }
};

#endif
