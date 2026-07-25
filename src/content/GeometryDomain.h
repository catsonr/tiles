#pragma once

#include <cstdint>

namespace tiles::content {

// The geometry domain one authored level belongs to. It selects which exact
// source compiler produces a canonical prototile's oriented geometry, and which
// canonical identities are playable at all.
//
// A domain is a source-compilation and authoring concept. It disappears at the
// source-compilation boundary: no OrientedPrototile, Placement, Arrangement,
// PaletteEntry, Palette, Level, State, or engine command carries one, and every
// downstream consumer remains domain-blind. There is deliberately no string
// conversion, Godot registration, resource property, registry, bitmask, or
// dispatch interface here: this is a closed two-valued choice made before a
// palette entry is published, not an extension point.
//
// The underlying type is fixed so that casting any integer to this enumeration
// is well defined. A value outside the two enumerators is then an ordinary
// rejected input rather than an unspecified one, exactly as
// Hex12RegularPolygon already arranges.
enum class GeometryDomain : std::uint8_t {
    lattice,
    hex12,
};

} // namespace tiles::content
