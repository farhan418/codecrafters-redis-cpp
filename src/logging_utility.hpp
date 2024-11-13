#ifndef LOGGING_UTILITY_HPP
#define LOGGING_UTILITY_HPP

#include <iostream>
#include <chrono>
#include <ctime>

// #ifndef DEBUG_LOG
// #define DEBUG_LOG(msg)
// auto now = std::chrono::system_clock::now();\
// std::time_t now_time = std::chrono::system_clock::to_time_t(now);\
// std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
// #endif

#ifndef DEBUG_LOG
#define DEBUG_LOG(msg) \
std::cerr << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
#endif

// static void DEBUG_LOG(std::string msg) {
//   auto now = std::chrono::system_clock::now();
//   std::time_t now_time = std::chrono::system_clock::to_time_t(now);
//   std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
// }

#endif  // LOGGING_UTILITY_HPP