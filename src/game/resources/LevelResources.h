#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>

namespace tiles::game {

// The authored level-resource graph.
//
// These are mutable Godot transport values, not domain values. A default or
// half-edited resource may describe nothing valid, and that is deliberate:
// authoring is incremental. No constructor asserts domain validity, no setter
// validates, compiles, quantizes, logs, saves, or loads, and no resource ever
// caches an exact core value. Everything exact is produced by the separate
// resource compiler, in one direction only.
//
// One level artifact is exactly the authoring witness — a schema version, a
// geometry domain, a palette, and the blueprint records which tile it. It
// stores no independently authored region: the only region a level has is the
// one derived by compiling its blueprint into an arrangement. Only canonical
// prototile ids, supplies, presentation colors, rational orientations, and raw
// q16.48 translations cross the Godot numeric boundary.

// The schema version this build writes and the only one it reads. A future
// incompatible schema increments it; there is deliberately no migration,
// accepted range, or best-effort interpretation of any other value.
constexpr std::int64_t LEVEL_RESOURCE_FORMAT_VERSION = 1;

// The stable serialized encoding of the two geometry domains. It is a transport
// spelling, not a domain value: the enumeration itself is still Godot-free and
// no resource holds one.
constexpr std::int64_t ENCODED_GEOMETRY_DOMAIN_LATTICE = 0;
constexpr std::int64_t ENCODED_GEOMETRY_DOMAIN_HEX12 = 1;

// One authored palette slot: which canonical prototile, how many, and the
// color this level presents it in.
//
// The id is a plain signed integer because Godot's serialized integer is
// signed; 0 is a valid encoding whose existence in the catalog is a separate
// lookup question, and negative values are invalid. Supply encodes -1 as
// unlimited and any positive value as a finite amount; 0 and values below -1
// are invalid.
//
// Color is presentation transport for the editor and player. It takes no part
// in identity, lookup, palette uniqueness, orientation compilation, legality,
// supply, containment, completion, or exact comparison, and it never enters the
// engine.
class PaletteEntryResource : public godot::Resource {
    GDCLASS(PaletteEntryResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_prototile_id(std::int64_t p_id);
    std::int64_t get_prototile_id() const;

    void set_supply(std::int64_t p_supply);
    std::int64_t get_supply() const;

    void set_color(const godot::Color &p_color);
    godot::Color get_color() const;

private:
    std::int64_t prototile_id_ = 0;
    std::int64_t supply_ = -1;
    godot::Color color_ = godot::Color(1.0f, 1.0f, 1.0f, 1.0f);
};

// One authored palette. Entry order is preserved exactly and becomes runtime
// palette order after successful compilation.
class PaletteResource : public godot::Resource {
    GDCLASS(PaletteResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_entries(const godot::TypedArray<PaletteEntryResource> &p_entries);
    godot::TypedArray<PaletteEntryResource> get_entries() const;

private:
    godot::TypedArray<PaletteEntryResource> entries_;
};

// One authored blueprint record: which canonical prototile, which of its
// distinct compiled orientations, and where.
//
// The orientation is a rational fraction of a turn written as a step/order
// pair, so a record keeps its meaning across recompilation and across any
// reordering of the values it names. The translation is authoritative signed
// q16.48 raw storage, transported as the two integers it already is: nothing
// here passes a coordinate through Vector2, decimal text, multiplication by
// Coordinate::SCALE, quantization, or rendered geometry.
//
// Every property is a plain signed integer because Godot's serialized integer
// is signed. Out-of-range and inconsistent encodings are representable on
// purpose; the compiler reports them with their exact record index.
class BlueprintPlacementResource : public godot::Resource {
    GDCLASS(BlueprintPlacementResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_prototile_id(std::int64_t p_id);
    std::int64_t get_prototile_id() const;

    void set_orientation_step(std::int64_t p_step);
    std::int64_t get_orientation_step() const;

    void set_orientation_order(std::int64_t p_order);
    std::int64_t get_orientation_order() const;

    void set_translation_x_raw(std::int64_t p_raw);
    std::int64_t get_translation_x_raw() const;

    void set_translation_y_raw(std::int64_t p_raw);
    std::int64_t get_translation_y_raw() const;

private:
    std::int64_t prototile_id_ = 0;
    std::int64_t orientation_step_ = 0;
    std::int64_t orientation_order_ = 1;
    std::int64_t translation_x_raw_ = 0;
    std::int64_t translation_y_raw_ = 0;
};

// One authored level: a schema version, a geometry domain, a palette, and a
// blueprint, and nothing else. It holds no region, arrangement, history,
// completion flag, player progress, export result, dirty flag, editor state,
// catalog reference, or display metadata. Godot's inherited resource path is
// its only file identity.
class LevelResource : public godot::Resource {
    GDCLASS(LevelResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_format_version(std::int64_t p_version);
    std::int64_t get_format_version() const;

    void set_geometry_domain(std::int64_t p_domain);
    std::int64_t get_geometry_domain() const;

    void set_palette(const godot::Ref<PaletteResource> &p_palette);
    godot::Ref<PaletteResource> get_palette() const;

    void set_blueprint(const godot::TypedArray<BlueprintPlacementResource> &p_blueprint);
    godot::TypedArray<BlueprintPlacementResource> get_blueprint() const;

private:
    std::int64_t format_version_ = LEVEL_RESOURCE_FORMAT_VERSION;
    std::int64_t geometry_domain_ = ENCODED_GEOMETRY_DOMAIN_LATTICE;
    godot::Ref<PaletteResource> palette_;
    godot::TypedArray<BlueprintPlacementResource> blueprint_;
};

} // namespace tiles::game
