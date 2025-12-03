#pragma once
#include <string>

namespace stringutils {
    std::string to_upper(const std::string& str);
    std::string to_lower(const std::string& str);
    std::string format_number(double num);
}
