#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dc::cfg {

struct ZoneProcessTopologyEntry
{
    std::uint32_t zone_server_id = 0;
    std::uint16_t logical_zone_id = 0;
    std::uint16_t channel_id = 0;
    std::uint16_t world_id = 1;
    std::uint16_t listen_port = 0;
    std::string server_name;
    std::vector<std::uint32_t> maps;
};

struct ServerTopology
{
    std::filesystem::path source_path;
    std::vector<ZoneProcessTopologyEntry> zone_processes;
    std::unordered_map<std::uint32_t, std::size_t> zone_process_index_by_server_id;

    const ZoneProcessTopologyEntry* FindZoneProcess(std::uint32_t zone_server_id) const noexcept;
    std::vector<const ZoneProcessTopologyEntry*> FindZoneProcessesForMap(std::uint32_t map_id) const;
};

std::filesystem::path DefaultServersConfigPath();
std::optional<ServerTopology> LoadServerTopology(const std::filesystem::path& path = {});

} // namespace dc::cfg
