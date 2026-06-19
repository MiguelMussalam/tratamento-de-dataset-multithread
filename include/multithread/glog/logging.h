#pragma once
#include <iostream>
#include <cstdlib>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Mock implementation of glog/logging.h to support derrickburns/tdigest
// without introducing external heavy dependencies.

#define CHECK_GT(a, b) if (!((a) > (b))) { std::cerr << "Check failed: " << #a << " > " << #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); }
#define CHECK_LE(a, b) if (!((a) <= (b))) { std::cerr << "Check failed: " << #a << " <= " << #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); }
#define CHECK_GE(a, b) if (!((a) >= (b))) { std::cerr << "Check failed: " << #a << " >= " << #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); }

class DummyStream {
public:
    template<typename T>
    DummyStream& operator<<(const T&) { return *this; }
    DummyStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
};

class LogStream {
public:
    LogStream(const char* severity) {
        // Suppress general logs in production/benchmark to avoid console pollution,
        // but compile them correctly. Feel free to print if needed.
    }
    template<typename T>
    LogStream& operator<<(const T&) {
        return *this;
    }
    LogStream& operator<<(std::ostream& (*)(std::ostream&)) {
        return *this;
    }
};

#define LOG_INFO LogStream("INFO")
#define LOG_WARNING LogStream("WARNING")
#define LOG_ERROR LogStream("ERROR")
#define LOG(severity) LOG_##severity

#define DLOG(severity) DummyStream()
