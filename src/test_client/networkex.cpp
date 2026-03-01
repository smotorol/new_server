#include "networkex.h"
#include <iostream>
#include <cstring>
#include "../proto/proto.h"
#include "../common/string_utils.h"
#include "../proto/packet_util.h"
#include "../net/tcp_client.h"
#include "../net/tcp_session.h"
#include "define.h"
#include "GlobalClients.h"

void CNetworkEX::wait_ready()
{
	if (is_ready()) return;
	std::unique_lock lk(ready_mtx_);
	ready_cv_.wait(lk, [&] { return ready_.load(std::memory_order_relaxed); });
}

bool CNetworkEX::DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	switch (static_cast<eLine>(dwProID))
	{
	case eLine::sample_server:
		{
			return LineAnalysis(dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	}
	return false;
}

void CNetworkEX::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	// 연결되면 open_world_notice 전송
	proto::C2S_open_world_notice req{};

	std::cout << "[client] connected sid=" << dwIndex << "\n";
	copy_cstr(req.szWorldName, "world");

	auto h = proto::make_header((std::uint16_t)proto::C2SMsg::open_world_notice,
		+(std::uint16_t)sizeof(req));

	auto client = GlobalLineClients().client((std::uint8_t)eLine::sample_server);
	auto s = client->session();
	if (s) s->async_send(h, reinterpret_cast<const char*>(&req));

	spdlog::info("CNetworkEX::AcceptClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

void CNetworkEX::CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("CNetworkEX::CloseClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

bool CNetworkEX::LineAnalysis(std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	const std::uint16_t type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = pMsgHeader->m_wSize - MSG_HEADER_SIZE;

	switch (type)
	{
	case proto::S2CMsg::open_world_success:
		{
			auto* req = proto::as < proto::S2C_open_world_success>(pMsg, body_len);
			if (!req) return false;

			std::cout << "[World] recv open_world_success sid=" << n << "\n";
		}
		return true;
	case proto::S2CMsg::add_gold_ok:
		{
			auto* res = proto::as<proto::S2C_add_gold_ok>(pMsg, body_len);
			if (!res) return false;

			std::cout << "[World] recv add_gold_ok sid=" << n
				<< " ok=" << res->ok
				<< " gold=" << res->gold
				<< "\n";
		}
		return true;
	case proto::S2CMsg::actor_bound:
		{
			auto* res = proto::as<proto::S2C_actor_bound>(pMsg, body_len);
			if (!res) return false;
			actor_id_.store(res->actor_id, std::memory_order_relaxed);
			{
				std::lock_guard lk(ready_mtx_);
				ready_.store(true, std::memory_order_relaxed);
			}
			ready_cv_.notify_all();
			std::cout << "[World] actor_bound sid=" << n << " actor_id=" << res->actor_id << "\n";
		}
		return true;
	case proto::S2CMsg::actor_seq_ack:
		{
			auto* res = proto::as<proto::S2C_actor_seq_ack>(pMsg, body_len);
			if (!res) return false;
			std::cout << "[World] actor_seq_ack sid=" << n
				<< " ok=" << res->ok
				<< " seq/tag=" << res->seq
				<< " shard=" << res->shard
				<< " errors=" << res->errors
				<< "\n";
		}
		return true;
	default:
		std::cout << "[World] unknown type=" << type << "\n";
		return true;
	}
}
