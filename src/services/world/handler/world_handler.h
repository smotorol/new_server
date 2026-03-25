#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <cstring>
#include <string>
#include <spdlog/spdlog.h>

#include "server_common/handler/service_line_handler_base.h"
#include "services/world/runtime/i_world_runtime.h"

class WorldHandler : public dc::ServiceLineHandlerBase
{
public:
	explicit WorldHandler(svr::IWorldRuntime& runtime)
		: runtime_(runtime)
	{}
	~WorldHandler() override = default;

	// ✅ (sid -> char_id) 바인딩이 완료되면 char_id Actor로 라우팅
	std::uint64_t GetActorIdBySession(std::uint32_t sid) const;
	std::uint32_t GetLatestSerialForRuntime(std::uint32_t sid) const { return GetLatestSerial(sid); }
    std::string GetLatestRemoteEndpointForRuntime(std::uint32_t sid) const { return GetLatestRemoteEndpoint(sid); }
    void SendZoneMapState(
        std::uint32_t dwProID,
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t char_id,
        std::uint32_t zone_id,
        std::uint32_t map_id,
        std::int32_t x,
        std::int32_t y,
        proto::ZoneMapStateReason reason);
protected:
	bool DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
		_MSG_HEADER* pMsgHeader, char* pMsg) override;

	void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;
	void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;
	bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial) override;

	// ✅ ActorId 라우팅 키(기본 sid, 바인딩 후 char_id)
	std::uint64_t ResolveActorId(std::uint32_t session_idx) const override;

	// ✅ 패킷이 특정 actor(target_char_id)를 지정하는 경우 라우팅 변경
	std::uint64_t ResolveActorIdForPacket(std::uint32_t session_idx,
		const _MSG_HEADER& header, const char* body, std::size_t body_len,
		std::uint64_t default_actor) const override;

	svr::IWorldRuntime& runtime() noexcept { return runtime_; }
	const svr::IWorldRuntime& runtime() const noexcept { return runtime_; }

private:
	bool ResolveAuthenticatedCharIdOrReject_(
		const char* op_name,
		std::uint32_t sid,
		std::uint64_t& out_char_id) const;

	bool HandleEnterWorldWithToken(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);

	bool HandleWorldOpenWorldNotice(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldAddGold(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldGetStats(std::uint32_t dwProID, std::uint32_t n);
	bool HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);

	bool HandleWorldMove(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldBenchMove(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldBenchReset();
	bool HandleWorldBenchMeasure(const char* body, std::size_t body_len);

	bool HandleWorldSpawnMonster(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldAttackMonster(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldAttackPlayer(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);

	bool HandleWorldActorSeqTest(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);
	bool HandleWorldActorForward(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len);

private:
	svr::IWorldRuntime& runtime_;
};

