#include "channel_handler.h"

#include <memory>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "services/channel/runtime/channel_runtime.h"
#include "services/channel/actors/channel_actors.h"

bool ChannelHandler::HandleWorldAttackMonster(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_attack_monster>(body, body_len);
	if (!req) return false;

	const std::uint64_t attacker_id = GetActorIdBySession(sid);
	auto& attacker = svr::g_Main.GetOrCreatePlayerActor(attacker_id);
	const std::uint32_t attacker_atk = attacker.combat.atk;
	const std::uint32_t zone_id = attacker.zone_id;
	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial == 0) return true;
	const std::uint64_t monster_id = req->monster_id;
	auto self = shared_from_this();

	svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [self, dwProID, sid, serial, attacker_id, attacker_atk, zone_id, monster_id]() {
		auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
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
			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result, (std::uint16_t)sizeof(res));
			self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
			return;
		}

		const std::uint64_t tx_id = z.next_tx_id++;
		svr::g_Main.PostActor(attacker_id, [self, dwProID, sid, serial, attacker_id, monster_id, dmg, mon_hp, drop_item, drop_cnt, reward_gold, tx_id]() {
			auto& a = svr::g_Main.GetOrCreatePlayerActor(attacker_id);
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
			res.attacker_gold = a.combat.gold;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result, (std::uint16_t)sizeof(res));
			self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
		});
	});
	return true;
}

bool ChannelHandler::HandleWorldAttackPlayer(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_attack_player>(body, body_len);
	if (!req) return false;

	const std::uint64_t attacker_id = svr::g_Main.FindCharIdBySession(sid);
	if (attacker_id == 0) return true;

	auto& target = svr::g_Main.GetOrCreatePlayerActor(req->target_char_id);
	const std::uint32_t target_sid = target.sid;

	std::uint32_t dmg = 10;
	const std::uint32_t real_dmg = (dmg > target.combat.def) ? (dmg - target.combat.def) : 1;
	dmg = real_dmg;
	bool killed = false;
	if (real_dmg >= target.combat.hp) {
		target.combat.hp = 0;
		killed = true;
	}
	else {
		target.combat.hp -= real_dmg;
	}
	const std::uint32_t target_hp = target.combat.hp;

	proto::S2C_attack_result res{};
	res.attacker_id = attacker_id;
	res.target_id = req->target_char_id;
	res.damage = dmg;
	res.target_hp = target_hp;
	res.killed = killed ? 1u : 0u;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result,
		(std::uint16_t)sizeof(res));

	const std::uint32_t a_serial = GetLatestSerial(sid);
	if (a_serial != 0) {
		Send(dwProID, sid, a_serial, h, reinterpret_cast<const char*>(&res));
	}
	if (target_sid != 0) {
		const std::uint32_t t_serial = GetLatestSerial(target_sid);
		if (t_serial != 0) {
			Send(dwProID, target_sid, t_serial, h, reinterpret_cast<const char*>(&res));
		}
	}

	return true;
}
