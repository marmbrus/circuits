#pragma once

#include <stdbool.h>

namespace config { class IOConfig; }

namespace i2c_logic {

// Applies LOCK_KEYPAD logic to the given IOConfig instance.
// module_name is like "io1".."io8" for logging context.
// Returns true if it modified any switch state.
bool apply_lock_keypad_logic(config::IOConfig& cfg, const char* module_name);

}


