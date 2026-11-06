#pragma once

#include "core/ByteReader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ps2br {

class ZsoReader final : public ByteReader {
public:
    explicit ZsoReader(const std::filesystem::path& path);

    bool isOpen() const;
    const std::string& openError() const;
    bool read(std::uint64_t offset, std::span<std::byte> destination, std::string& error) override;
    std::uint64_t logicalBytes() const override { return total_bytes_; }

private:
    bool readAt(std::uint64_t offset, std::span<std::byte> destination, std::string& error);
    bool ensureBlock(std::uint64_t block_index, std::string& error);

    std::ifstream file_;
    std::uint64_t total_bytes_ = 0;
    std::uint32_t block_size_ = 0;
    std::uint8_t align_ = 0;
    std::uint64_t block_count_ = 0;
    std::vector<std::byte> block_;
    std::uint64_t cached_block_ = 0;
    bool block_cached_ = false;
    std::string open_error_;
};

} // namespace ps2br
