#pragma once

#include "core/ByteReader.h"

#include <filesystem>
#include <fstream>

namespace ps2br {

class IsoReader final : public ByteReader {
public:
    explicit IsoReader(const std::filesystem::path& path);

    bool isOpen() const;
    const std::string& openError() const;
    bool read(std::uint64_t offset, std::span<std::byte> destination, std::string& error) override;
    std::uint64_t logicalBytes() const override { return logical_bytes_; }

private:
    std::ifstream file_;
    std::uint64_t logical_bytes_ = 0;
    std::string open_error_;
};

} // namespace ps2br
