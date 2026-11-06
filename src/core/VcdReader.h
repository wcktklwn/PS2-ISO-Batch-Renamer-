#pragma once

#include "core/ByteReader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace ps2br {

class VcdReader final : public ByteReader {
public:
    explicit VcdReader(const std::filesystem::path& path);

    bool isOpen() const;
    const std::string& openError() const;
    bool read(std::uint64_t offset, std::span<std::byte> destination, std::string& error) override;

private:
    bool readAt(std::uint64_t offset, std::span<std::byte> destination, std::string& error);
    bool hasVolumeDescriptor(std::uint32_t raw_sector_size, std::uint32_t user_offset);

    std::ifstream file_;
    std::uint64_t logical_bytes_ = 0;
    std::uint32_t raw_sector_size_ = 0;
    std::uint32_t user_offset_ = 0;
    std::string open_error_;
};

} // namespace ps2br
