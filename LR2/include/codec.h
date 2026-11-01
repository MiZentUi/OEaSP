#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace codec {

// Maximum record text length (RFC, section 3).
constexpr std::size_t kMaxText = 1 << 20; // 1 MiB

// Record size excluding text: 16 (uuid) + 4 (text_length) = 20 bytes.
constexpr std::size_t kRecordOverhead = 20;

using Bytes = std::vector<std::uint8_t>;

// A logical record: 16-byte UUID plus text. The format and its checks are an
// application decision; the kernel only sees bytes.
struct Record {
    std::array<std::uint8_t, 16> uuid{};
    std::string text;
};

// Numeric format fields are always little-endian.
void put_le(Bytes& out, std::uint64_t value, int width);
std::uint64_t get_le(const Bytes& bytes, std::size_t pos, int width);

// Serializes a record into bytes of length 20 + text.size() (uuid, LE length,
// text).
Bytes serialize(const Record& r);

// Parses the bytes of a single complete record; checks length and ASCII.
bool deserialize(const Bytes& bytes, Record& out, std::string& err);

// Text is considered ASCII if all bytes are < 0x80.
bool isAscii(const std::string& s);

// Line for displaying a record: "uuid  text".
std::string formatLine(const std::string& uuidStr, const std::string& text);

} // namespace codec