#pragma once
#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace gpmc {

class Semiring;
using SS = std::unique_ptr<Semiring>;  // owning handle to a Semiring value

// Constrains the T in useSemiring<T>()-style template APIs to actual Semiring
// implementations, so passing the wrong type fails at the call site with a
// concept-not-satisfied error instead of a confusing pointer-conversion one.
template<typename T>
concept SemiringType = std::is_base_of_v<Semiring, T>;

// Weight algebra for model counting. Subclass to plug in a new weight type;
// rhs is always the same concrete type as *this.
class Semiring {
public:
    virtual ~Semiring() = default;

    virtual SS add(const Semiring& rhs) const = 0;  // *this + rhs, new value
    virtual SS mul(const Semiring& rhs) const = 0;  // *this * rhs, new value

    virtual void add_inplace(const Semiring& rhs) = 0;  // *this += rhs (hot path)
    virtual void mul_inplace(const Semiring& rhs) = 0;  // *this *= rhs (hot path)

    virtual SS dup() const = 0;  // deep copy

    virtual SS zero() const = 0;  // additive identity, same type as *this
    virtual SS one()  const = 0;  // multiplicative identity, same type as *this

    virtual bool is_zero() const = 0;
    virtual bool is_one()  const = 0;

    virtual bool equals(const Semiring& rhs) const = 0;

    virtual size_t internal_size() const = 0;  // bytes held, for cache accounting

    virtual std::string to_string() const = 0;  // human-readable value

    virtual std::string notation() const = 0;  // format tag, e.g. "int" / "frac"

    // Parse a per-literal weight string into a value of this same type. Only
    // meaningful for weighted algebras; the default rejects weighted input, so
    // an unweighted semiring need not implement it.
    virtual SS parse(const std::string&) const {
        throw std::runtime_error("this semiring does not support weighted input");
    }
};

}
