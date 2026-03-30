#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace svr::zonecontent {

struct ZoneContentEntry {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    std::filesystem::path directory;
    std::filesystem::path map_wm_path;
    std::size_t portal_count = 0;
    std::size_t npc_count = 0;
    std::size_t monster_count = 0;
    std::size_t safe_count = 0;
    std::size_t special_count = 0;
};

std::filesystem::path DefaultResourcesRoot();
bool LoadCatalog(const std::filesystem::path& root = {});
std::vector<ZoneContentEntry> Snapshot();
std::optional<ZoneContentEntry> Find(std::uint32_t zone_id, std::uint32_t map_id);

} // namespace svr::zonecontent
