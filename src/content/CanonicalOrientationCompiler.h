#pragma once

#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/Hex12.h"
#include "core/OrientedPrototile.h"
#include "core/Result.h"

#include <optional>
#include <vector>

namespace tiles::content {

enum class CanonicalOrientationCompilationErrorCode {
    unsupported_domain,
    prototile_unavailable_in_domain,
    lattice_compilation_failed,
    hex12_compilation_failed,
};

// Exactly one nested compiler error is populated, matching the code:
// lattice_compilation_failed populates lattice_error, hex12_compilation_failed
// populates hex12_error, and the two domain-membership failures populate
// neither. The source compiler's own error is preserved complete.
struct CanonicalOrientationCompilationError final {
    CanonicalOrientationCompilationErrorCode code;
    std::optional<LatticeOrientationError> lattice_error;
    std::optional<Hex12CompilationError> hex12_error;
};

// Compile one canonical identity's admitted orientations in the named geometry
// domain.
//
// This is the whole of the domain decision. The requested orientation policy is
// fixed by the domain, not by a caller:
//
//     lattice    the four quarter turns 0/1, 1/4, 1/2, 3/4
//     hex12      all twelve twelfth turns k/12
//
// The selected source compiler is called exactly once and its complete ordered
// grouped product is returned unchanged: nothing here re-sorts, re-groups,
// rotates, deduplicates, inspects a coordinate, or caches a result. For hex-12
// both the identity and the regular polygon come from the canonical entry, so no
// caller can submit an arbitrary id and claim another canonical source.
//
// Domain disappears here. Every value returned is an ordinary OrientedPrototile
// which no downstream consumer can trace back to a domain.
Result<std::vector<OrientedPrototile>, CanonicalOrientationCompilationError>
compile_canonical_orientations(
    GeometryDomain p_domain, const CanonicalPrototile &p_prototile);

} // namespace tiles::content
