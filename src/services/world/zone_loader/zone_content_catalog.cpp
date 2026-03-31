#include "services/world/zone_loader/zone_content_catalog.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace svr::zonecontent {
namespace {

struct CatalogState {
    std::mutex mtx;
    std::filesystem::path root;
    std::vector<ZoneContentEntry> entries;
};

CatalogState& GetState() noexcept
{
    static CatalogState state;
    return state;
}

std::vector<std::string> SplitCsvLine_(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (ch == ',' && !in_quotes) {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    fields.push_back(current);
    return fields;
}

std::size_t CountCsvRows_(const std::filesystem::path& path)
{
    std::ifstream is(path);
    if (!is) {
        return 0;
    }
    std::string line;
    std::size_t count = 0;
    bool header_skipped = false;
    while (std::getline(is, line)) {
        if (!header_skipped) {
            header_skipped = true;
            continue;
        }
        if (!line.empty()) {
            ++count;
        }
    }
    return count;
}

bool ReadZoneMapFromMapsCsv_(const std::filesystem::path& path, std::uint32_t& out_zone_id, std::uint32_t& out_map_id)
{
    std::ifstream is(path);
    if (!is) {
        return false;
    }
    std::string line;
    if (!std::getline(is, line)) {
        return false;
    }
    if (!std::getline(is, line)) {
        return false;
    }
    const auto fields = SplitCsvLine_(line);
    if (fields.size() < 2) {
        return false;
    }
    out_zone_id = static_cast<std::uint32_t>(std::stoul(fields[0]));
    out_map_id = static_cast<std::uint32_t>(std::stoul(fields[1]));
    return true;
}

bool ReadZoneMapFromZoneJson_(const std::filesystem::path& path, std::uint32_t& out_zone_id, std::uint32_t& out_map_id)
{
    std::ifstream is(path, std::ios::in | std::ios::binary);
    if (!is) {
        return false;
    }

    nlohmann::json root;
    is >> root;
    out_zone_id = root.value("zone_id", 0u);
    out_map_id = root.value("map_id", 0u);
    return out_zone_id != 0 && out_map_id != 0;
}

} // namespace

std::filesystem::path DefaultResourcesRoot()
{
    const auto cwd = std::filesystem::current_path();
    const auto zones_root = cwd / "resources" / "zones";
    if (std::filesystem::exists(zones_root)) {
        return zones_root;
    }
    return cwd / "resources" / "map";
}

bool LoadCatalog(const std::filesystem::path& root)
{
    const auto resolved_root = root.empty() ? DefaultResourcesRoot() : root;
    std::vector<ZoneContentEntry> entries;

    std::error_code ec;
    if (!std::filesystem::exists(resolved_root, ec)) {
        auto& state = GetState();
        std::lock_guard lk(state.mtx);
        state.root = resolved_root;
        state.entries.clear();
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(resolved_root, ec)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto dir = entry.path();
        std::uint32_t zone_id = 0;
        std::uint32_t map_id = 0;
        if (!ReadZoneMapFromMapsCsv_(dir / "maps.csv", zone_id, map_id)) {
            if (!ReadZoneMapFromZoneJson_(dir / "zone.json", zone_id, map_id)) {
                continue;
            }
        }

        ZoneContentEntry content{};
        content.zone_id = zone_id;
        content.map_id = map_id;
        content.directory = dir;
        if (std::filesystem::exists(dir / "map.wm")) {
            content.map_wm_path = dir / "map.wm";
        }
        else {
            content.map_wm_path = std::filesystem::current_path() / "resources" / "maps" / fmt::format("map_{:03d}", map_id) / "base.wm";
        }
        content.portal_count = CountCsvRows_(dir / "portal.csv");
        content.npc_count = std::max(CountCsvRows_(dir / "npc.csv"), CountCsvRows_(dir / "npc_spawn.csv"));
        content.monster_count = std::max(CountCsvRows_(dir / "monster.csv"), CountCsvRows_(dir / "monster_spawn.csv"));
        content.safe_count = std::max(CountCsvRows_(dir / "safe.csv"), CountCsvRows_(dir / "safe_zone.csv"));
        content.special_count = std::max(CountCsvRows_(dir / "special.csv"), CountCsvRows_(dir / "special_region.csv"));
        entries.push_back(std::move(content));
    }

    std::sort(entries.begin(), entries.end(), [](const ZoneContentEntry& lhs, const ZoneContentEntry& rhs) {
        if (lhs.zone_id != rhs.zone_id) {
            return lhs.zone_id < rhs.zone_id;
        }
        return lhs.map_id < rhs.map_id;
    });

    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    state.root = resolved_root;
    state.entries = std::move(entries);
    return !state.entries.empty();
}

std::vector<ZoneContentEntry> Snapshot()
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    return state.entries;
}

std::optional<ZoneContentEntry> Find(std::uint32_t zone_id, std::uint32_t map_id)
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    auto it = std::find_if(state.entries.begin(), state.entries.end(), [zone_id, map_id](const ZoneContentEntry& entry) {
        return entry.zone_id == zone_id && entry.map_id == map_id;
    });
    if (it == state.entries.end()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace svr::zonecontent
