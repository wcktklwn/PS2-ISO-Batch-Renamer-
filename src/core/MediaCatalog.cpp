#include "core/MediaCatalog.h"

#include "embedded_media_catalog.h"

#include <cctype>

namespace ps2br {
namespace {

void trimLineEnd(std::string_view& line) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
    }
}

bool isCatalogId(std::string_view id) {
    if (id.size() != 11 || id[4] != '_' || id[8] != '.') {
        return false;
    }
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (index == 4 || index == 8) {
            continue;
        }
        const unsigned char value = static_cast<unsigned char>(id[index]);
        if (!(std::isupper(value) || std::isdigit(value))) {
            return false;
        }
    }
    return true;
}

} // namespace

MediaCatalog MediaCatalog::loadEmbedded() {
    MediaCatalog parsed;
    std::string_view text = embedded::media_catalog;
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t end = text.find('\n', cursor);
        std::string_view line = text.substr(cursor, (end == std::string_view::npos ? text.size() : end) - cursor);
        trimLineEnd(line);
        if (!line.empty() && line.front() != '#' && line.size() == 13 && line[11] == ' ' && isCatalogId(line.substr(0, 11))) {
            if (line[12] == 'C' || line[12] == 'D' || line[12] == 'P') {
                const MediaKind kind = line[12] == 'C' ? MediaKind::Cd : line[12] == 'D' ? MediaKind::Dvd : MediaKind::Playstation;
                parsed.entries_.emplace(std::string(line.substr(0, 11)), kind);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1;
    }
    return parsed;
}

const MediaCatalog& MediaCatalog::builtIn() {
    static const MediaCatalog catalog = loadEmbedded();
    return catalog;
}

const MediaKind* MediaCatalog::find(std::string_view game_id) const {
    const auto it = entries_.find(std::string(game_id));
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace ps2br
