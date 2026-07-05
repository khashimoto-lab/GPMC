#pragma once
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <gmpxx.h>
#include <stdexcept>
#include <string>

#include <gpmc/Semiring.h>

namespace gpmc {

// Parse a decimal/fraction/scientific weight string into an exact rational.
inline mpq_class parse_rational_weight(const std::string& s) {
    if (s.find('/') != std::string::npos) {
        mpq_class r(s); r.canonicalize(); return r;
    }

    bool negative = (!s.empty() && s[0] == '-');
    std::string t = negative ? s.substr(1) : s;
    if (t.empty())
        throw std::invalid_argument("invalid weight string: '" + s + "'");

    long exp = 0;
    auto e_pos = t.find_first_of("eE");
    if (e_pos != std::string::npos) {
        exp = std::stol(t.substr(e_pos + 1));
        t   = t.substr(0, e_pos);
    }

    auto dot = t.find('.');
    std::string int_part, frac_part;
    if (dot == std::string::npos) {
        int_part  = t.empty() ? "1" : t;
        frac_part = "";
    } else {
        int_part  = t.substr(0, dot);
        frac_part = t.substr(dot + 1);
        if (int_part.empty())  int_part  = "0";
    }

    std::string digits = int_part + frac_part;
    mpz_class num(digits, 10);
    long frac_len = static_cast<long>(frac_part.size());

    long net_exp = exp - frac_len;
    mpq_class r;
    if (net_exp >= 0) {
        mpz_class scale;
        mpz_ui_pow_ui(scale.get_mpz_t(), 10, static_cast<unsigned long>(net_exp));
        r = mpq_class(num * scale, mpz_class(1));
    } else {
        mpz_class den;
        mpz_ui_pow_ui(den.get_mpz_t(), 10, static_cast<unsigned long>(-net_exp));
        r = mpq_class(num, den);
    }
    r.canonicalize();
    if (negative) r = -r;
    return r;
}

// Standard rational-weighted algebra (arbitrary-precision fractions). A second
// worked example of the Semiring interface, alongside Integer.
class Rational final : public Semiring {
    mpq_class value_;

public:
    explicit Rational(mpq_class v = 0) : value_(std::move(v)) { value_.canonicalize(); }

    SS add(const Semiring& rhs) const override {
        return std::make_unique<Rational>(value_ + cast(rhs).value_);
    }
    SS mul(const Semiring& rhs) const override {
        return std::make_unique<Rational>(value_ * cast(rhs).value_);
    }
    void add_inplace(const Semiring& rhs) override { value_ += cast(rhs).value_; }
    void mul_inplace(const Semiring& rhs) override { value_ *= cast(rhs).value_; }

    SS   dup()     const override { return std::make_unique<Rational>(value_); }
    SS   zero()    const override { return std::make_unique<Rational>(mpq_class(0)); }
    SS   one()     const override { return std::make_unique<Rational>(mpq_class(1)); }
    bool is_zero() const override { return value_ == 0; }
    bool is_one()  const override { return value_ == 1; }
    bool equals(const Semiring& rhs) const override { return value_ == cast(rhs).value_; }

    size_t internal_size() const override {
        return (mpz_size(value_.get_num().get_mpz_t()) +
                mpz_size(value_.get_den().get_mpz_t())) * sizeof(mp_limb_t);
    }

    std::string to_string_frac() const {
        return value_.get_num().get_str() + "/" + value_.get_den().get_str();
    }

    // Exact decimal if the denominator's only prime factors are 2 and 5 and it
    // fits in `digits` significant figures; otherwise scientific notation
    // rounded to `digits` significant figures.
    std::string to_string_prec_sci(int digits = 16) const {
        if (value_ == 0) return "0";

        bool negative = (value_ < 0);
        mpq_class abs_val = negative ? -value_ : value_;

        mpz_class den = abs_val.get_den();
        mpz_class tmp = den;
        int a = 0, b = 0;
        while (mpz_divisible_ui_p(tmp.get_mpz_t(), 2)) { tmp /= 2; a++; }
        while (mpz_divisible_ui_p(tmp.get_mpz_t(), 5)) { tmp /= 5; b++; }
        if (tmp == 1) {

            int k = std::max(a, b);
            mpz_class scale;
            mpz_ui_pow_ui(scale.get_mpz_t(), 10, static_cast<unsigned long>(k));
            mpz_class int_val = abs_val.get_num() * scale / den;

            std::string s = int_val.get_str();
            while ((int)s.size() <= k) s = "0" + s;
            std::string exact = s;
            exact.insert(exact.size() - k, ".");
            exact.erase(exact.find_last_not_of('0') + 1);
            if (exact.back() == '.') exact.pop_back();
            int sig = static_cast<int>(int_val.get_str().size());
            if (sig <= digits)
                return negative ? "-" + exact : exact;

        }

        int bits = static_cast<int>(digits * 3.32193) + 64;
        mpf_class f(abs_val, bits);
        char buf[512];
        gmp_snprintf(buf, sizeof(buf), "%.*Fe", digits - 1, f.get_mpf_t());
        return negative ? "-" + std::string(buf) : std::string(buf);
    }

    std::string to_string() const override { return to_string_frac(); }
    std::string notation() const override { return "frac"; }

    SS parse(const std::string& w) const override {
        return std::make_unique<Rational>(parse_rational_weight(w));
    }

    const mpq_class& value() const { return value_; }

private:
    static const Rational& cast(const Semiring& s) {
        assert(dynamic_cast<const Rational*>(&s) && "Semiring type mismatch: expected Rational");
        return static_cast<const Rational&>(s);
    }
};

}
