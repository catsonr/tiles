#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
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
// Level resources serialize canonical prototile ids, never prototile geometry,
// display names, or catalog references. Only per-level region vertices cross the
// Godot numeric boundary.

// One authored polygon boundary. The closing vertex is implicit: the first
// vertex is never repeated at the end. An invalid ring is diagnosed by the
// compiler, not by this setter.
//
// Used only for region boundaries. No catalog prototile refers to one.
class PolygonResource : public godot::Resource {
    GDCLASS(PolygonResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_vertices(const godot::PackedVector2Array &p_vertices);
    godot::PackedVector2Array get_vertices() const;

private:
    godot::PackedVector2Array vertices_;
};

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

// One authored region: an outer boundary and zero or more holes in authored
// order. A null outer boundary or a null hole is a representable transport
// value; the compiler reports it with its exact index.
class RegionResource : public godot::Resource {
    GDCLASS(RegionResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_outer_boundary(const godot::Ref<PolygonResource> &p_boundary);
    godot::Ref<PolygonResource> get_outer_boundary() const;

    void set_inner_boundaries(const godot::TypedArray<PolygonResource> &p_boundaries);
    godot::TypedArray<PolygonResource> get_inner_boundaries() const;

private:
    godot::Ref<PolygonResource> outer_boundary_;
    godot::TypedArray<PolygonResource> inner_boundaries_;
};

// One authored level: a palette and a region, and nothing else. It holds no
// arrangement, history, completion flag, known solution, catalog reference,
// rotation rule, display metadata, or save path. Godot's inherited resource
// path is its persistence identity.
class LevelResource : public godot::Resource {
    GDCLASS(LevelResource, godot::Resource)

protected:
    static void _bind_methods();

public:
    void set_palette(const godot::Ref<PaletteResource> &p_palette);
    godot::Ref<PaletteResource> get_palette() const;

    void set_region(const godot::Ref<RegionResource> &p_region);
    godot::Ref<RegionResource> get_region() const;

private:
    godot::Ref<PaletteResource> palette_;
    godot::Ref<RegionResource> region_;
};

} // namespace tiles::game
