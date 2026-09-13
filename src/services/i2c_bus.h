#pragma once
// Accessor for the single shared I2C bus (GPIO7=SDA, GPIO8=SCL) created once in
// app_main() for GT911 touch. The ES8311 audio codec sits on the same physical bus
// (see docs/BRINGUP.md "Audio codec bring-up") — do not call i2c_new_master_bus() a
// second time on these pins, it will fail with the bus already claimed. main.cpp
// registers the handle here right after creating it; audio_service reads it back.
#include "driver/i2c_master.h"

// Named i2c_bus_registry (not i2c_bus) because app_main()'s own local variable for the bus
// handle is itself named i2c_bus — a same-named namespace would be shadowed and unusable there.
namespace i2c_bus_registry {

void set_shared_bus(i2c_master_bus_handle_t bus);
i2c_master_bus_handle_t get_shared_bus();

} // namespace i2c_bus_registry
