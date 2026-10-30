#include "png.hpp"

#include <zlib.h>

#include <fstream>

namespace railmaster::client {

namespace {

void put32(std::string& out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

// A chunk: its length, type, data and a CRC over the type and data.
void chunk(std::string& out, const char* type, const std::string& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const std::string body = std::string(type, 4) + data;
    out += body;
    put32(out, static_cast<std::uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
}

} // namespace

bool write_png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb) {
    if (width <= 0 || height <= 0 || rgb.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3) return false;
    // Each row starts with its filter type (0, none).
    std::string raw;
    const auto row = static_cast<std::size_t>(width) * 3;
    raw.reserve((row + 1) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back('\0');
        raw.append(reinterpret_cast<const char*>(rgb.data()) + static_cast<std::size_t>(y) * row, row);
    }
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::string packed(packed_size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(packed.data()), &packed_size, reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), 9) != Z_OK) {
        return false;
    }
    packed.resize(packed_size);
    std::string ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5); // 8-bit RGB, no interlace
    std::string out = "\x89PNG\r\n\x1a\n";
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", packed);
    chunk(out, "IEND", "");
    std::ofstream file(path, std::ios::binary);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

} // namespace railmaster::client
