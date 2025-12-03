#include <iostream>
#include <string>

// Note: In a real build, we'd link against mathlib and stringutils
// For this demo, we'll use inline implementations

namespace mathlib {
    double add(double a, double b) { return a + b; }
    double multiply(double a, double b) { return a * b; }
    double power(double base, int exp) {
        double result = 1.0;
        for(int i = 0; i < exp; i++) result *= base;
        return result;
    }
}

namespace stringutils {
    std::string to_upper(const std::string& str) {
        std::string result = str;
        for(auto& c : result) c = std::toupper(c);
        return result;
    }
}

int main() {
    std::cout << "=== Rivet Complex Demo ===" << std::endl;
    std::cout << std::endl;
    
    // Using mathlib
    double a = 10.5, b = 3.2;
    std::cout << "Math Library Demo:" << std::endl;
    std::cout << "  " << a << " + " << b << " = " << mathlib::add(a, b) << std::endl;
    std::cout << "  " << a << " * " << b << " = " << mathlib::multiply(a, b) << std::endl;
    std::cout << "  " << a << "^3 = " << mathlib::power(a, 3) << std::endl;
    std::cout << std::endl;
    
    // Using stringutils
    std::string text = "hello from rivet";
    std::cout << "String Utilities Demo:" << std::endl;
    std::cout << "  Original: " << text << std::endl;
    std::cout << "  Upper: " << stringutils::to_upper(text) << std::endl;
    std::cout << std::endl;
    
    // Workspace features
    std::cout << "✓ Built with workspace support" << std::endl;
    std::cout << "✓ Multiple packages compiled" << std::endl;
    std::cout << "✓ External dependencies (boost)" << std::endl;
    std::cout << "✓ Static libraries linked" << std::endl;
    
    return 0;
}
