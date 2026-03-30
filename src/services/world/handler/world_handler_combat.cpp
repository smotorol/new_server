#include "services/world/handler/world_handler.h"

#include <memory>

#include "services/world/runtime/world_runtime.h"

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/common/protobuf_packet_codec.h"
#include "server_common/session/session_key.h"
#include "services/world/actors/world_actors.h"

#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
#include "proto/generated/cpp/client_world.pb.h"
#define DC_WORLD_CLIENT_PROTOBUF 1
#else
#define DC_WORLD_CLIENT_PROTOBUF 0
#endif

namespace {
	void SendAttackResultPacket_(
		WorldHandler& handler,
		std::uint32_t dwProID,
		std::uint32_t sid,
		std::uint32_t serial,
		const proto::S2C_attack_result& res)
	{
#if DC_WORLD_CLIENT_PROTOBUF
		if (handler.IsSessionProtoMode(sid, serial)) {
			dc::proto::client::world::AttackResult msg;
			msg.set_attacker_id(res.attacker_id);
			msg.set_target_id(res.target_id);
			msg.set_damage(res.damage);
			msg.set_target_hp(res.target_hp);
			msg.set_killed(res.killed != 0);
			msg.set_drop_item_id(res.drop_item_id);
			msg.set_drop_count(res.drop_count);
			msg.set_attacker_gold(res.attacker_gold);
			std::vector<char> framed;
			if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(proto::S2CMsg::attack_result), msg, framed)) {
				_MSG_HEADER header{};
				std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
				handler.Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
				return;
			}
		}
#endif
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result, (std::uint16_t)sizeof(res));
		handler.Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
}

bool WorldHandler::HandleWorldAttackMonster(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len, bool use_protobuf)
{
	std::uint64_t monster_id = 0;
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::AttackMonsterRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		monster_id = req.monster_id();
	}
	else
#endif
	{
		auto* req = proto::as<proto::C2S_attack_monster>(body, body_len);
		if (!req) return false;
		monster_id = req->monster_id;
	}

	std::uint64_t attacker_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("attack_monster", sid, attacker_id)) {
		return true;
	}
	auto& attacker = runtime().GetOrCreatePlayerActor(attacker_id);
	const std::uint32_t attacker_atk = attacker.GetAttack();
	const std::uint32_t zone_id = attacker.GetZoneId();
	const std::uint32_t serial = GetLatestSerial(sid);
	if (!dc::IsValidSessionKey(sid, serial)) return true;
	SetSessionProtoMode(sid, serial, use_protobuf || IsSessionProtoMode(sid, serial));
	auto self = shared_from_this();

	runtime().PostActor(svr::MakeZoneActorId(zone_id), [this, self, dwProID, sid, serial, attacker_id, attacker_atk, zone_id, monster_id]() {
		auto& z = runtime().GetOrCreateZoneActor(zone_id);
		auto it_m = z.monsters.find(monster_id);
		if (it_m == z.monsters.end()) return;

		auto& mon = it_m->second;
		std::uint32_t dmg = (attacker_atk > mon.def) ? (attacker_atk - mon.def) : 1;
		bool killed = false;
		std::uint32_t mon_hp = 0;
		std::uint32_t drop_item = 0;
		std::uint32_t drop_cnt = 0;
		std::uint32_t reward_gold = 0;

		if (dmg >= mon.hp) {
			killed = true;
			mon_hp = 0;
			drop_item = mon.drop_item_id;
			drop_cnt = mon.drop_count;
			reward_gold = 50;
			z.monsters.erase(it_m);
		}
		else {
			mon.hp -= dmg;
			mon_hp = mon.hp;
		}

		if (!killed) {
			proto::S2C_attack_result res{};
			res.attacker_id = attacker_id;
			res.target_id = monster_id;
			res.damage = dmg;
			res.target_hp = mon_hp;
			res.killed = 0;
			SendAttackResultPacket_(static_cast<WorldHandler&>(*self), dwProID, sid, serial, res);
			return;
		}

		const std::uint64_t tx_id = z.next_tx_id++;
		runtime().PostActor(attacker_id, [this, self, dwProID, sid, serial, attacker_id, monster_id, dmg, mon_hp, drop_item, drop_cnt, reward_gold, tx_id]() {
			auto& a = runtime().GetOrCreatePlayerActor(attacker_id);
			bool ok = a.CanAddItem(drop_item, drop_cnt);
			if (ok) {
				a.CommitLoot(tx_id, drop_item, drop_cnt, reward_gold);
			}

			proto::S2C_attack_result res{};
			res.attacker_id = attacker_id;
			res.target_id = monster_id;
			res.damage = dmg;
			res.target_hp = mon_hp;
			res.killed = 1;
			res.drop_item_id = ok ? drop_item : 0;
			res.drop_count = ok ? drop_cnt : 0;
			res.attacker_gold = a.GetGold();

			SendAttackResultPacket_(static_cast<WorldHandler&>(*self), dwProID, sid, serial, res);
		});
	});
	return true;
}

bool WorldHandler::HandleWorldAttackPlayer(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len, bool use_protobuf)
{
	std::uint64_t target_char_id = 0;
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::AttackPlayerRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		target_char_id = req.target_char_id();
	}
	else
#endif
	{
		auto* req = proto::as<proto::C2S_attack_player>(body, body_len);
		if (!req) return false;
		target_char_id = req->target_char_id;
	}

	std::uint64_t attacker_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("attack_player", sid, attacker_id)) {
		return true;
	}

	const std::uint32_t a_serial = GetLatestSerial(sid);
	if (a_serial != 0) {
		SetSessionProtoMode(sid, a_serial, use_protobuf || IsSessionProtoMode(sid, a_serial));
	}

	auto& target = runtime().GetOrCreatePlayerActor(target_char_id);
	const std::uint32_t target_sid = target.sid;

	std::uint32_t dmg = 10;
	const std::uint32_t real_dmg = (dmg > target.GetDefense()) ? (dmg - target.GetDefense()) : 1;
	dmg = real_dmg;
	bool killed = false;
	const auto target_hp_before = target.GetCurrentHp();
	if (real_dmg >= target_hp_before) {
		target.SetCurrentHp(0);
		killed = true;
	}
	else {
		target.SetCurrentHp(target_hp_before - real_dmg);
	}
	const std::uint32_t target_hp = target.GetCurrentHp();

	proto::S2C_attack_result res{};
	res.attacker_id = attacker_id;
	res.target_id = target_char_id;
	res.damage = dmg;
	res.target_hp = target_hp;
	res.killed = killed ? 1u : 0u;

	if (a_serial != 0) {
		SendAttackResultPacket_(*this, dwProID, sid, a_serial, res);
 	}
	if (target_sid != 0) {
		const std::uint32_t t_serial = GetLatestSerial(target_sid);
		if (t_serial != 0) {
			SendAttackResultPacket_(*this, dwProID, target_sid, t_serial, res);
 		}
 	}

	return true;
}
