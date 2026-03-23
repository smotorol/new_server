#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "db/core/dqs_payloads.h"
#include "db/core/dqs_results.h"
#include "db/core/dqs_types.h"
#include "proto/common/proto_base.h"
#include "server_common/session/session_key.h"
#include "services/world/actors/zone_actor.h"

namespace {

	bool TestSessionKeyPackUnpack()
	{
		const std::uint32_t sid = 123456789u;
		const std::uint32_t serial = 987654321u;
		const auto packed = dc::PackSessionKey(sid, serial);

		if (dc::UnpackSessionSid(packed) != sid) {
			return false;
		}
		if (dc::UnpackSessionSerial(packed) != serial) {
			return false;
		}
		if (!dc::MatchesPackedSessionKey(packed, sid, serial)) {
			return false;
		}
		return true;
	}

	bool TestSpawnBatchLayout()
	{
		static_assert(sizeof(proto::S2C_player_spawn_batch) == 18);
		static_assert(sizeof(proto::S2C_player_despawn_batch) == 10);

		const std::uint16_t count = 3;
		const std::size_t body_size = sizeof(proto::S2C_player_spawn_batch)
			+ (static_cast<std::size_t>(count) - 1) * sizeof(proto::S2C_player_spawn_item);

		std::vector<char> body(body_size);
		auto* pkt = reinterpret_cast<proto::S2C_player_spawn_batch*>(body.data());
		pkt->count = count;
		for (std::uint16_t i = 0; i < count; ++i) {
			pkt->items[i].char_id = i + 1;
			pkt->items[i].x = static_cast<std::int32_t>(i * 10);
			pkt->items[i].y = static_cast<std::int32_t>(i * 20);
		}

		for (std::uint16_t i = 0; i < count; ++i) {
			if (pkt->items[i].char_id != i + 1) return false;
			if (pkt->items[i].x != static_cast<std::int32_t>(i * 10)) return false;
			if (pkt->items[i].y != static_cast<std::int32_t>(i * 20)) return false;
		}
		return true;
	}

	bool TestDespawnBatchLayout()
	{
		const std::uint16_t count = 4;
		const std::size_t body_size = sizeof(proto::S2C_player_despawn_batch)
			+ (static_cast<std::size_t>(count) - 1) * sizeof(proto::S2C_player_despawn_item);

		std::vector<char> body(body_size);
		auto* pkt = reinterpret_cast<proto::S2C_player_despawn_batch*>(body.data());
		pkt->count = count;
		for (std::uint16_t i = 0; i < count; ++i) {
			pkt->items[i].char_id = static_cast<std::uint64_t>(100 + i);
		}

		for (std::uint16_t i = 0; i < count; ++i) {
			if (pkt->items[i].char_id != static_cast<std::uint64_t>(100 + i)) {
				return false;
			}
		}
		return true;
	}


	bool TestAoiOneCellEnteredLeft()
	{
		svr::ZoneActor z;
		if (!z.InitSectorSystem({ 100, 100 }, 10, 1)) {
			return false;
		}

		const auto entered = z.CalcEnteredCells(5, 5, 6, 5);
		const auto left = z.CalcLeftCells(5, 5, 6, 5);

		if (entered.size() != 3 || left.size() != 3) {
			return false;
		}

		std::vector<std::int64_t> expected_entered{
			svr::ZoneActor::SectorContainer::CellKey(7, 4),
			svr::ZoneActor::SectorContainer::CellKey(7, 5),
			svr::ZoneActor::SectorContainer::CellKey(7, 6),
		};
		std::vector<std::int64_t> expected_left{
			svr::ZoneActor::SectorContainer::CellKey(4, 4),
			svr::ZoneActor::SectorContainer::CellKey(4, 5),
			svr::ZoneActor::SectorContainer::CellKey(4, 6),
		};

		auto sortv = [](std::vector<std::int64_t>& v) { std::sort(v.begin(), v.end()); };
		auto entered_sorted = entered;
		auto left_sorted = left;
		sortv(entered_sorted);
		sortv(left_sorted);
		sortv(expected_entered);
		sortv(expected_left);

		if (entered_sorted != expected_entered) {
			return false;
		}
		if (left_sorted != expected_left) {
			return false;
		}
		return true;
	}



	bool TestAoiMoveDiffRecipientsAndIds()
	{
		svr::ZoneActor z;
		if (!z.InitSectorSystem({ 100, 100 }, 10, 1)) {
			return false;
		}

		// mover(1): (1,1) -> (2,1)
		z.JoinOrUpdate(1, { 15, 15 }, 1001, 1);
		// enters on move-right: cell(3,1)
		z.JoinOrUpdate(2, { 35, 15 }, 1002, 1);
		// exits on move-right: cell(0,1)
		z.JoinOrUpdate(3, { 5, 15 }, 1003, 1);
		// remains visible: cell(2,1)
		z.JoinOrUpdate(4, { 25, 15 }, 1004, 1);

		auto d = z.Move(1, { 25, 15 }, 1001, 2);

		auto sort_unique = [](std::vector<std::uint64_t>& v) {
			std::sort(v.begin(), v.end());
			v.erase(std::unique(v.begin(), v.end()), v.end());
		};

		auto entered = d.entered_vis;
		auto exited = d.exited_vis;
		auto now_vis = d.new_vis;
		sort_unique(entered);
		sort_unique(exited);
		sort_unique(now_vis);

		if (entered != std::vector<std::uint64_t>{2}) {
			return false;
		}
		if (exited != std::vector<std::uint64_t>{3}) {
			return false;
		}
		if (now_vis != std::vector<std::uint64_t>({2, 4})) {
			return false;
		}

		for (auto id : entered) {
			if (id == 0 || id == 1) return false;
		}
		for (auto id : exited) {
			if (id == 0 || id == 1) return false;
		}
		for (auto id : now_vis) {
			if (id == 0 || id == 1) return false;
		}

		return true;
	}

	bool TestFlushOneCharVersionFields()
	{
		svr::dqs_payload::FlushOneChar payload{};
		payload.world_code = 7;
		payload.char_id = 99;
		payload.expected_version = 42;

		if (payload.expected_version != 42) {
			return false;
		}

		svr::dqs_result::FlushOneCharResult res{};
		res.world_code = payload.world_code;
		res.char_id = payload.char_id;
		res.expected_version = payload.expected_version;
		res.actual_version = 41;
		res.saved = false;
		res.result = svr::dqs::ResultCode::conflict;

		if (res.result != svr::dqs::ResultCode::conflict) {
			return false;
		}
		if (res.expected_version != 42 || res.actual_version != 41) {
			return false;
		}
		return true;
	}

	bool TestFlushDirtyCharsConflictFields()
	{
		svr::dqs_result::FlushDirtyCharsResult res{};
		res.world_code = 7;
		res.shard_id = 3;
		res.max_batch = 64;
		res.pulled = 10;
		res.saved = 7;
		res.failed = 1;
		res.conflicts = 2;
		res.result = svr::dqs::ResultCode::db_error;

		if (res.shard_id != 3) {
			return false;
		}
		if (res.conflicts != 2) {
			return false;
		}
		if (res.saved + res.failed + res.conflicts != 10) {
			return false;
		}
		return true;
	}

} // namespace

int main()
{
	const bool ok_session = TestSessionKeyPackUnpack();
	const bool ok_batch = TestSpawnBatchLayout();
	const bool ok_despawn_batch = TestDespawnBatchLayout();
	const bool ok_aoi_one_cell = TestAoiOneCellEnteredLeft();
	const bool ok_aoi_recipients = TestAoiMoveDiffRecipientsAndIds();
	const bool ok_flush = TestFlushOneCharVersionFields();
	const bool ok_dirty = TestFlushDirtyCharsConflictFields();

	if (!ok_session || !ok_batch || !ok_despawn_batch || !ok_aoi_one_cell || !ok_aoi_recipients || !ok_flush || !ok_dirty) {
		std::cerr
			<< "world_regression_tests failed:"
			<< " session=" << ok_session
			<< " batch=" << ok_batch
			<< " despawn_batch=" << ok_despawn_batch
			<< " aoi_one_cell=" << ok_aoi_one_cell
			<< " aoi_recipients=" << ok_aoi_recipients
			<< " flush=" << ok_flush
			<< " dirty=" << ok_dirty
			<< "\n";
		return 1;
	}

	std::cout << "world_regression_tests passed\n";
	return 0;
}
