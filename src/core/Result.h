#pragma once

#include <cassert>
#include <cstddef>
#include <utility>
#include <variant>

namespace tiles {

// A minimal expected-like value: exactly one of a success value or an error.
// Success and failure are distinct alternatives, and a caller may inspect the
// error without consuming the result. Accessing the inactive alternative is a
// programmer error and asserts.
//
// Access uses std::get_if (noexcept) rather than std::get so the type carries
// no throwing path; the core is compiled with -fno-exceptions.
template <typename T, typename E>
class Result final {
public:
    static Result success(T p_value) {
        return Result(std::in_place_index<0>, std::move(p_value));
    }

    static Result failure(E p_error) {
        return Result(std::in_place_index<1>, std::move(p_error));
    }

    bool has_value() const {
        return storage_.index() == 0;
    }

    explicit operator bool() const {
        return has_value();
    }

    T &value() & {
        assert(has_value());
        return *std::get_if<0>(&storage_);
    }

    const T &value() const & {
        assert(has_value());
        return *std::get_if<0>(&storage_);
    }

    T &&value() && {
        assert(has_value());
        return std::move(*std::get_if<0>(&storage_));
    }

    const E &error() const & {
        assert(!has_value());
        return *std::get_if<1>(&storage_);
    }

private:
    template <std::size_t I, typename U>
    explicit Result(std::in_place_index_t<I> p_tag, U &&p_value) :
        storage_(p_tag, std::forward<U>(p_value)) {}

    std::variant<T, E> storage_;
};

} // namespace tiles
