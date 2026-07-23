#include "nodes/GDExtensionSmokeTest.h"

#include <godot_cpp/variant/utility_functions.hpp>

namespace tiles {

void GDExtensionSmokeTest::_bind_methods() {}

void GDExtensionSmokeTest::_ready() {
    godot::UtilityFunctions::print("[tiles] gdextension smoke test ready");
}

} // tiles
