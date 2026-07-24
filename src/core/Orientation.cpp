#include "core/Orientation.h"

namespace tiles {

namespace {

Orientation::Component gcd(Orientation::Component p_a, Orientation::Component p_b) {
    while (p_b != 0) {
        const Orientation::Component t = p_a % p_b;
        p_a = p_b;
        p_b = t;
    }
    return p_a;
}

} // namespace

Result<Orientation, OrientationError> Orientation::make(
    Component p_step, Component p_order) {
    if (p_order == 0) {
        return Result<Orientation, OrientationError>::failure(
            OrientationError::zero_order);
    }

    // Reduce the step into [0, order); a whole turn becomes 0/1.
    const Component reduced_step = p_step % p_order;
    if (reduced_step == 0) {
        return Result<Orientation, OrientationError>::success(reference());
    }

    // Otherwise divide both components by their greatest common divisor, giving
    // the unique lowest-terms representative of the angle.
    const Component divisor = gcd(reduced_step, p_order);
    return Result<Orientation, OrientationError>::success(
        Orientation(reduced_step / divisor, p_order / divisor));
}

} // namespace tiles
