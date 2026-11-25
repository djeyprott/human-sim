#pragma once
// Little-endian binary reading and writing, used by saves and history files.
// The reader checks every bound and throws FormatError on bad data, so a
// corrupt or foreign file is refused instead of loaded as garbage.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace hsim {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class BinaryWriter {
public:
    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    void put(T value) {
        // All supported targets (x64 Windows, and Linux for development) are
        // little-endian, so the in-memory representation is the file format.
        const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof(T));
    }

    void put_string(std::string_view s) {
        put<std::uint32_t>(static_cast<std::uint32_t>(s.size()));
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }

    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    void put_vector(const std::vector<T>& v) {
        put<std::uint64_t>(v.size());
        if (!v.empty()) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            bytes_.insert(bytes_.end(), p, p + v.size() * sizeof(T));
        }
    }

    void put_bytes(const std::vector<std::uint8_t>& b) {
        put<std::uint64_t>(b.size());
        bytes_.insert(bytes_.end(), b.begin(), b.end());
    }

    // Writes a tagged section: 4-char tag, then length, then the body.
    void put_section(const char (&tag)[5], const BinaryWriter& body) {
        bytes_.insert(bytes_.end(), tag, tag + 4);
        put_bytes(body.bytes());
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t>& bytes() { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
public:
    BinaryReader() = default;
    explicit BinaryReader(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    T get() {
        need(sizeof(T));
        T value;
        std::memcpy(&value, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    std::string get_string() {
        auto n = get<std::uint32_t>();
        need(n);
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), n);
        pos_ += n;
        return s;
    }

    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    std::vector<T> get_vector() {
        auto n = get<std::uint64_t>();
        if (n > (data_.size() - pos_) / sizeof(T)) throw FormatError("vector length out of range");
        std::vector<T> v(static_cast<std::size_t>(n));
        if (n) std::memcpy(v.data(), data_.data() + pos_, static_cast<std::size_t>(n) * sizeof(T));
        pos_ += static_cast<std::size_t>(n) * sizeof(T);
        return v;
    }

    std::vector<std::uint8_t> get_bytes() { return get_vector<std::uint8_t>(); }

    // Reads the next section; checks the tag matches.
    BinaryReader get_section(const char (&tag)[5]) {
        need(4);
        if (std::memcmp(data_.data() + pos_, tag, 4) != 0)
            throw FormatError(std::string("expected section '") + tag + "'");
        pos_ += 4;
        return BinaryReader(get_bytes());
    }

    bool at_end() const { return pos_ == data_.size(); }
    std::size_t remaining() const { return data_.size() - pos_; }

private:
    void need(std::size_t n) const {
        if (n > data_.size() - pos_) throw FormatError("unexpected end of data");
    }

    std::vector<std::uint8_t> data_;
    std::size_t pos_ = 0;
};

// Whole-file helpers. write_file writes to a temporary name and renames, so a
// crash mid-save never leaves a half-written file under the real name.
void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> read_file(const std::filesystem::path& path);

// 64-bit hash of a byte buffer (for determinism checks).
std::uint64_t hash_bytes(const std::vector<std::uint8_t>& bytes, std::uint64_t seed = 0);

}  // namespace hsim
