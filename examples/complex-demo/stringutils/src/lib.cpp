#include "stringutils/strings.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace stringutils {
    std::string to_upper(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }
    
    std::string to_lower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    std::string format_number(double num) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << num;
        return oss.str();
    }
}
