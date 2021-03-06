#pragma once

#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include <fmt/format.h>


std::string time_point_to_string(const std::chrono::system_clock::time_point &tp) {
    using namespace std::chrono;

    auto ttime_t = system_clock::to_time_t(tp);
    auto tp_sec = system_clock::from_time_t(ttime_t);
    milliseconds ms = duration_cast<milliseconds>(tp - tp_sec);

    const std::tm *ttm = std::localtime(&ttime_t);

    char date_time_format[] = "%Y-%m-%d %H:%M:%S";
    char time_str[] = "yyyy-mm-dd HH:MM:SS.fff";

    std::strftime(time_str, std::strlen(time_str), date_time_format, ttm);

    std::string result(time_str);
    result.append(".");
    result.append(std::to_string(ms.count()));

    return result;
}

template <typename... Args> void log(const char *fmt, Args &&... args) {
    std::string nstr = time_point_to_string(std::chrono::system_clock::now());
    std::clog << fmt::format("[{}]: {}\n", std::move(nstr), fmt::format(fmt, std::forward<Args>(args)...));
}





