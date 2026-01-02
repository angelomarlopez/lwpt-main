#ifndef RELAY_HPP
#define RELAY_HPP

#include "driver/gpio.h"

#define RELAY_PIN GPIO_NUM_26

class Relay {
  gpio_config_t ioConf;

public:
  Relay() : state( true ) {
    ioConf = {
      .pin_bit_mask = ( 1ULL << RELAY_PIN ),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config( &ioConf );
    
    on();
  }

  void on() {
    state = true;
    gpio_set_level( RELAY_PIN, !state );
  }

  void off() {
    state = false;
    gpio_set_level( RELAY_PIN, !state );
  }

  bool is_on() {
    return state;
  }

  bool state;
};

#endif
