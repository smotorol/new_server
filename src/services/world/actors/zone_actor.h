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

#include "net/actor/actor_system.h"
#include "proto/common/proto_base.h"
#include "proto/common/packet_util.h"

#include "services/world/bench/bench_stats.h"
#include "services/world/actors/zone_aoi_types.h"
#include "services/world/metrics/world_metrics.h"
#include "server_common/handler/service_line_handler_base.h"
#include "server_common/session/session_key.h"

namespace svr {
class ZoneActor final : public net::IActor {
	public:
		static constexpr std::int32_t kCellSize = 10; // 샘플: 10 단위

		// ====== (레거시 eos_royal 스타일) 섹터(셀) 컨테이너 ======
		// - init_sector(map_size, unit): 맵 크기 / 셀 단위 / AOI 반경(셀 단위)을 초기화
		// - 좌표 -> 셀 변환/클램프, 브로드캐스트(주변 셀) 목록 제공
		struct SectorContainer final {
			bool inited = false;
			Vec2i map_size{};          // world width/height (임시)
			std::int32_t unit = 10;    // WORLD_SIGHT_UNIT 느낌(셀 한 변 크기)
			std::int32_t aoi_r = 1;    // 주변 셀 반경(1이면 3x3)

			std::int32_t max_cx = 0;
			std::int32_t max_cy = 0;

			// 방향( dx,dy in [-1..1] ) 별 edge offsets 캐시
			// idx = (dx+1) + (dy+1)*3
			std::array<std::vector<Vec2i>, 9> entered_edge_offs{}; // new window에 새로 들어온 edge
			std::array<std::vector<Vec2i>, 9> left_edge_offs{};    // old window에서 빠진 edge

			static constexpr int DirIndex(int dx, int dy) noexcept {
				return (dx + 1) + (dy + 1) * 3;
			}

			bool init_sector(Vec2i rc_size, std::int32_t world_sight_unit, std::int32_t aoi_radius_cells = 1) {
				if (world_sight_unit <= 0) return false;
				if (rc_size.x <= 0 || rc_size.y <= 0) return false;
				if (aoi_radius_cells < 0) aoi_radius_cells = 0;

				map_size = rc_size;
				unit = world_sight_unit;
				aoi_r = aoi_radius_cells;

				// 셀 인덱스 최대치(0..max)
				max_cx = (map_size.x - 1) / unit;
				max_cy = (map_size.y - 1) / unit;

				inited = true;
				return true;
			}

			// floor division for negatives
			std::int32_t ToCell(std::int32_t v) const {
				if (v >= 0) return v / unit;
				return -(((-v) + unit - 1) / unit);
			}

			std::int32_t ClampCx(std::int32_t cx) const {
				if (!inited) return cx;
				if (cx < 0) return 0;
				if (cx > max_cx) return max_cx;
				return cx;
			}
			std::int32_t ClampCy(std::int32_t cy) const {
				if (!inited) return cy;
				if (cy < 0) return 0;
				if (cy > max_cy) return max_cy;
				return cy;
			}

			Vec2i ToCellClamped(Vec2i pos) const {
				Vec2i c{};
				c.x = ClampCx(ToCell(pos.x));
				c.y = ClampCy(ToCell(pos.y));
				return c;
			}

			// (cx,cy) -> key
			static std::int64_t CellKey(std::int32_t cx, std::int32_t cy) {
				return (std::int64_t(cx) << 32) ^ (std::uint32_t)cy;
			}

			// 주변 셀 목록(반경=aoi_r). (기본은 3x3)
			std::vector<std::int64_t> BroadCells(std::int32_t cx, std::int32_t cy) const {
				std::vector<std::int64_t> out;
				const int r = std::max(0, aoi_r);
				out.reserve((std::size_t)(2 * r + 1) * (2 * r + 1));
				for (int dy = -r; dy <= r; ++dy) {
					for (int dx = -r; dx <= r; ++dx) {
						const auto ncx = ClampCx(cx + dx);
						const auto ncy = ClampCy(cy + dy);
						out.push_back(CellKey(ncx, ncy));
					}
				}
				return out;
			}

			// 3x3 AOI 셀키 목록(가장 흔한 케이스: aoi_r=1)
			std::array<std::int64_t, 9> BroadCells3x3(std::int32_t cx, std::int32_t cy) const {
				std::array<std::int64_t, 9> out{};
				int i = 0;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const auto ncx = ClampCx(cx + dx);
						const auto ncy = ClampCy(cy + dy);
						out[i++] = CellKey(ncx, ncy);
					}
				}
				return out;
			}

			// 레거시 ResetSector 패턴: 셀 이동(1칸) 시, entered/left edge 셀 목록 제공
			// - dx,dy는 -1/0/1만 지원(그 외는 full diff 경로로 처리 권장)
			const std::vector<Vec2i>& EnteredEdgeOffsets(int dx, int dy) const {
				return entered_edge_offs[DirIndex(dx, dy)];
			}
			const std::vector<Vec2i>& LeftEdgeOffsets(int dx, int dy) const {
				return left_edge_offs[DirIndex(dx, dy)];
			}

		private:
			void BuildEdgeCaches_() {
				for (auto& v : entered_edge_offs) v.clear();
				for (auto& v : left_edge_offs) v.clear();

				const int r = std::max(0, aoi_r);
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const int idx = DirIndex(dx, dy);
						if (dx == 0 && dy == 0) continue;

						auto& ent = entered_edge_offs[idx];
						auto& lef = left_edge_offs[idx];

						// entered: new window에서 새로 들어온 edge
						if (dx == 1) {
							for (int oy = -r; oy <= r; ++oy) ent.push_back({ r, oy });
						}
						else if (dx == -1) {
							for (int oy = -r; oy <= r; ++oy) ent.push_back({ -r, oy });
						}
						if (dy == 1) {
							for (int ox = -r; ox <= r; ++ox) ent.push_back({ ox, r });
						}
						else if (dy == -1) {
							for (int ox = -r; ox <= r; ++ox) ent.push_back({ ox, -r });
						}
						// 중복 제거(대각 이동에서 코너 중복 가능)
						std::sort(ent.begin(), ent.end(), [](const Vec2i& a, const Vec2i& b) {
							if (a.x != b.x) return a.x < b.x;
							return a.y < b.y;
						});
						ent.erase(std::unique(ent.begin(), ent.end(), [](const Vec2i& a, const Vec2i& b) {
							return a.x == b.x && a.y == b.y;
						}), ent.end());

						// left: old window에서 빠진 edge(entered의 반대편)
						if (dx == 1) {
							for (int oy = -r; oy <= r; ++oy) lef.push_back({ -r, oy });
						}
						else if (dx == -1) {
							for (int oy = -r; oy <= r; ++oy) lef.push_back({ r, oy });
						}
						if (dy == 1) {
							for (int ox = -r; ox <= r; ++ox) lef.push_back({ ox, -r });
						}
						else if (dy == -1) {
							for (int ox = -r; ox <= r; ++ox) lef.push_back({ ox, r });
						}
						std::sort(lef.begin(), lef.end(), [](const Vec2i& a, const Vec2i& b) {
							if (a.x != b.x) return a.x < b.x;
							return a.y < b.y;
						});
						lef.erase(std::unique(lef.begin(), lef.end(), [](const Vec2i& a, const Vec2i& b) {
							return a.x == b.x && a.y == b.y;
						}), lef.end());
					}
				}
			}
		};
		// eos_royal처럼: 맵 크기/섹터 단위 초기화
		//  - 예: if (!sector_container_.init_sector({map_w,map_h}, WORLD_SIGHT_UNIT)) { ... }
		bool InitSectorSystem(Vec2i map_rc_size, std::int32_t world_sight_unit, std::int32_t aoi_radius_cells = 1) {
			return sector_container_.init_sector(map_rc_size, world_sight_unit, aoi_radius_cells);
		}

		const SectorContainer& sector() const { return sector_container_; }
		SectorContainer& sector() { return sector_container_; }

	private:
		SectorContainer sector_container_{};

	public:
		struct PlayerInfo {
			Vec2i pos{};
			std::int32_t cx = 0;
			std::int32_t cy = 0;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
		};

		// ====== (사용성) 레거시처럼 쓰는 "간단 API" ======
		// - SendToUser(): 특정 세션에게 1:1 전송
		// - PublishToMyBroadCells(): 나의 현재 셀 기준 3x3(주변 섹터)에게 publish
		//   (eos_royal의 get_broad_sectors() + deliver_to_all_proxy 느낌)
		void SendToUser(std::uint32_t sid, std::uint32_t serial, proto::S2CMsg msg,
			const void* body, std::size_t body_len)
		{
			auto h = proto::make_header((std::uint16_t)msg, (std::uint16_t)body_len);
			EnqueueSend_(sid, serial, h, MakeBody_(body, body_len), (std::uint16_t)msg);
		}

		template <class T>
		void SendToUser(std::uint32_t sid, std::uint32_t serial, proto::S2CMsg msg, const T& pod)
		{
			SendToUser(sid, serial, msg, &pod, sizeof(T));
		}

		void PublishToMyBroadCells(std::uint64_t char_id, proto::S2CMsg msg,
			std::shared_ptr<std::vector<char>> body)
		{
			auto it = players.find(char_id);
			if (it == players.end()) return;
			auto h = proto::make_header((std::uint16_t)msg, (std::uint16_t)(body ? body->size() : 0));
			CellChannels{ *this }.PublishToBroadCells(it->second.cx, it->second.cy, h, std::move(body), (std::uint16_t)msg);
		}

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
		void FlushPendingSends_(dc::ServiceLineHandlerBase& net, std::uint32_t pro_id,
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

		// 기본 키 함수(외부 util이 이걸 쓰는 경우가 있어 유지)
		static std::int64_t CellKey(std::int32_t cx, std::int32_t cy) {
			return SectorContainer::CellKey(cx, cy);
		}

		// ====== (eos_royal 스타일) 셀 채널(pub-sub) 브로드캐스트 헬퍼 ======
		// 레거시처럼 "섹터키만 바꿔서 deliver" 하듯이, 여기서는 "셀(채널)"에 publish 한다.
		// 호출자는 "누구에게 보낼지"를 신경쓰지 않고, "어느 셀/어느 주변 셀"에 publish 할지만 결정하면 된다.
		struct CellChannels final {
			ZoneActor& z;

			// 3x3 AOI(자기 셀 + 주변 8셀)의 셀키 목록
			std::array<std::int64_t, 9> BroadCells(std::int32_t cx, std::int32_t cy) const {
				return z.sector_container_.BroadCells3x3(cx, cy);
			}

			// "셀 채널"에 publish: 해당 셀에 있는 모든 세션에게 동일 body(shared_ptr)를 큐잉
			void PublishToCell(std::int64_t cell_key, const _MSG_HEADER& h,
				std::shared_ptr<std::vector<char>> body,
				std::uint16_t msg_type)
			{
				auto it = z.cells.find(cell_key);
				if (it == z.cells.end()) return;
				const auto& recv_ids = it->second;

				// ✅ metrics (server-side) : 클라 recv_move_pkts/items 와 맞추기 위해
				// "실제로 각 세션에게 enqueue"되는 수만큼 누적한다.
				std::uint16_t move_items = 0;
				if (msg_type == (std::uint16_t)proto::S2CMsg::player_move_batch) {
					if (body && body->size() >= sizeof(std::uint16_t)) {
						std::memcpy(&move_items, body->data(), sizeof(std::uint16_t));
					}
				}
				else if (msg_type == (std::uint16_t)proto::S2CMsg::player_move) {
					move_items = 1;
				}
				// bench_move_ack 등은 제외
				for (auto rid : recv_ids) {
					auto itr = z.players.find(rid);
					if (itr == z.players.end()) continue;
					const auto sid = itr->second.sid;
					const auto serial = itr->second.serial;
					if (sid == 0 || serial == 0) continue;
					z.EnqueueSend_(sid, serial, h, body, msg_type);

					if (move_items > 0) {
						svr::metrics::g_s2c_move_pkts_sent.fetch_add(1, std::memory_order_relaxed);
						svr::metrics::g_s2c_move_items_sent.fetch_add(move_items, std::memory_order_relaxed);
					}
				}
			}

			// "내 셀 기준" 주변 3x3 셀 채널에 publish (eos_royal의 get_broad_sectors() 느낌)
			void PublishToBroadCells(std::int32_t cx, std::int32_t cy, const _MSG_HEADER& h,
				std::shared_ptr<std::vector<char>> body,
				std::uint16_t msg_type)
			{
				for (auto ck : BroadCells(cx, cy)) {
					PublishToCell(ck, h, body, msg_type);
				}
			}
		};

		// ====== (사용성) eos_royal 감성: get_sector()->get_broad_sectors() ======
		// - 외부 코드가 "내 섹터"를 먼저 얻고, 거기서 broad 목록을 꺼내 쓰는 패턴을 흉내낸다.
		struct SectorView final {
			ZoneActor& z;
			std::int32_t cx = 0;
			std::int32_t cy = 0;

			std::array<std::int64_t, 9> get_broad_sectors() const {
				return z.sector_container_.BroadCells3x3(cx, cy);
			}

			// 셀 1칸 이동 시, 새로 들어온/빠진 edge offsets(레거시 calc_enter_leave_sector 감성)
			const std::vector<Vec2i>& entered_edge_offsets(int dx, int dy) const {
				return z.sector_container_.EnteredEdgeOffsets(dx, dy);
			}
			const std::vector<Vec2i>& left_edge_offsets(int dx, int dy) const {
				return z.sector_container_.LeftEdgeOffsets(dx, dy);
			}
		};

		SectorView get_sector(std::uint64_t char_id) {
			auto it = players.find(char_id);
			if (it == players.end()) return SectorView{ *this, 0, 0 };
			return SectorView{ *this, it->second.cx, it->second.cy };
		}
		SectorView get_sector_by_cell(std::int32_t cx, std::int32_t cy) {
			return SectorView{ *this, cx, cy };
		}

		// (추가) entered/left cell_key 목록을 빠르게 구한다.
		// - 셀 이동이 1칸(dx,dy in [-1..1])이면 edge 캐시를 사용
		// - 그 외(텔레포트/큰 점프)는 full diff(브로드 전체) 권장
		std::vector<std::int64_t> CalcEnteredCells(std::int32_t old_cx, std::int32_t old_cy,
			std::int32_t new_cx, std::int32_t new_cy) const
		{
			const int dx = new_cx - old_cx;
			const int dy = new_cy - old_cy;
			std::vector<std::int64_t> out;
			if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
				for (const auto& off : sector_container_.EnteredEdgeOffsets(dx, dy)) {
					const auto cx = sector_container_.ClampCx(new_cx + off.x);
					const auto cy = sector_container_.ClampCy(new_cy + off.y);
					out.push_back(SectorContainer::CellKey(cx, cy));
				}
				return out;
			}
			// 큰 점프는 full diff로 처리하는 쪽이 안전(여기서는 간단히 new broad 전체를 entered로 본다)
			return sector_container_.BroadCells(new_cx, new_cy);
		}

		std::vector<std::int64_t> CalcLeftCells(std::int32_t old_cx, std::int32_t old_cy,
			std::int32_t new_cx, std::int32_t new_cy) const
		{
			const int dx = new_cx - old_cx;
			const int dy = new_cy - old_cy;
			std::vector<std::int64_t> out;
			if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
				for (const auto& off : sector_container_.LeftEdgeOffsets(dx, dy)) {
					const auto cx = sector_container_.ClampCx(old_cx + off.x);
					const auto cy = sector_container_.ClampCy(old_cy + off.y);
					out.push_back(SectorContainer::CellKey(cx, cy));
				}
				return out;
			}
			return sector_container_.BroadCells(old_cx, old_cy);
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
			const auto c = sector_container_.ToCellClamped(pos);
			pi.cx = c.x;
			pi.cy = c.y;
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

				auto nc = sector_container_.ToCellClamped(new_pos);
				const std::int32_t ncx = nc.x;
				const std::int32_t ncy = nc.y;

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
		// - (eos_royal 스타일) "셀 채널(pub-sub)" 브로드캐스트
		//   * 수신자(sid)별로 모아 패킷을 만드는 대신, "수신자 셀" 단위로 1회 패킷을 만들고
		//     해당 셀에 존재하는 모든 플레이어(=세션)에게 동일 body(shared_ptr)를 전송한다.
		//   * 각 셀은 자신의 3x3 이웃 셀(관심영역)에서 발생한 move item들을 한 번에 받는다.
		//   * 결과적으로 "sid별 payload 빌드" 비용이 사라지고, 직렬화는 "셀당 1회"만 수행된다.
		// - FlushPendingSends_는 budget 기반으로 TrySendLossy()
		bool FlushMoveTickIfDue_(dc::ServiceLineHandlerBase& net, std::uint32_t pro_id, bool force = false)
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

			// 1) pending_moves_를 "발생 셀" 기준으로 묶는다.
			//    - 발생 셀의 move item 벡터는 이후 각 "수신 셀"의 3x3 윈도우를 구성할 때 재사용된다.
			std::unordered_map<std::int64_t, std::vector<proto::S2C_player_move_item>> src_cell_items;
			src_cell_items.reserve(std::max<std::size_t>(8, pending_moves_.size()));

			for (auto& kv : pending_moves_) {
				const auto mover_id = kv.first;
				auto itp = players.find(mover_id);
				if (itp == players.end()) continue;
				const auto& pi = itp->second;
				const auto ck = CellKey(pi.cx, pi.cy);

				proto::S2C_player_move_item item{};
				item.char_id = mover_id;
				item.x = kv.second.pos.x;
				item.y = kv.second.pos.y;

				src_cell_items[ck].push_back(item);
			}

			// 2) "수신 셀" 단위로 3x3 이웃 셀의 src_cell_items를 합쳐서 패킷을 1개 만들고,
			//    해당 셀에 있는 모든 플레이어(세션)에게 동일 body를 전송한다.
			//    - eos_royal의 sector_key publish와 동일한 효과 (셀 = 채널)
			for (auto& kv_cell : cells) {
				const std::int64_t recv_ck = kv_cell.first;
				const auto& recv_ids = kv_cell.second;
				if (recv_ids.empty()) continue;

				const std::int32_t rcx = (std::int32_t)(recv_ck >> 32);
				const std::int32_t rcy = (std::int32_t)(std::uint32_t)recv_ck;

				// gather items from neighbor 3x3 cells
				std::size_t total = 0;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						auto it = src_cell_items.find(CellKey(rcx + dx, rcy + dy));
						if (it == src_cell_items.end()) continue;
						total += it->second.size();
					}
				}
				if (total == 0) continue;

				const std::uint16_t count = (std::uint16_t)std::min<std::size_t>(total, 4096);
				// ✅ flexible array style: sizeof(batch) + (count-1)*sizeof(item)
				const std::size_t body_size = sizeof(proto::S2C_player_move_batch)
					+ (count > 0 ? ((std::size_t)count - 1) * sizeof(proto::S2C_player_move_item) : 0);
				auto body = std::make_shared<std::vector<char>>(body_size);
				auto* pkt = reinterpret_cast<proto::S2C_player_move_batch*>(body->data());
				pkt->count = count;
				char* dst = reinterpret_cast<char*>(pkt->items);
				std::size_t written = 0;
				for (int dy = -1; dy <= 1 && written < count; ++dy) {
					for (int dx = -1; dx <= 1 && written < count; ++dx) {
						auto it = src_cell_items.find(CellKey(rcx + dx, rcy + dy));
						if (it == src_cell_items.end()) continue;
						auto& vec = it->second;
						const std::size_t can = std::min<std::size_t>(vec.size(), (std::size_t)count - written);
						if (can == 0) continue;
						std::memcpy(dst + written * sizeof(proto::S2C_player_move_item), vec.data(), can * sizeof(proto::S2C_player_move_item));
						written += can;
					}
				}

				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_move_batch, (std::uint16_t)body_size);

				// publish to this cell channel (shared body)
				CellChannels{ *this }.PublishToCell(recv_ck, h, body, (std::uint16_t)proto::S2CMsg::player_move_batch);
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
					if (net.TrySendLossy(pro_id, a.sid, a.serial, h, reinterpret_cast<const char*>(&ack))) {
						svr::metrics::g_s2c_bench_ack_tx.fetch_add(1, std::memory_order_relaxed);
					}
					else {
						svr::metrics::g_s2c_bench_ack_drop.fetch_add(1, std::memory_order_relaxed);
					}
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

			auto nc = sector_container_.ToCellClamped(new_pos);
			const std::int32_t ncx = nc.x;
			const std::int32_t ncy = nc.y;

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
