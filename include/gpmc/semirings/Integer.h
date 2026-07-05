#pragma once
#include <cassert>
#include <gmpxx.h>
#include <string>

#include <gpmc/Semiring.h>

namespace gpmc {

// Standard unweighted / integer-weighted algebra (arbitrary-precision integers).
// Also serves as a worked example for implementing the Semiring interface.
class Integer final : public Semiring {
    mpz_class value_;

public:
    explicit Integer(mpz_class v = 0) : value_(std::move(v)) {}

    SS add(const Semiring& rhs) const override {
        return std::make_unique<Integer>(value_ + cast(rhs).value_);
    }
    SS mul(const Semiring& rhs) const override {
        return std::make_unique<Integer>(value_ * cast(rhs).value_);
    }
    void add_inplace(const Semiring& rhs) override { value_ += cast(rhs).value_; }
    void mul_inplace(const Semiring& rhs) override { value_ *= cast(rhs).value_; }

    SS   dup()     const override { return std::make_unique<Integer>(value_); }
    SS   zero()    const override { return std::make_unique<Integer>(0); }
    SS   one()     const override { return std::make_unique<Integer>(1); }
    bool is_zero() const override { return value_ == 0; }
    bool is_one()  const override { return value_ == 1; }
    bool equals(const Semiring& rhs) const override { return value_ == cast(rhs).value_; }

    void shift_left(int k) { value_ <<= k; }

    size_t internal_size() const override {
        return mpz_size(value_.get_mpz_t()) * sizeof(mp_limb_t);
    }
    std::string to_string() const override { return value_.get_str(); }
    std::string notation() const override { return "int"; }

    SS parse(const std::string& w) const override {
        return std::make_unique<Integer>(mpz_class(w));
    }

    const mpz_class& value()  const { return value_; }

private:
    static const Integer& cast(const Semiring& s) {
        assert(dynamic_cast<const Integer*>(&s) && "Semiring type mismatch: expected Integer");
        return static_cast<const Integer&>(s);
    }
};

}
