#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ps2br {

enum class MediaKind {
    Cd,
    Dvd,
    Playstation,
    // Redump lists this executable serial on both CD and DVD.
    Tied,
};

// Serials from the Redump PlayStation 2 CD, PlayStation 2 DVD, and PlayStation
// catalogs. The built-in table is generated from media_catalog.txt.
class MediaCatalog {
public:
    static const MediaCatalog& builtIn();

    const MediaKind* find(std::string_view game_id) const;
    std::size_t size() const { return entries_.size(); }

private:
    MediaCatalog() = default;
    static MediaCatalog loadEmbedded();

    std::unordered_map<std::string, MediaKind> entries_;
};

} // namespace ps2br
