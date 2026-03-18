#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "server_common/runtime/line_client_start_helper.h"
#include "services/runtime/server_runtime_base.h"

#include "proto/internal/world_zone_proto.h"

namespace pt_wz = proto::internal::world_zone;

class ZoneWorldHandler;

namespace svr {

	class ZoneRuntime final : public dc::ServerRuntimeBase
	{
	private:
		struct MapInstanceState
		{
			std::uint32_t map_template_id = 0;
			std::uint32_t instance_id = 0;
			bool dungeon_instance = false;
			std::uint16_t active_player_count = 0;
			std::chrono::steady_clock::time_point created_at{};
			std::chrono::steady_clock::time_point last_access_at{};
		};

		struct PlayerBindingState
		{
			std::uint64_t map_key = 0;
			std::uint32_t map_template_id = 0;
			std::uint32_t instance_id = 0;
		};
	public:
		ZoneRuntime();
		~ZoneRuntime();

		std::uint16_t GetActiveMapInstanceCount() const noexcept;
		std::uint16_t GetMapInstanceCapacity() const noexcept;
		std::uint16_t GetLoadScore() const noexcept;
		std::uint16_t GetActivePlayerCount() const noexcept;

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

		bool NetworkInit_();
		void MarkWorldRegistered_(std::uint32_t sid, std::uint32_t serial);
		void MarkWorldDisconnected_(std::uint32_t sid, std::uint32_t serial);
		void SendWorldHeartbeat_();
		void OnMapAssignRequest(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::WorldZoneMapAssignRequest& req);
		void EnsureMapInstance_(
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			bool create_if_missing,
			bool dungeon_instance);
		void OnPlayerEnterRequest_(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::WorldZonePlayerEnter& req);
		void OnPlayerLeaveRequest_(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::WorldZonePlayerLeave& req);
		void ReapEmptyDungeonInstances_(std::chrono::steady_clock::time_point now);
		void RefreshMetrics_();
		static std::uint64_t MakeMapInstanceKey_(std::uint32_t map_template_id, std::uint32_t instance_id) noexcept;
		static bool IsDungeonMapTemplate_(std::uint32_t map_template_id) noexcept;


	private:
		std::uint32_t zone_server_id_ = 2001;
		std::uint16_t zone_id_ = 1;
		std::uint16_t world_id_ = 1;
		std::uint16_t channel_id_ = 1;
		std::string server_name_ = "zone-1";

		std::string world_host_ = "127.0.0.1";
		std::uint16_t world_port_ = 27788;

		std::uint16_t map_instance_capacity_ = 128;
		std::unordered_map<std::uint64_t, MapInstanceState> map_instances_{};
		std::unordered_map<std::uint64_t, PlayerBindingState> player_bindings_{};
		std::atomic<std::uint16_t> active_map_instance_count_{ 0 };
		std::atomic<std::uint16_t> active_player_count_{ 0 };
		std::atomic<std::uint16_t> load_score_{ 0 };
		std::uint32_t flags_ = 1;

		dc::OutboundLineEntry world_line_{};
		std::shared_ptr<ZoneWorldHandler> zone_world_handler_;
		std::atomic<bool> world_ready_{ false };
		std::atomic<std::uint32_t> world_sid_{ 0 };
		std::atomic<std::uint32_t> world_serial_{ 0 };
		std::chrono::steady_clock::time_point next_world_heartbeat_tp_{};
		std::chrono::steady_clock::time_point next_reap_tp_{};
	};

	extern ZoneRuntime g_ZoneMain;

} // namespace svr
