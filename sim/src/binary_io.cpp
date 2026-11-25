#include "hsim/binary_io.hpp"

#include <fstream>

#include "hsim/rng.hpp"

namespace hsim {

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("write failed: " + tmp.string());
    }
    std::filesystem::rename(tmp, path);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    in.seekg(0, std::ios::end);
    auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in) throw std::runtime_error("read failed: " + path.string());
    return bytes;
}

std::uint64_t hash_bytes(const std::vector<std::uint8_t>& bytes, std::uint64_t seed) {
    std::uint64_t h = mix64(seed ^ bytes.size());
    std::size_t i = 0;
    for (; i + 8 <= bytes.size(); i += 8) {
        std::uint64_t w;
        std::memcpy(&w, bytes.data() + i, 8);
        h = hash_combine(h, w);
    }
    std::uint64_t tail = 0;
    for (std::size_t k = 0; i < bytes.size(); ++i, ++k) tail |= std::uint64_t(bytes[i]) << (8 * k);
    return hash_combine(h, tail);
}

}  // namespace hsim
