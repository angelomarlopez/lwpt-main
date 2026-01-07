#ifndef RELAY_HPP
#define RELAY_HPP

#include <cstdint>
#include "driver/gpio.h"

#define RELAY_PIN GPIO_NUM_26

class Relay {
public:
  using ChangeHook = void (*)( void* ctx );

  Relay() : state( true ), change_hook( nullptr ), change_hook_ctx( nullptr ) {
    gpio_config_t ioConf{};
    ioConf.pin_bit_mask = ( 1ULL << RELAY_PIN );
    ioConf.mode = GPIO_MODE_OUTPUT;
    ioConf.pull_up_en = GPIO_PULLUP_DISABLE;
    ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ioConf.intr_type = GPIO_INTR_DISABLE;

    gpio_config( &ioConf );
    
    on();
  }

  void set_change_hook( ChangeHook hook, void* ctx ) {
    change_hook = hook;
    change_hook_ctx = ctx;
  }

  void on() {
    if ( state ) return;
    state = true;
    gpio_set_level( RELAY_PIN, !state );
    notify_change();
  }

  void off() {
    if ( !state ) return;
    state = false;
    gpio_set_level( RELAY_PIN, !state );
    notify_change();
  }

  void set( bool on_state ) {
    if ( on_state ) on();
    else off();
  }

  bool is_on() const {
    return state;
  }

private:
  void notify_change() {
    if ( change_hook ) change_hook( change_hook_ctx );
  }

  bool state;

  ChangeHook change_hook;
  void* change_hook_ctx;
};

#endif
