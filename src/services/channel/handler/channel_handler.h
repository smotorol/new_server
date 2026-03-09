#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <mutex>
#include <spdlog/spdlog.h>

#include "app/runtime/networkex_base.h"

enum eLine
{
	eLine_World = 0,
	eLine_Login,
	eLine_Control,
	eLine_Count,
};

class ChannelHandler : public dc::NetworkEXBase
{
public:
	explicit ChannelHandler(std::uint32_t pro_id = 0) : dc::NetworkEXBase(pro_id) {}
	~ChannelHandler() override = default;

	// ✅ (sid -> char_id) 바인딩이 완료되면 char_id Actor로 라우팅
	std::uint64_t GetActorIdBySession(std::uint32_t sid) const;
protected:
	bool DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex,
		_MSG_HEADER* pMsgHeader, char* pMsg) override;
	void AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;
	void CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;
	// line별 분석(레거시 구조 유지용)
	bool LoginLineAnalysis(std::uint32_t dwProID, std::uint32_t n,
		_MSG_HEADER* pMsgHeader, char* pMsg);
	bool WorldLineAnalysis(std::uint32_t dwProID, std::uint32_t n,
		_MSG_HEADER* pMsgHeader, char* pMsg);
	bool ControlLineAnalysis(std::uint32_t dwProID, std::uint32_t n,
		_MSG_HEADER* pMsgHeader, char* pMsg);

	// ✅ ActorId 라우팅 키(기본 sid, 바인딩 후 char_id)
	std::uint64_t ResolveActorId(std::uint32_t session_idx) const override;

	// ✅ 패킷이 특정 actor(target_char_id)를 지정하는 경우 라우팅 변경
	std::uint64_t ResolveActorIdForPacket(std::uint32_t session_idx,
		const _MSG_HEADER& header, const char* body, std::size_t body_len,
		std::uint64_t default_actor) const override;

private:
	bool HandleWorldOpenWorldNotice(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldAddGold(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldGetStats(std::uint32_t dwProID, std::uint32_t sid);
	bool HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldBenchMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldBenchReset();
	bool HandleWorldBenchMeasure(const char* body, std::size_t body_len);
	bool HandleWorldSpawnMonster(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldAttackMonster(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldAttackPlayer(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldActorSeqTest(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);
	bool HandleWorldActorForward(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len);

	// ⚠️ ResolveActorId는 IO thread에서 호출될 수 있음(포스트 전에 결정)
	// -> 아래 상태는 mutex로 보호한다.
	mutable std::mutex state_mtx_;

	// ✅ 세션(index) -> char_id 바인딩
	std::unordered_map<std::uint32_t, std::uint64_t> session_char_ids_;

};
