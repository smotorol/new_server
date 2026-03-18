#include "services/zone/runtime/zone_runtime.h"

#include <spdlog/spdlog.h>

#include "services/zone/handler/zone_world_handler.h"

namespace svr {

	ZoneRuntime g_ZoneMain;

	ZoneRuntime::ZoneRuntime() = default;

	ZoneRuntime::~ZoneRuntime()
	{
		ReleaseMainThread();
	}

	std::uint16_t ZoneRuntime::GetActiveMapInstanceCount() const noexcept
	{
		return active_map_instance_count_.load(std::memory_order_relaxed);
	}

	std::uint16_t ZoneRuntime::GetMapInstanceCapacity() const noexcept
	{
		return map_instance_capacity_;
	}

	std::uint16_t ZoneRuntime::GetLoadScore() const noexcept
	{
		return load_score_.load(std::memory_order_relaxed);
	}

	std::uint16_t ZoneRuntime::GetActivePlayerCount() const noexcept
	{
		return active_player_count_.load(std::memory_order_relaxed);
 	}

	bool ZoneRuntime::OnRuntimeInit()
	{
		return NetworkInit_();
	}

	void ZoneRuntime::OnBeforeIoStop()
	{
	}

	void ZoneRuntime::OnAfterIoStop()
	{
		world_line_.host.Stop();
		zone_world_handler_.reset();
		world_ready_.store(false, std::memory_order_release);
		world_sid_.store(0, std::memory_order_relaxed);
		world_serial_.store(0, std::memory_order_relaxed);
	}

	void ZoneRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now)
	{
		if (world_ready_.load(std::memory_order_acquire) && now >= next_world_heartbeat_tp_) {
			next_world_heartbeat_tp_ = now + std::chrono::seconds(2);
			SendWorldHeartbeat_();
		}
		if (now >= next_reap_tp_) {
			next_reap_tp_ = now + std::chrono::seconds(10);
			ReapEmptyDungeonInstances_(now);
 		}
	}

	bool ZoneRuntime::NetworkInit_()
	{
		zone_world_handler_ = std::make_shared<ZoneWorldHandler>(
			zone_server_id_,
			zone_id_,
			world_id_,
			channel_id_,
			server_name_,
			[this](std::uint32_t sid, std::uint32_t serial) {
			MarkWorldRegistered_(sid, serial);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			MarkWorldDisconnected_(sid, serial);
		},
			[this]() { return GetMapInstanceCapacity(); },
			[this]() { return GetActiveMapInstanceCount(); },
			[this]() { return GetActivePlayerCount(); },
			[this]() { return GetLoadScore(); },
			[this]() { return flags_; },
			[this](std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZoneMapAssignRequest& req) {
				OnMapAssignRequest(sid, serial, req);
			},
			[this](std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerEnter& req) {
				OnPlayerEnterRequest_(sid, serial, req);
			},
			[this](std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerLeave& req) {
				OnPlayerLeaveRequest_(sid, serial, req);
 			});

		dc::InitOutboundLineEntry(world_line_, 201, "zone-world", world_host_, world_port_, true, 1000, 5000);
		if (!dc::StartOutboundLine(world_line_, io_, zone_world_handler_, [this](std::uint64_t, std::function<void()> fn) {
			boost::asio::post(io_, std::move(fn));
		})) {
			spdlog::error("ZoneRuntime failed to start outbound world line. remote={}:{}", world_host_, world_port_);
			return false;
		}

		spdlog::info("ZoneRuntime started. zone_id={} world_id={} channel_id={} remote_world={}:{}",
			zone_id_, world_id_, channel_id_, world_host_, world_port_);
		return true;
	}

	void ZoneRuntime::MarkWorldRegistered_(std::uint32_t sid, std::uint32_t serial)
	{
		world_sid_.store(sid, std::memory_order_relaxed);
		world_serial_.store(serial, std::memory_order_relaxed);
		world_ready_.store(true, std::memory_order_release);
		next_world_heartbeat_tp_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		next_reap_tp_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	}

	void ZoneRuntime::MarkWorldDisconnected_(std::uint32_t sid, std::uint32_t serial)
	{
		if (world_sid_.load(std::memory_order_relaxed) != sid ||
			world_serial_.load(std::memory_order_relaxed) != serial) {
			return;
		}

		world_ready_.store(false, std::memory_order_release);
		world_sid_.store(0, std::memory_order_relaxed);
		world_serial_.store(0, std::memory_order_relaxed);
	}

	void ZoneRuntime::SendWorldHeartbeat_()
	{
		if (!zone_world_handler_) {
			return;
		}

		const auto sid = world_sid_.load(std::memory_order_relaxed);
		const auto serial = world_serial_.load(std::memory_order_relaxed);
		if (sid == 0 || serial == 0) {
			return;
		}

		if (!zone_world_handler_->SendRouteHeartbeat(0, sid, serial)) {
			spdlog::debug("ZoneRuntime failed to send world heartbeat. sid={} serial={}", sid, serial);
		}
	}

	void ZoneRuntime::OnMapAssignRequest(
		std::uint32_t sid,
		std::uint32_t serial,
		const pt_wz::WorldZoneMapAssignRequest& req)
	{
		auto* handler = zone_world_handler_.get();
		if (!handler) {
			return;
		}
		EnsureMapInstance_(req.map_template_id, req.instance_id, req.create_if_missing != 0, req.dungeon_instance != 0);
		handler->SendMapAssignResponse(0, sid, serial, req.request_id, 0, zone_id_, req.map_template_id, req.instance_id);
	}
 	void ZoneRuntime::EnsureMapInstance_(
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
 		bool create_if_missing,
		bool dungeon_instance)
	{
		const auto key = MakeMapInstanceKey_(map_template_id, instance_id);
		auto now = std::chrono::steady_clock::now();
		auto it = map_instances_.find(key);
		if (it != map_instances_.end()) {
			it->second.last_access_at = now;
			return;
		}
		if (!create_if_missing) {
			return;
		}
		MapInstanceState st{};
		st.map_template_id = map_template_id;
		st.instance_id = instance_id;
		st.dungeon_instance = dungeon_instance || IsDungeonMapTemplate_(map_template_id);
		st.created_at = now;
		st.last_access_at = now;
		map_instances_[key] = st;
		RefreshMetrics_();
	}

	void ZoneRuntime::OnPlayerEnterRequest_(
		std::uint32_t /*sid*/,
		std::uint32_t /*serial*/,
		const pt_wz::WorldZonePlayerEnter& req)
	{
		EnsureMapInstance_(req.map_template_id, req.instance_id, true, IsDungeonMapTemplate_(req.map_template_id));
		const auto key = MakeMapInstanceKey_(req.map_template_id, req.instance_id);
		auto it = map_instances_.find(key);
		if (it == map_instances_.end()) {
			return;
		}

		auto bind_it = player_bindings_.find(req.char_id);
		if (bind_it != player_bindings_.end()) {
			if (bind_it->second.map_key == key) {
				it->second.last_access_at = std::chrono::steady_clock::now();
				return;
			}
			auto old_it = map_instances_.find(bind_it->second.map_key);
			if (old_it != map_instances_.end() && old_it->second.active_player_count > 0) {
				--old_it->second.active_player_count;
				old_it->second.last_access_at = std::chrono::steady_clock::now();
			}
		}

		++it->second.active_player_count;
		it->second.last_access_at = std::chrono::steady_clock::now();
		player_bindings_[req.char_id] = PlayerBindingState{ key, req.map_template_id, req.instance_id };
		RefreshMetrics_();
	}

	void ZoneRuntime::OnPlayerLeaveRequest_(
		std::uint32_t /*sid*/,
		std::uint32_t /*serial*/,
		const pt_wz::WorldZonePlayerLeave& req)
	{
		auto bind_it = player_bindings_.find(req.char_id);
		if (bind_it == player_bindings_.end()) {
			return;
		}
		const auto key = bind_it->second.map_key;
		auto it = map_instances_.find(key);
		if (it != map_instances_.end()) {
			if (it->second.active_player_count > 0) {
				--it->second.active_player_count;
			}
			it->second.last_access_at = std::chrono::steady_clock::now();
		}
		player_bindings_.erase(bind_it);
		RefreshMetrics_();
	}

	void ZoneRuntime::ReapEmptyDungeonInstances_(std::chrono::steady_clock::time_point now)
	{
		bool changed = false;
		for (auto it = map_instances_.begin(); it != map_instances_.end();) {
			const auto& inst = it->second;
			if (inst.dungeon_instance && inst.active_player_count == 0 && (now - inst.last_access_at) >= std::chrono::seconds(30)) {
				it = map_instances_.erase(it);
				changed = true;
				continue;
			}
			++it;
		}
		if (changed) {
			RefreshMetrics_();
		}
	}

	void ZoneRuntime::RefreshMetrics_()
	{
		std::uint32_t players = 0;
		for (const auto& [_, inst] : map_instances_) {
			players += inst.active_player_count;
		}
		active_map_instance_count_.store(static_cast<std::uint16_t>(map_instances_.size()), std::memory_order_relaxed);
		active_player_count_.store(static_cast<std::uint16_t>(players), std::memory_order_relaxed);
		load_score_.store(static_cast<std::uint16_t>(map_instances_.size() + players), std::memory_order_relaxed);
	}

	std::uint64_t ZoneRuntime::MakeMapInstanceKey_(std::uint32_t map_template_id, std::uint32_t instance_id) noexcept
	{
		return (static_cast<std::uint64_t>(map_template_id) << 32) | static_cast<std::uint64_t>(instance_id);
	}

	bool ZoneRuntime::IsDungeonMapTemplate_(std::uint32_t map_template_id) noexcept
	{
		return map_template_id >= 2000;
 	}
 
 } // namespace svr
