#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <memory>
#include <deque>
#include <cstring>
#include <chrono>

#include "../net/actor_system.h"
#include "../proto/proto.h"
#include "../proto/packet_util.h"

namespace svr {

	// ---- ActorId tagging ----
	//  - PlayerActor : actor_id = char_id (assume char_id < 2^63)
	//  - WorldActor  : actor_id = 0
	//  - ZoneActor   : actor_id = (1<<63) | zone_id
	constexpr std::uint64_t kZoneTag = 1ull << 63;
	inline constexpr std::uint64_t MakeZoneActorId(std::uint32_t zone_id) noexcept { return kZoneTag | (std::uint64_t)zone_id; }
	inline constexpr bool IsZoneActorId(std::uint64_t actor_id) noexcept { return (actor_id & kZoneTag) != 0; }
	inline constexpr std::uint32_t ZoneIdFromActorId(std::uint64_t actor_id) noexcept { return (std::uint32_t)(actor_id & ~kZoneTag); }

	// ---- 기본 전투 상태(샘플) ----
	struct CharCombatState {
		std::uint32_t hp = 100;
		std::uint32_t max_hp = 100;
		std::uint32_t atk = 20;
		std::uint32_t def = 3;
		std::uint32_t gold = 1000;
	};

	// ---- 샘플 몬스터 ----
	struct MonsterState {
		std::uint64_t id = 0;
		std::uint32_t hp = 50;
		std::uint32_t atk = 8;
		std::uint32_t def = 1;
		std::uint32_t drop_item_id = 1001;
		std::uint32_t drop_count = 1;
	};

	struct Vec2i {
		std::int32_t x = 0;
		std::int32_t y = 0;
	};

	// ✅ WorldActor(ActorId=0): 월드 공유 상태(몬스터 등)
	class WorldActor final : public net::IActor {
	public:
		// 필요 시: global chat / world event 등
	};

	// ✅ PlayerActor(ActorId=char_id): 캐릭터 상태를 독점
	class PlayerActor final : public net::IActor {
	public:
		explicit PlayerActor(std::uint64_t char_id_) : char_id(char_id_) {}

		void bind_session(std::uint32_t sid_, std::uint32_t serial_) {
			sid = sid_;
			serial = serial_;
			online = true;
		}
		void unbind_session(std::uint32_t sid_, std::uint32_t serial_) {
			// stale disconnect 방어: sid/serial이 다르면 무시
			if (sid != sid_ || serial != serial_) return;
			online = false;
			sid = kInvalidSid;
			serial = 0;
		}
		bool has_session() const { return online && sid != kInvalidSid; }

		bool CanAddItem(std::uint32_t item_id, std::uint32_t count) const {
			(void)count;
			// 샘플: 서로 다른 아이템 종류 최대 30개
			if (item_id == 0) return true;
			if (items.find(item_id) != items.end()) return true;
			return items.size() < 30;
		}

		void CommitLoot(std::uint64_t tx_id, std::uint32_t item_id, std::uint32_t count, std::uint32_t add_gold) {
			// 멱등 커밋
			if (tx_id != 0) {
				if (committed_loot_txs.find(tx_id) != committed_loot_txs.end()) return;
				committed_loot_txs.insert(tx_id);
				// 너무 커지면 적당히 trim (샘플)
				if (committed_loot_txs.size() > 2048) {
					committed_loot_txs.clear();
				}
			}
			if (item_id != 0 && count != 0) items[item_id] += count;
			combat.gold += add_gold;
		}

	public:
		std::uint64_t char_id = 0;
		bool online = false;
		static constexpr std::uint32_t kInvalidSid = 0xFFFFFFFFu;
		std::uint32_t sid = kInvalidSid;
		std::uint32_t serial = 0;

		// zone/aoi
		std::uint32_t zone_id = 1;
		Vec2i pos{};

		CharCombatState combat{};
		std::unordered_map<std::uint32_t, std::uint32_t> items; // item_id -> count
		std::unordered_set<std::uint64_t> committed_loot_txs;    // 멱등 보장
	};

	// ✅ ZoneActor: AOI/브로드캐스트 + 몬스터/드랍 coordinator
	class ZoneActor final : public net::IActor {
	public:
		static constexpr std::int32_t kCellSize = 10; // 샘플: 10 단위

		struct PlayerInfo {
			Vec2i pos{};
			std::int32_t cx = 0;
			std::int32_t cy = 0;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
		};

		// - 좁은 구역에 유저가 많을 때, 한 번의 Move 처리에서 N명에게 Send()를 연속 호출하면
		//   세션 send_q가 burst로 커지면서 overflow/close가 발생할 수 있다.
		// - 그래서 ZoneActor에서 "보낼 것"을 pending_net_에 쌓고, 1회 처리당 budget만큼만 flush한다.
		// - flush되지 못한 전송은 다음 ZoneActor 작업에서 이어서 처리된다(빈번히 move가 오므로 자연스럽게 drain됨).
		struct PendingNetSend {
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			_MSG_HEADER h{};
			std::shared_ptr<std::vector<char>> body; // nullable
			std::uint16_t msg_type = 0;
			std::uint64_t move_char_id = 0; // for coalesce hint

			std::size_t bytes() const noexcept { return (std::size_t)h.m_wSize; }
		};

		// ✅ per-session pending queue + bytes tracking
		std::unordered_map<std::uint32_t, std::deque<PendingNetSend>> pending_by_sid_;
		std::unordered_map<std::uint32_t, std::size_t> pending_bytes_by_sid_;

		// ✅ pending limits (avoid server memory blow-up)
		static constexpr std::size_t kMaxPendingMsgsPerSid = 5000;
		static constexpr std::size_t kMaxPendingBytesPerSid = 4 * 1024 * 1024;


		// ====== (AOI) Tick broadcast for dense area optimization ======
		// - Move 이벤트를 즉시 브로드캐스트하지 않고, 짧은 틱(예: 10ms) 동안 모아서 한 번에 전송한다.
		// - 같은 char_id의 move는 마지막 값만 유지(Coalescing).
		static constexpr std::int32_t kMoveTickMs = 10;

		struct PendingMove {
			std::uint64_t char_id = 0;
			Vec2i pos{};
		};
		std::unordered_map<std::uint64_t, PendingMove> pending_moves_; // char_id -> last move

		struct PendingBenchAck {
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint32_t seq = 0;
			std::uint64_t client_ts_ns = 0;
			std::uint32_t zone_id = 0;
		};
		std::deque<PendingBenchAck> pending_bench_acks_;

		std::chrono::steady_clock::time_point last_move_tick_tp_{};
		bool move_tick_inited_ = false;


		static std::shared_ptr<std::vector<char>> MakeBody_(const void* p, std::size_t n)
		{
			if (!p || n == 0) return {};
			auto v = std::make_shared<std::vector<char>>(n);
			std::memcpy(v->data(), p, n);
			return v;
		}

		template <class T>
		static std::shared_ptr<std::vector<char>> MakeBody_(const T& pod)
		{
			return MakeBody_(&pod, sizeof(T));
		}

		bool HasPendingNet_() const noexcept
		{
			for (auto& kv : pending_by_sid_) {
				if (!kv.second.empty()) return true;
			}
			return false;
		}

		void EnqueueSend_(std::uint32_t sid, std::uint32_t serial, const _MSG_HEADER& h,
			std::shared_ptr<std::vector<char>> body,
			std::uint16_t msg_type,
			std::uint64_t move_char_id = 0)
		{
			PendingNetSend ps{};
			ps.sid = sid;
			ps.serial = serial;
			ps.h = h;
			ps.body = std::move(body);
			ps.msg_type = msg_type;
			ps.move_char_id = move_char_id;

			auto& q = pending_by_sid_[sid];
			auto& b = pending_bytes_by_sid_[sid];

			const std::size_t add_bytes = ps.bytes();

			// ✅ per-sid pending cap (drop newest on overflow)
			if (q.size() + 1 > kMaxPendingMsgsPerSid || (b + add_bytes) > kMaxPendingBytesPerSid) {
				return;
			}

			// ✅ Move Coalescing:
			// - 같은 sid에게 같은 (msg_type, move_char_id) move가 여러 개 쌓이면 최신 1개만 유지.
			// - burst 상황에서 send_q/pending 폭주를 막는다.
			if (move_char_id != 0) {
				const std::size_t scan = std::min<std::size_t>(q.size(), 256);
				for (std::size_t i = 0; i < scan; ++i) {
					auto& prev = q[q.size() - 1 - i];
					if (prev.msg_type == msg_type && prev.move_char_id == move_char_id) {
						// bytes가 달라질 수 있으니 보정
						const std::size_t prev_bytes = prev.bytes();
						if (b >= prev_bytes) b -= prev_bytes;
						prev = std::move(ps);
						b += prev.bytes();
						return;
					}
				}
			}

			b += add_bytes;
			q.push_back(std::move(ps));
		}

		// ✅ Drain pending sends with budgets.
		// - uses TrySendLossy() to avoid disconnect on overflow
		// - if a session is saturated, keep its messages in pending (backpressure)
		void FlushPendingSends_(CNetworkEX& net, std::uint32_t pro_id,
			std::size_t budget_msgs = 2000,
			std::size_t budget_bytes = 2 * 1024 * 1024)
		{
			std::size_t n = 0;
			std::size_t bytes = 0;

			// snapshot sids for stable iteration
			std::vector<std::uint32_t> sids;
			sids.reserve(pending_by_sid_.size());
			for (auto& kv : pending_by_sid_) {
				if (!kv.second.empty()) sids.push_back(kv.first);
			}

			for (auto sid : sids) {
				auto itq = pending_by_sid_.find(sid);
				if (itq == pending_by_sid_.end()) continue;
				auto& q = itq->second;
				auto& b = pending_bytes_by_sid_[sid];

				while (n < budget_msgs && bytes < budget_bytes && !q.empty()) {
					auto ps = std::move(q.front());
					q.pop_front();
					b -= ps.bytes();

					const char* body_ptr = (ps.body && !ps.body->empty()) ? ps.body->data() : nullptr;

					// if saturated: push back and move to next sid
					if (!net.TrySendLossy(pro_id, ps.sid, ps.serial, ps.h, body_ptr)) {
						q.push_front(std::move(ps));
						b += q.front().bytes();
						break;
					}

					bytes += ps.h.m_wSize;
					++n;
				}

				if (q.empty()) {
					pending_by_sid_.erase(sid);
					pending_bytes_by_sid_.erase(sid);
				}

				if (n >= budget_msgs || bytes >= budget_bytes) break;
			}
		}

		std::uint64_t next_monster_id = 1;
		std::uint64_t next_tx_id = 1;
		std::unordered_map<std::uint64_t, MonsterState> monsters;

		std::unordered_map<std::uint64_t, PlayerInfo> players; // char_id -> info
		std::unordered_map<std::int64_t, std::unordered_set<std::uint64_t>> cells; // cell_key -> set<char_id>

		static std::int32_t ToCell(std::int32_t v) {
			// floor division for negatives
			if (v >= 0) return v / kCellSize;
			return -(((-v) + kCellSize - 1) / kCellSize);
		}
		static std::int64_t CellKey(std::int32_t cx, std::int32_t cy) {
			return (std::int64_t(cx) << 32) ^ (std::uint32_t)cy;
		}

		std::vector<std::uint64_t> GatherNeighborsVec(std::int32_t cx, std::int32_t cy) {
			std::vector<std::uint64_t> out;
			std::size_t approx = 0;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					auto it = cells.find(CellKey(cx + dx, cy + dy));
					if (it == cells.end()) continue;
					approx += it->second.size();
				}
			}
			out.reserve(approx);
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					auto it = cells.find(CellKey(cx + dx, cy + dy));
					if (it == cells.end()) continue;
					for (auto id : it->second) out.push_back(id);
				}
			}
			return out;
		}

		void JoinOrUpdate(std::uint64_t char_id, Vec2i pos, std::uint32_t sid, std::uint32_t serial) {
			auto& pi = players[char_id];
			pi.pos = pos;
			pi.cx = ToCell(pos.x);
			pi.cy = ToCell(pos.y);
			pi.sid = sid;
			pi.serial = serial;
			cells[CellKey(pi.cx, pi.cy)].insert(char_id);
		}

		void Leave(std::uint64_t char_id) {
			auto it = players.find(char_id);
			if (it == players.end()) return;
			auto ck = CellKey(it->second.cx, it->second.cy);
			auto itc = cells.find(ck);
			if (itc != cells.end()) {
				itc->second.erase(char_id);
				if (itc->second.empty()) cells.erase(itc);
			}
			players.erase(it);
		}

		// Move는 ZoneActor 밖에서 old/new visible 계산 및 브로드캐스트용으로 사용하기 위해
		// old/new visible set을 반환한다.

	// ✅ (Fast) cell update only: old/new visible diff 계산을 하지 않는다.
	// - 벤치/밀집 최적화 경로에서 사용.
		void MoveFastUpdate(std::uint64_t char_id, Vec2i new_pos, std::uint32_t sid, std::uint32_t serial) {
			auto it = players.find(char_id);
			if (it == players.end()) {
				JoinOrUpdate(char_id, new_pos, sid, serial);
			}
			else {
				auto& pi = it->second;
				pi.sid = sid;
				pi.serial = serial;

				const std::int32_t ncx = ToCell(new_pos.x);
				const std::int32_t ncy = ToCell(new_pos.y);

				if (ncx != pi.cx || ncy != pi.cy) {
					auto oldk = CellKey(pi.cx, pi.cy);
					auto itc = cells.find(oldk);
					if (itc != cells.end()) {
						itc->second.erase(char_id);
						if (itc->second.empty()) cells.erase(itc);
					}
					cells[CellKey(ncx, ncy)].insert(char_id);
					pi.cx = ncx;
					pi.cy = ncy;
				}
				pi.pos = new_pos;
			}

			// last-write-wins move buffer
			pending_moves_[char_id] = PendingMove{ char_id, new_pos };
		}

		void EnqueueBenchAck(std::uint32_t sid, std::uint32_t serial, std::uint32_t seq, std::uint64_t client_ts_ns, std::uint32_t zone_id) {
			PendingBenchAck a{};
			a.sid = sid;
			a.serial = serial;
			a.seq = seq;
			a.client_ts_ns = client_ts_ns;
			a.zone_id = zone_id;
			pending_bench_acks_.push_back(a);
		}

		// ✅ Tick flush:
		// - pending_moves_를 각 수신자 sid별로 모아서 player_move_batch로 1회 전송
		// - FlushPendingSends_는 budget 기반으로 TrySendLossy()
		bool FlushMoveTickIfDue_(CNetworkEX& net, std::uint32_t pro_id, bool force = false)
		{
			using clock = std::chrono::steady_clock;
			const auto now = clock::now();
			if (!move_tick_inited_) {
				last_move_tick_tp_ = now;
				move_tick_inited_ = true;
			}

			if (!force) {
				const auto elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - last_move_tick_tp_).count();
				if (elapsed_ms < kMoveTickMs) return false;
			}

			if (pending_moves_.empty() && pending_bench_acks_.empty()) {
				last_move_tick_tp_ = now;
				return true;
			}

			// sid -> items
			std::unordered_map<std::uint32_t, std::vector<proto::S2C_player_move_item>> out;
			out.reserve(std::max<std::size_t>(8, pending_by_sid_.size()));


			std::unordered_map<std::uint32_t, std::uint32_t> sid_serial;
			sid_serial.reserve(128);

			for (auto& kv : pending_moves_) {
				const auto mover_id = kv.first;
				auto itp = players.find(mover_id);
				if (itp == players.end()) continue;
				const auto& pi = itp->second;

				// recipients in 9 cells around mover
				auto rids = GatherNeighborsVec(pi.cx, pi.cy);

				proto::S2C_player_move_item item{};
				item.char_id = mover_id;
				item.x = kv.second.pos.x;
				item.y = kv.second.pos.y;

				for (auto rid : rids) {
					if (rid == mover_id) continue;
					auto itr = players.find(rid);
					if (itr == players.end()) continue;
					if (itr->second.sid == 0 || itr->second.serial == 0) continue;
					out[itr->second.sid].push_back(item);
					sid_serial[itr->second.sid] = itr->second.serial;
				}
			}


			// build & enqueue batches (one packet per sid)
			// - out[sid]에 모은 move item들을 player_move_batch로 1회 전송
			for (auto& kv : out) {
				const std::uint32_t sid = kv.first;
				const auto itser = sid_serial.find(sid);
				if (itser == sid_serial.end()) continue;
				const std::uint32_t serial = itser->second;

				auto& items = kv.second;
				const std::uint16_t count = (std::uint16_t)std::min<std::size_t>(items.size(), 4096);

				const std::size_t body_size = sizeof(std::uint16_t) + (std::size_t)count * sizeof(proto::S2C_player_move_item);
				auto body = std::make_shared<std::vector<char>>(body_size);
				std::memcpy(body->data(), &count, sizeof(std::uint16_t));
				if (count > 0) {
					std::memcpy(body->data() + sizeof(std::uint16_t), items.data(), (std::size_t)count * sizeof(proto::S2C_player_move_item));
				}

				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_move_batch, (std::uint16_t)body_size);
				EnqueueSend_(sid, serial, h, std::move(body), (std::uint16_t)proto::S2CMsg::player_move_batch);
			}

			pending_moves_.clear();

			// ✅ flush batched moves first (budgeted)
			FlushPendingSends_(net, pro_id, 4000, 4 * 1024 * 1024);

			// ✅ bench acks after broadcast flush
			while (!pending_bench_acks_.empty()) {
				auto a = pending_bench_acks_.front();
				pending_bench_acks_.pop_front();

				proto::S2C_bench_move_ack ack{};
				ack.ok = 1;
				ack.seq = a.seq;
				ack.client_ts_ns = a.client_ts_ns;
				ack.zone = a.zone_id;
				ack.shard = (proto::u32)std::max(0, net::ActorSystem::current_shard_index());
				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::bench_move_ack, (std::uint16_t)sizeof(ack));

				if (a.serial != 0) {
					net.TrySendLossy(pro_id, a.sid, a.serial, h, reinterpret_cast<const char*>(&ack));
				}
			}

			last_move_tick_tp_ = now;
			return true;
		}

		struct MoveDiff {
			Vec2i new_pos{};
			std::vector<std::uint64_t> old_vis;
			std::vector<std::uint64_t> new_vis;;
		};

		MoveDiff Move(std::uint64_t char_id, Vec2i new_pos, std::uint32_t sid, std::uint32_t serial) {
			MoveDiff d{};
			d.new_pos = new_pos;
			auto it = players.find(char_id);
			if (it == players.end()) {
				JoinOrUpdate(char_id, new_pos, sid, serial);
				// old_vis empty, new_vis will be based on current
				auto& pi = players[char_id];
				d.new_vis = GatherNeighborsVec(pi.cx, pi.cy);
				d.new_vis.erase(std::remove(d.new_vis.begin(), d.new_vis.end(), char_id), d.new_vis.end());
				return d;
			}

			auto& pi = it->second;
			d.old_vis = GatherNeighborsVec(pi.cx, pi.cy);
			d.old_vis.erase(std::remove(d.old_vis.begin(), d.old_vis.end(), char_id), d.old_vis.end());

			pi.sid = sid;
			pi.serial = serial;

			const std::int32_t ncx = ToCell(new_pos.x);
			const std::int32_t ncy = ToCell(new_pos.y);

			if (ncx != pi.cx || ncy != pi.cy) {
				// remove old cell
				auto oldk = CellKey(pi.cx, pi.cy);
				auto itc = cells.find(oldk);
				if (itc != cells.end()) {
					itc->second.erase(char_id);
					if (itc->second.empty()) cells.erase(itc);
				}
				// add new cell
				cells[CellKey(ncx, ncy)].insert(char_id);
				pi.cx = ncx;
				pi.cy = ncy;
			}
			pi.pos = new_pos;

			d.new_vis = GatherNeighborsVec(pi.cx, pi.cy);
			d.new_vis.erase(std::remove(d.new_vis.begin(), d.new_vis.end(), char_id), d.new_vis.end());
			return d;
		}
	};

} // namespace svr
