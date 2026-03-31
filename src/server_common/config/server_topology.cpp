#include "server_common/config/server_topology.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "server_common/config/runtime_config_path.h"

namespace dc::cfg {
namespace {

std::vector<std::uint32_t> ParseMaps_(const nlohmann::json& node)
{
    std::vector<std::uint32_t> maps;
    if (!node.is_array()) {
        return maps;
    }
    for (const auto& item : node) {
        maps.push_back(item.get<std::uint32_t>());
    }
    std::sort(maps.begin(), maps.end());
    maps.erase(std::unique(maps.begin(), maps.end()), maps.end());
    return maps;
}

std::vector<std::uint16_t> ParseChannels_(const nlohmann::json& node)
{
    std::vector<std::uint16_t> channels;
    if (!node.is_array()) {
        return channels;
    }
    for (const auto& item : node) {
        channels.push_back(item.get<std::uint16_t>());
    }
    std::sort(channels.begin(), channels.end());
    channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
    return channels;
}

} // namespace

const ZoneProcessTopologyEntry* ServerTopology::FindZoneProcess(std::uint32_t zone_server_id) const noexcept
{
    auto it = zone_process_index_by_server_id.find(zone_server_id);
    if (it == zone_process_index_by_server_id.end()) {
        return nullptr;
    }
    if (it->second >= zone_processes.size()) {
        return nullptr;
    }
    return &zone_processes[it->second];
}

std::vector<const ZoneProcessTopologyEntry*> ServerTopology::FindZoneProcessesForMap(std::uint32_t map_id) const
{
    std::vector<const ZoneProcessTopologyEntry*> result;
    for (const auto& entry : zone_processes) {
        if (std::find(entry.maps.begin(), entry.maps.end(), map_id) != entry.maps.end()) {
            result.push_back(&entry);
        }
    }
    return result;
}

std::filesystem::path DefaultServersConfigPath()
{
    return ResolveRuntimeConfigPath(
        "DC_SERVERS_CONFIG_PATH",
        {
            std::filesystem::path("config") / "servers.json",
            std::filesystem::path("Initialize") / "servers.json",
            std::filesystem::path("servers.json"),
        });
}

std::optional<ServerTopology> LoadServerTopology(const std::filesystem::path& path)
{
    const auto resolved = path.empty() ? DefaultServersConfigPath() : path;
    std::ifstream is(resolved, std::ios::in | std::ios::binary);
    if (!is) {
        return std::nullopt;
    }

    nlohmann::json root;
    is >> root;

    ServerTopology topology{};
    topology.source_path = resolved;

    const auto& zone_servers = root.contains("zone_servers") ? root["zone_servers"] : nlohmann::json::array();
    for (const auto& shard : zone_servers) {
        const auto logical_id = shard.value("id", 0);
        const auto world_id = static_cast<std::uint16_t>(shard.value("world_id", 1));
        const auto listen_port = static_cast<std::uint16_t>(shard.value("port", 0));
        const auto maps = ParseMaps_(shard.value("maps", nlohmann::json::array()));
        auto channels = ParseChannels_(shard.value("channels", nlohmann::json::array()));
        if (channels.empty()) {
            channels.push_back(1);
        }

        for (const auto channel_id : channels) {
            ZoneProcessTopologyEntry entry{};
            entry.zone_server_id = static_cast<std::uint32_t>(logical_id * 10 + channel_id);
            entry.logical_zone_id = static_cast<std::uint16_t>(logical_id);
            entry.channel_id = channel_id;
            entry.world_id = world_id;
            entry.listen_port = static_cast<std::uint16_t>(listen_port == 0 ? 0 : (listen_port + (channel_id - 1)));
            entry.server_name = shard.value("server_name", std::string{}) + (shard.value("server_name", std::string{}).empty() ? "" : "-") + "ch" + std::to_string(channel_id);
            if (entry.server_name.empty()) {
                entry.server_name = "zone-" + std::to_string(logical_id) + "-ch" + std::to_string(channel_id);
            }
            entry.maps = maps;
            topology.zone_process_index_by_server_id[entry.zone_server_id] = topology.zone_processes.size();
            topology.zone_processes.push_back(std::move(entry));
        }
    }

    return topology;
}

} // namespace dc::cfg
