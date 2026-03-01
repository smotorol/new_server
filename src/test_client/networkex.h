#pragma once

#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "../app/networkex_base.h"

class CNetworkEX : public dc::NetworkEXBase
{
public:
	explicit CNetworkEX(std::uint32_t pro_id = 0) : dc::NetworkEXBase(pro_id) {}
	~CNetworkEX() override = default;

	// ✅ 서버가 알려준 ActorId(char_id)
	std::uint64_t actor_id() const noexcept { return actor_id_.load(std::memory_order_relaxed); }
	bool is_ready() const noexcept { return ready_.load(std::memory_order_relaxed); }
	void wait_ready();

private:
	std::atomic<std::uint64_t> actor_id_{ 0 };
	std::atomic<bool> ready_{ false };
	mutable std::mutex ready_mtx_;
	std::condition_variable ready_cv_;
protected:
	// ====== 아래는 기존 LogServer의 확장 지점(포팅 대상 함수들) ======
	bool DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg) override;
	void AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
	void CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;

	// line별 분석(레거시 구조 유지용)
	bool LineAnalysis(std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg);
};
