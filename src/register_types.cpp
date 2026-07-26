#include "register_types.h"

#include "game/AudioDirector.h"
#include "game/Exam.h"
#include "game/LevelEditor.h"
#include "game/LevelPlayer.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelResources.h"
#include "game/testing/AudioIntegrationRunner.h"
#include "game/testing/ExamIntegrationRunner.h"
#include "game/testing/LevelEditorIntegrationRunner.h"
#include "game/testing/LevelPlayerIntegrationRunner.h"
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
    GDREGISTER_CLASS(tiles::game::PaletteEntryResource);
    GDREGISTER_CLASS(tiles::game::PaletteResource);
    GDREGISTER_CLASS(tiles::game::BlueprintPlacementResource);
    GDREGISTER_CLASS(tiles::game::LevelResource);

    // Registered only because their own dedicated test scenes instantiate them.
    // Registration does not place them in the main application scene.
    GDREGISTER_CLASS(tiles::game::ResourceIntegrationRunner);
    GDREGISTER_CLASS(tiles::game::LevelEditorIntegrationRunner);
    GDREGISTER_CLASS(tiles::game::LevelPlayerIntegrationRunner);
    GDREGISTER_CLASS(tiles::game::ExamIntegrationRunner);
    GDREGISTER_CLASS(tiles::game::AudioIntegrationRunner);

    // The row preview is constructed by LevelEditor and by the exam's palette,
    // so it must exist in ClassDB before the main scene is instantiated. The
    // exam instantiates the problem canvas scene, so the player must exist
    // before the exam does. The audio director is a child of the exam scene, so
    // it must exist before that scene is instantiated too.
    GDREGISTER_CLASS(tiles::game::PrototilePreview);
    GDREGISTER_CLASS(tiles::game::AudioDirector);
    GDREGISTER_CLASS(tiles::game::LevelEditor);
    GDREGISTER_CLASS(tiles::game::LevelPlayer);
    GDREGISTER_CLASS(tiles::game::Exam);
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
