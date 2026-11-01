#include "file_io.h"

#include "codec.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>

namespace file_io {

namespace {

// Format (RFC, section 3): 12-byte header, record of 20 + N bytes.
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;       // 4 (version) + 8 (record_count)
constexpr std::size_t kRecordHeader = 20;     // 16 (uuid) + 4 (text_length)

constexpr int kDefaultMode = 0644; // permissions of a new file, subject to umask

// RAII wrapper for a file descriptor.
struct Fd {
    int fd = -1;
    explicit Fd(int f) : fd(f) {}
    ~Fd() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : fd(o.fd) { o.fd = -1; }
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) {
            if (fd >= 0) ::close(fd);
            fd = o.fd;
            o.fd = -1;
        }
        return *this;
    }
};

OpResult errMsg(std::string m) { return OpResult{false, std::move(m)}; }
OpResult okMsg(std::string m) { return OpResult{true, std::move(m)}; }

std::string describe(const std::string& op, int err) {
    std::string s = op;
    if (err > 0) {
        s += ": " + std::to_string(err) + " (" + std::strerror(err) + ")";
    }
    return s;
}

OpResult errnoMsg(const std::string& op, const std::string& path, int err) {
    return errMsg(describe(op + " '" + path + "'", err));
}

// ---------- low-level wrappers ----------

// read_exact: read exactly len bytes, retrying partial reads and handling
// EINTR. On premature EOF returns false and leaves errno == 0.
bool readExact(int fd, void* buf, std::size_t len) {
    auto* p = static_cast<char*>(buf);
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::read(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) { // premature EOF
            errno = 0;
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// write_all: write exactly len bytes; zero progress is an error.
bool writeAll(int fd, const void* buf, std::size_t len) {
    auto* p = static_cast<const char*>(buf);
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool getSize(int fd, off_t& out) {
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        return false;
    }
    out = st.st_size;
    return true;
}

// Open a file and ensure it is a regular file, not a directory.
int openRegular(const std::string& path, int flags) {
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        return -1;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int e = errno;
        ::close(fd);
        errno = e;
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        errno = EISDIR;
        return -1;
    }
    return fd;
}

// Advisory write lock over the whole file (fcntl F_SETLKW, F_WRLCK).
bool lockWrite(int fd) {
    struct flock fl {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0; // whole file
    for (;;) {
        if (::fcntl(fd, F_SETLKW, &fl) == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}

// ---------- format ----------

void writeHeader(int fd, std::uint32_t version, std::uint64_t count) {
    codec::Bytes b;
    codec::put_le(b, version, 4);
    codec::put_le(b, count, 8);
    writeAll(fd, b.data(), b.size());
}

bool readHeader(int fd, std::uint32_t& version, std::uint64_t& count) {
    std::array<std::uint8_t, kHeaderSize> raw{};
    if (!readExact(fd, raw.data(), raw.size())) {
        return false;
    }
    codec::Bytes b(raw.begin(), raw.end());
    version = static_cast<std::uint32_t>(codec::get_le(b, 0, 4));
    count = codec::get_le(b, 4, 8);
    return true;
}

// Traverse all records from the current position (after the header). The
// callback is invoked for each valid record and must return true (a full
// traversal always validates the file to the end, even on an early match).
bool traverse(int fd, std::uint64_t count,
              const std::function<bool(const codec::Record&)>& fn,
              std::string& err) {
    off_t fsize = 0;
    if (!getSize(fd, fsize)) {
        err = "failed to get file size";
        return false;
    }
    std::array<std::uint8_t, kRecordHeader> raw{};
    for (std::uint64_t i = 0; i < count; ++i) {
        if (!readExact(fd, raw.data(), raw.size())) {
            if (errno == 0) {
                err = "truncated record: missing record header";
            } else {
                err = "error reading record header";
            }
            return false;
        }
        codec::Bytes hb(raw.begin(), raw.end());
        const std::uint64_t tlen = codec::get_le(hb, 16, 4);
        if (tlen > codec::kMaxText) {
            err = "text length exceeds the limit";
            return false;
        }
        const off_t pos = ::lseek(fd, 0, SEEK_CUR);
        if (pos < 0) {
            err = "positioning error";
            return false;
        }
        if (pos + static_cast<off_t>(tlen) > fsize) {
            err = "truncated record: missing text";
            return false;
        }

        codec::Record r;
        std::copy(raw.begin(), raw.begin() + 16, r.uuid.begin());
        r.text.resize(tlen);
        if (tlen > 0 && !readExact(fd, r.text.data(), tlen)) {
            err = "error reading record text";
            return false;
        }
        if (!codec::isAscii(r.text)) {
            err = "record text is not ASCII";
            return false;
        }
        if (!fn(r)) {
            return false;
        }
    }
    const off_t end = ::lseek(fd, 0, SEEK_CUR);
    if (end < 0) {
        err = "positioning error";
        return false;
    }
    if (end != fsize) {
        err = "trailing data after records";
        return false;
    }
    return true;
}

// Validate the whole database (used by add before inserting).
bool validateBase(int fd, std::uint64_t count, std::string& err) {
    return traverse(fd, count, [](const codec::Record&) { return true; }, err);
}

// ---------- UUID (libuuid) ----------

// The libuuid uuid_t already holds the bytes of the canonical representation,
// so they can be stored in the file as-is and restored via uuid_unparse.
std::string uuidString(const std::array<std::uint8_t, 16>& bytes) {
    uuid_t u{};
    std::copy(bytes.begin(), bytes.end(), u);
    char buf[37]{};
    uuid_unparse(u, buf);
    return std::string(buf);
}

bool parseUuid(const std::string& s, std::array<std::uint8_t, 16>& out) {
    uuid_t u{};
    if (uuid_parse(s.c_str(), u) != 0) {
        return false;
    }
    std::copy(std::begin(u), std::end(u), out.begin());
    return true;
}

// Roll back add after a failure following the record append: restore count and
// size.
OpResult rollbackAdd(int fd, off_t oldSize, std::uint64_t oldCount) {
    bool ok = true;
    if (::lseek(fd, 0, SEEK_SET) >= 0) {
        writeHeader(fd, kVersion, oldCount);
    }
    if (::ftruncate(fd, oldSize) != 0) {
        ok = false;
    }
    ::fsync(fd);
    return errMsg("write failed; rollback " + std::string(ok ? "performed" : "failed"));
}

} // namespace

Timer::Timer() {
    ::clock_gettime(CLOCK_MONOTONIC, &start_);
}

double Timer::elapsedMs() const {
    struct timespec end {};
    ::clock_gettime(CLOCK_MONOTONIC, &end);
    const double sec = static_cast<double>(end.tv_sec - start_.tv_sec) +
                       static_cast<double>(end.tv_nsec - start_.tv_nsec) / 1e9;
    return sec * 1000.0;
}

std::string makeUuid() {
    uuid_t u{};
    uuid_generate(u);
    char buf[37]{};
    uuid_unparse(u, buf);
    return std::string(buf);
}

OpResult create(const std::string& path) {
    int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, kDefaultMode);
    if (fd < 0) {
        return errnoMsg("create", path, errno);
    }
    {
        Fd g(fd);
        writeHeader(fd, kVersion, 0);
        if (::fsync(fd) != 0) {
            return errnoMsg("fsync", path, errno);
        }
    }

    // Re-read the created header and compare it.
    const int rf = ::open(path.c_str(), O_RDONLY);
    if (rf < 0) {
        return errnoMsg("reopen", path, errno);
    }
    Fd rg(rf);
    std::uint32_t version = 0;
    std::uint64_t count = 0;
    if (!readHeader(rf, version, count)) {
        return errnoMsg("re-read header", path, errno);
    }
    if (version != kVersion || count != 0) {
        return errMsg("created header does not match the expected one");
    }
    return okMsg("");
}

OpResult add(const std::string& path, const std::string& text) {
    if (!codec::isAscii(text)) {
        return errMsg("text is not ASCII");
    }
    if (text.size() > codec::kMaxText) {
        return errMsg("text exceeds the 1 MiB limit");
    }

    codec::Record rec;
    if (!parseUuid(makeUuid(), rec.uuid)) {
        return errMsg("UUID generation failed");
    }
    rec.text = text;
    const codec::Bytes recBytes = codec::serialize(rec);

    const int fd = openRegular(path, O_RDWR);
    if (fd < 0) {
        return errnoMsg("open", path, errno);
    }
    Fd g(fd);
    if (!lockWrite(fd)) {
        return errnoMsg("lock", path, errno);
    }

    std::uint32_t version = 0;
    std::uint64_t count = 0;
    if (::lseek(fd, 0, SEEK_SET) < 0 || !readHeader(fd, version, count)) {
        return errnoMsg("read header", path, errno);
    }
    if (version != kVersion) {
        return errMsg("invalid database version");
    }
    std::string terr;
    if (!validateBase(fd, count, terr)) {
        return errMsg("database is corrupted: " + terr);
    }

    off_t oldSize = 0;
    if (!getSize(fd, oldSize)) {
        return errnoMsg("size", path, errno);
    }

    if (::lseek(fd, 0, SEEK_END) < 0) {
        return errnoMsg("seek", path, errno);
    }
    if (!writeAll(fd, recBytes.data(), recBytes.size())) {
        return rollbackAdd(fd, oldSize, count);
    }
    if (::fsync(fd) != 0) {
        return rollbackAdd(fd, oldSize, count);
    }

    const std::uint64_t newCount = count + 1;
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        return rollbackAdd(fd, oldSize, count);
    }
    writeHeader(fd, kVersion, newCount);
    if (::fsync(fd) != 0) {
        return rollbackAdd(fd, oldSize, count);
    }

    // Re-read the count and the record, compare with the expected values.
    std::uint32_t v2 = 0;
    std::uint64_t c2 = 0;
    if (::lseek(fd, 0, SEEK_SET) < 0 || !readHeader(fd, v2, c2)) {
        return errnoMsg("re-read header", path, errno);
    }
    if (::lseek(fd, oldSize, SEEK_SET) < 0) {
        return errnoMsg("re-read seek", path, errno);
    }
    codec::Bytes check(recBytes.size());
    if (!readExact(fd, check.data(), check.size())) {
        return errnoMsg("re-read record", path, errno);
    }
    if (v2 != kVersion || c2 != newCount || check != recBytes) {
        return errMsg("verification after write failed");
    }
    return okMsg(uuidString(rec.uuid));
}

OpResult list(const std::string& path, std::ostream& out) {
    const int fd = openRegular(path, O_RDONLY);
    if (fd < 0) {
        return errnoMsg("open", path, errno);
    }
    Fd g(fd);
    std::uint32_t version = 0;
    std::uint64_t count = 0;
    if (!readHeader(fd, version, count)) {
        return errnoMsg("read header", path, errno);
    }
    if (version != kVersion) {
        return errMsg("invalid database version");
    }
    std::string terr;
    const bool ok = traverse(
        fd, count,
        [&](const codec::Record& r) {
            out << codec::formatLine(uuidString(r.uuid), r.text) << "\n";
            return true;
        },
        terr);
    if (!ok) {
        return errMsg("database is corrupted: " + terr);
    }
    return okMsg("");
}

OpResult find(const std::string& path, const std::string& uuidStr) {
    codec::Record target;
    if (!parseUuid(uuidStr, target.uuid)) {
        return errMsg("invalid UUID: " + uuidStr);
    }
    const int fd = openRegular(path, O_RDONLY);
    if (fd < 0) {
        return errnoMsg("open", path, errno);
    }
    Fd g(fd);
    std::uint32_t version = 0;
    std::uint64_t count = 0;
    if (!readHeader(fd, version, count)) {
        return errnoMsg("read header", path, errno);
    }
    if (version != kVersion) {
        return errMsg("invalid database version");
    }
    bool matched = false;
    codec::Record found;
    std::string terr;
    const bool ok = traverse(
        fd, count,
        [&](const codec::Record& r) {
            if (!matched && r.uuid == target.uuid) {
                matched = true;
                found = r;
            }
            return true;
        },
        terr);
    if (!ok) {
        return errMsg("database is corrupted: " + terr);
    }
    if (!matched) {
        return okMsg("not found");
    }
    return okMsg(codec::formatLine(uuidString(found.uuid), found.text));
}

OpResult generate(const std::string& path, std::uint64_t count, std::uint64_t maxLen) {
    if (count == 0) {
        return errMsg("count must be positive");
    }
    if (maxLen < 1 || maxLen > codec::kMaxText) {
        return errMsg("maxLen is out of range 1..1048576");
    }
    // Overflow check: 12 + count * (20 + maxLen).
    const std::uint64_t rec = kRecordHeader + maxLen;
    if (count > (std::numeric_limits<std::uint64_t>::max() - kHeaderSize) / rec) {
        return errMsg("database size exceeds the limit");
    }

    // Read-write is required: every record is re-read and compared.
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, kDefaultMode);
    if (fd < 0) {
        return errnoMsg("create", path, errno);
    }
    Fd g(fd);
    writeHeader(fd, kVersion, 0);

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::size_t> lenDist(1, static_cast<std::size_t>(maxLen));
    std::uniform_int_distribution<int> charDist('a', 'z');

    std::string firstUuid, middleUuid, lastUuid;
    const std::uint64_t middle = count / 2;

    for (std::uint64_t i = 0; i < count; ++i) {
        codec::Record r;
        const std::string u = makeUuid();
        if (!parseUuid(u, r.uuid)) {
            return errMsg("UUID generation failed");
        }
        r.text.resize(lenDist(rng));
        for (char& ch : r.text) {
            ch = static_cast<char>(charDist(rng));
        }
        if (i == 0) {
            firstUuid = u;
        }
        if (i == middle) {
            middleUuid = u;
        }
        lastUuid = u;

        const codec::Bytes b = codec::serialize(r);
        if (!writeAll(fd, b.data(), b.size())) {
            return errnoMsg("write", path, errno);
        }
        // Re-read this record and compare byte by byte.
        if (::lseek(fd, -static_cast<off_t>(b.size()), SEEK_CUR) < 0) {
            return errnoMsg("seek", path, errno);
        }
        codec::Bytes check(b.size());
        if (!readExact(fd, check.data(), check.size())) {
            return errnoMsg("re-read", path, errno);
        }
        if (check != b) {
            return errMsg("record did not match after re-reading");
        }
    }

    // Save the count, flush, then validate the structure and size.
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        return errnoMsg("seek", path, errno);
    }
    writeHeader(fd, kVersion, count);
    if (::fsync(fd) != 0) {
        return errnoMsg("fsync", path, errno);
    }

    std::uint32_t v2 = 0;
    std::uint64_t c2 = 0;
    if (::lseek(fd, 0, SEEK_SET) < 0 || !readHeader(fd, v2, c2)) {
        return errnoMsg("re-read header", path, errno);
    }
    if (v2 != kVersion || c2 != count) {
        return errMsg("header did not match after generate");
    }
    std::string terr;
    if (!validateBase(fd, c2, terr)) {
        return errMsg("generated database is corrupted: " + terr);
    }
    off_t sz = 0;
    if (!getSize(fd, sz)) {
        return errnoMsg("size", path, errno);
    }

    return okMsg("count=" + std::to_string(count) + " size=" + std::to_string(sz) +
                 " first=" + firstUuid + " middle=" + middleUuid + " last=" + lastUuid);
}

} // namespace file_io