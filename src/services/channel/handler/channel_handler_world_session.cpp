#include "channel_handler.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "core/util/string_utils.h"
#include "db/core/dqs_payloads.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "services/channel/runtime/channel_runtime.h"

namespace {
#pragma pack(push, 1)
	struct DemoCharState
	{
		std::uint64_t char_id = 0;
		std::uint32_t gold = 0;
		std::uint32_t version = 0;
	};
#pragma pack(pop)
	static_assert(sizeof(DemoCharState) == 16);

	static std::string SerializeDemo(const DemoCharState& s)
	{
		return std::string(reinterpret_cast<const char*>(&s), sizeof(s));
	}

	static bool TryDeserializeDemo(const std::string& blob, DemoCharState& out)
	{
		if (blob.size() != sizeof(DemoCharState)) return false;
		std::memcpy(&out, blob.data(), sizeof(DemoCharState));
		return true;
	}
} // namespace

bool ChannelHandler::HandleWorldOpenWorldNotice(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_open_world_notice>(body, body_len);
	if (!req) return false;

	std::cout << "[World] recv open_world_notice sid=" << n << "\n";

	svr::dqs_payload::OpenWorldNotice payload{};
	payload.sid = n;
	payload.serial = GetLatestSerial(n);
	copy_cstr(payload.world_name, req->szWorldName);

	const bool pushed = svr::g_Main.PushDQSData(
		(std::uint8_t)svr::dqs::ProcessCode::world,
		(std::uint8_t)svr::dqs::QueryCase::open_world_notice,
		reinterpret_cast<const char*>(&payload),
		(int)sizeof(payload));

	if (!pushed) {
		spdlog::warn("[World] DQS push failed sid={}", n);

		proto::S2C_open_world_success res{};
		res.ok = 0;
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::open_world_success,
			(std::uint16_t)sizeof(res));

		const std::uint32_t serial = GetLatestSerial(n);
		if (serial != 0) {
			Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
		}
	}
	return true;
}

bool ChannelHandler::HandleWorldAddGold(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_add_gold>(body, body_len);
	if (!req) return false;

	const std::uint32_t world_code = 0;
	const std::uint64_t char_id = svr::g_Main.FindCharIdBySession(n);
	if (char_id == 0) {
		spdlog::warn("[Demo] add_gold but no bound char. sid={}", n);
		return true;
	}

	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	a.combat.gold += req->add;
	const std::uint32_t combat_gold = a.combat.gold;

	DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;
	st.version = 1;

	if (auto blob = svr::g_Main.TryLoadCharacterState(world_code, char_id)) {
		DemoCharState loaded{};
		if (TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
			st = loaded;
	}

	st.gold += req->add;
	st.version += 1;
	svr::g_Main.CacheCharacterState(world_code, char_id, SerializeDemo(st));

	proto::S2C_add_gold_ok res{};
	res.ok = 1;
	res.gold = combat_gold;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::add_gold_ok,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(n);
	if (serial != 0) {
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}

bool ChannelHandler::HandleWorldGetStats(std::uint32_t dwProID, std::uint32_t n)
{
	const std::uint64_t char_id = GetActorIdBySession(n);
	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	auto cs = a.combat;

	proto::S2C_stats res{};
	res.char_id = char_id;
	res.hp = cs.hp;
	res.max_hp = cs.max_hp;
	res.atk = cs.atk;
	res.def = cs.def;
	res.gold = cs.gold;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(n);
	if (serial != 0) {
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}

bool ChannelHandler::HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_heal_self>(body, body_len);
	if (!req) return false;

	const std::uint64_t char_id = GetActorIdBySession(n);
	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	auto& st = a.combat;
	if (req->amount == 0) st.hp = st.max_hp;
	else {
		const std::uint64_t nhp = (std::uint64_t)st.hp + (std::uint64_t)req->amount;
		st.hp = (std::uint32_t)std::min<std::uint64_t>(st.max_hp, nhp);
	}
	auto cs = st;

	proto::S2C_stats res{};
	res.char_id = char_id;
	res.hp = cs.hp;
	res.max_hp = cs.max_hp;
	res.atk = cs.atk;
	res.def = cs.def;
	res.gold = cs.gold;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(n);
	if (serial != 0) {
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}
