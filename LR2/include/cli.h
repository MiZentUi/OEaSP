#pragma once

#include <cstdint>
#include <string>

namespace cli {

enum class Command {
    Create,
    Add,
    List,
    Find,
    Generate,
    Help,
    None,
};

struct Parsed {
    Command cmd = Command::None;
    std::string file;
    std::string text;   // add
    std::string uuid;   // find
    std::uint64_t count = 0;   // generate
    std::uint64_t maxLen = 0;  // generate
    bool ok = true;      // false => argument error (exit code 2)
    std::string error;   // error text when !ok
};

Parsed parse(int argc, char** argv);
std::string usage();

} // namespace cli