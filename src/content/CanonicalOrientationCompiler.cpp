#include "content/CanonicalOrientationCompiler.h"

#include "core/Orientation.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace tiles::content {

namespace {

using Compiled =
    Result<std::vector<OrientedPrototile>, CanonicalOrientationCompilationError>;

CanonicalOrientationCompilationError plain_error(
    CanonicalOrientationCompilationErrorCode p_code) {
    CanonicalOrientationCompilationError error {};
    error.code = p_code;
    return error;
}

// The lattice domain's fixed rotation policy: the four quarter turns. The core
// compiler collapses geometrically identical results, so a symmetric prototile
// still offers fewer than four distinct variants.
std::vector<Orientation> quarter_turns() {
    return {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };
}

// The hex-12 domain's fixed rotation policy: every twelfth turn. Orientation
// construction rejects only a zero order, so each of these is well formed and
// canonicalizes itself — step 3 is stored as 1/4, step 4 as 1/3, step 6 as 1/2.
std::vector<Orientation> twelfth_turns() {
    std::vector<Orientation> requested;
    requested.reserve(12);
    for (std::uint32_t k = 0; k < 12; ++k) {
        auto made = Orientation::make(k, 12);
        if (made) {
            requested.push_back(made.value());
        }
    }
    return requested;
}

bool is_known_domain(GeometryDomain p_domain) {
    switch (p_domain) {
        case GeometryDomain::lattice:
        case GeometryDomain::hex12:
            return true;
    }
    return false;
}

} // namespace

Result<std::vector<OrientedPrototile>, CanonicalOrientationCompilationError>
compile_canonical_orientations(
    GeometryDomain p_domain, const CanonicalPrototile &p_prototile) {
    // A value cast from an arbitrary integer names no domain. It is answered
    // before membership, without undefined behaviour, assertion, fallback, or
    // silently selecting another domain.
    if (!is_known_domain(p_domain)) {
        return Compiled::failure(plain_error(
            CanonicalOrientationCompilationErrorCode::unsupported_domain));
    }

    if (!p_prototile.supports(p_domain)) {
        return Compiled::failure(plain_error(
            CanonicalOrientationCompilationErrorCode::prototile_unavailable_in_domain));
    }

    if (p_domain == GeometryDomain::lattice) {
        auto compiled =
            compile_lattice_orientations(p_prototile.prototile(), quarter_turns());
        if (!compiled) {
            CanonicalOrientationCompilationError error = plain_error(
                CanonicalOrientationCompilationErrorCode::lattice_compilation_failed);
            error.lattice_error = compiled.error();
            return Compiled::failure(error);
        }
        return Compiled::success(std::move(compiled).value());
    }

    // Membership in hex-12 is exactly the presence of a hex-12 source polygon.
    auto compiled = compile_hex12_orientations(
        p_prototile.prototile().id(),
        p_prototile.hex12_polygon().value(),
        twelfth_turns());
    if (!compiled) {
        CanonicalOrientationCompilationError error = plain_error(
            CanonicalOrientationCompilationErrorCode::hex12_compilation_failed);
        error.hex12_error = compiled.error();
        return Compiled::failure(error);
    }
    return Compiled::success(std::move(compiled).value());
}

} // namespace tiles::content
