#include "codec.h"

#include <algorithm>
#include <cstdint>

namespace codec {

void put_le(Bytes& out, std::uint64_t value, int width) {
    for (int i = 0; i < width; ++i) {
        out.push_back(static_cast<std::uint8_t>(value & 0xffu));
        value >>= 8;
    }
}

std::uint64_t get_le(const Bytes& bytes, std::size_t pos, int width) {
    std::uint64_t value = 0;
    for (int i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(bytes[pos + static_cast<std::size_t>(i)])
                 << (8 * i);
    }
    return value;
}

Bytes serialize(const Record& r) {
    Bytes out;
    out.reserve(kRecordOverhead + r.text.size());
    for (std::uint8_t b : r.uuid) {
        out.push_back(b);
    }
    put_le(out, static_cast<std::uint64_t>(r.text.size()), 4);
    for (unsigned char c : r.text) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
    return out;
}

bool deserialize(const Bytes& bytes, Record& out, std::string& err) {
    if (bytes.size() < kRecordOverhead) {
        err = "record is shorter than the 20-byte header";
        return false;
    }
    const std::uint64_t len = get_le(bytes, 16, 4);
    if (len > kMaxText) {
        err = "text length exceeds the limit";
        return false;
    }
    if (bytes.size() != kRecordOverhead + len) {
        err = "record size does not match the length field";
        return false;
    }
    std::copy(bytes.begin(), bytes.begin() + 16, out.uuid.begin());
    out.text.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kRecordOverhead),
                    bytes.end());
    if (!isAscii(out.text)) {
        err = "text is not ASCII";
        return false;
    }
    return true;
}

bool isAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80u) {
            return false;
        }
    }
    return true;
}

std::string formatLine(const std::string& uuidStr, const std::string& text) {
    return uuidStr + "  " + text;
}

} // namespace codec