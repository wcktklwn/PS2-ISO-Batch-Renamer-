#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ps2br {

class ByteReader {
public:
    virtual ~ByteReader() = default;
    virtual bool read(std::uint64_t offset, std::span<std::byte> destination, std::string& error) = 0;

    // Uncompressed user-data size. Zero means the reader could not determine it.
    virtual std::uint64_t logicalBytes() const { return 0; }

    // True when the container publishes CD track metadata, as a CD CHD does.
    virtual bool hasCdLayout() const { return false; }
};

} // namespace ps2br
