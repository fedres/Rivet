#include "mathlib/math.hpp"
#include <cmath>

namespace mathlib {
    double add(double a, double b) {
        return a + b;
    }
    
    double multiply(double a, double b) {
        return a * b;
    }
    
    double power(double base, int exp) {
        return std::pow(base, exp);
    }
}
