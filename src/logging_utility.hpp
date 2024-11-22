#ifndef LOGGING_UTILITY_HPP
#define LOGGING_UTILITY_HPP

#include <iostream>
#include <chrono>
#include <ctime>
#include <regex>


// #ifndef DEBUG_LOG
// #define DEBUG_LOG(msg)
// auto now = std::chrono::system_clock::now();\
// std::time_t now_time = std::chrono::system_clock::to_time_t(now);\
// std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
// #endif

#ifndef DEBUG_LOG
#define DEBUG_LOG(msg) \
std::cerr << "\n[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "] [" << __FILE__ << ":" << __LINE__ << "] " << __func__ << "() : " << (msg) << std::endl;
#endif

namespace utility {

    std::vector<std::string> split(const std::string& str, const std::string& delimeter = "\r\n") {
        std::vector<std::string> result;
        std::regex re(delimeter);
        std::sregex_token_iterator first(str.begin(), str.end(), re, -1);
        std::sregex_token_iterator last;
        result.assign(first, last);
        return result;
    }

    bool compareCaseInsensitive(const std::string& str1, const std::string& str2) {
        std::string str1_lower = str1;
        std::string str2_lower = str2;

        std::transform(str1_lower.begin(), str1_lower.end(), str1_lower.begin(), ::tolower);
        std::transform(str2_lower.begin(), str2_lower.end(), str2_lower.begin(), ::tolower);

        return str1_lower.compare(str2_lower) == 0;
    }
};

// static void DEBUG_LOG(std::string msg) {
//   auto now = std::chrono::system_clock::now();
//   std::time_t now_time = std::chrono::system_clock::to_time_t(now);
//   std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
// }

#endif  // LOGGING_UTILITY_HPP