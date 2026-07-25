#include "register_types.h"

#include "game/Editor.h"
#include "game/resources/LevelResources.h"
#include "game/testing/ResourceIntegrationRunner.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

void initialize_tiles_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Resources first, so every class a node may construct or load already
    // exists in ClassDB. Only resource and node classes enter ClassDB: no core,
    // content, or engine value is ever bound to Godot.
    GDREGISTER_CLASS(tiles::game::PolygonResource);
    GDREGISTER_CLASS(tiles::game::PaletteEntryResource);
    GDREGISTER_CLASS(tiles::game::PaletteResource);
    GDREGISTER_CLASS(tiles::game::RegionResource);
    GDREGISTER_CLASS(tiles::game::LevelResource);

    // Registered only because its own dedicated test scene instantiates it.
    // Registration does not place it in the main application scene.
    GDREGISTER_CLASS(tiles::game::ResourceIntegrationRunner);

    GDREGISTER_CLASS(tiles::game::Editor);
}

void uninitialize_tiles_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {

GDExtensionBool GDE_EXPORT tiles_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_tiles_module);
    init_obj.register_terminator(uninitialize_tiles_module);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
