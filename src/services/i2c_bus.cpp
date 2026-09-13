#include "i2c_bus.h"

namespace i2c_bus_registry {

static i2c_master_bus_handle_t s_bus = nullptr;

void set_shared_bus(i2c_master_bus_handle_t bus) { s_bus = bus; }
i2c_master_bus_handle_t get_shared_bus() { return s_bus; }

} // namespace i2c_bus_registry
