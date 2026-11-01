#pragma once

#include <cstdint>
#include <ctime>
#include <ostream>
#include <string>

namespace file_io {

// Command result: ok=false means a runtime or format error (exit code 1);
// message holds the error text or the result to print.
struct OpResult {
    bool ok = true;
    std::string message;
};

// Monotonic timer (clock_gettime(CLOCK_MONOTONIC)).
struct Timer {
    Timer();
    double elapsedMs() const;
    struct timespec start_;
};

OpResult create(const std::string& path);
OpResult add(const std::string& path, const std::string& text);
OpResult list(const std::string& path, std::ostream& out);
OpResult find(const std::string& path, const std::string& uuidStr);
OpResult generate(const std::string& path, std::uint64_t count, std::uint64_t maxLen);

std::string makeUuid();

} // namespace file_io