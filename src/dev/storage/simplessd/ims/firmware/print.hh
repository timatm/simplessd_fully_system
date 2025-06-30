#ifndef __PRINT_HH__
#define __PRINT_HH__

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <type_traits>

#define DEBUG

namespace logger {

enum class Level { Info, Debug };

// 基本輸出格式
inline void print(Level lvl, const std::string& msg) {
    switch (lvl) {
        case Level::Info:  std::cout << "[INFO]  "; break;
        case Level::Debug: std::cout << "[DEBUG] "; break;
    }
    std::cout << msg << std::endl;
}

// 改進版 fmt_str：若沒有 args，使用 safer fallback
inline std::string fmt_str(const char* fmt) {
    return std::string(fmt);  // 不格式化，直接當字串輸出
}

template<typename... Args>
inline typename std::enable_if<(sizeof...(Args) > 0), std::string>::type
fmt_str(const char* fmt, Args... args) {
    constexpr size_t size = 1024;
    char buffer[size];
    std::snprintf(buffer, size, fmt, args...);  // 安全限制長度
    return std::string(buffer);
}

// 封裝成 info/debug 格式化介面
template<typename... Args>
inline void infof(const char* fmt, Args... args) {
    print(Level::Info, fmt_str(fmt, args...));
}

template<typename... Args>
inline void debugf(const char* fmt, Args... args) {
    print(Level::Debug, fmt_str(fmt, args...));
}

} // namespace logger

// ────────────── 使用者介面 ──────────────
#define pr_info(...)  logger::infof(__VA_ARGS__)

#ifdef DEBUG
  #define pr_debug(...) logger::debugf(__VA_ARGS__)
#else
  #define pr_debug(...) ((void)0)
#endif

#endif // __PRINT_HH__
