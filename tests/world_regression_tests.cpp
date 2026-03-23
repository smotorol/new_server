#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "db/core/dqs_payloads.h"
#include "db/core/dqs_results.h"
#include "db/core/dqs_types.h"
#include "proto/common/proto_base.h"
#include "server_common/config/runtime_ini_sanity.h"
#include "server_common/session/session_key.h"
#include "services/world/actors/zone_actor.h"
#include "services/world/common/aoi_broadcast_sanitize.h"

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

	bool TestAoiSanitizeEntityIds()
	{
		const std::vector<std::uint64_t> input{ 0, 10, 10, 11, 0, 11, 12 };
		const auto sanitized = svr::aoi::SanitizeEntityIds(input);
		const std::vector<std::uint64_t> expected{ 10, 11, 12 };
		return sanitized == expected;
	}

	bool TestAoiBatchHelpers()
	{
		if (svr::aoi::ClampBatchEntityCount(0) != 0) {
			return false;
		}
		if (svr::aoi::ClampBatchEntityCount(99999) != svr::aoi::kMaxBatchEntityCount) {
			return false;
		}
		if (svr::aoi::SpawnBatchBodySize(0) != 0 || svr::aoi::DespawnBatchBodySize(0) != 0) {
			return false;
		}

		const std::uint16_t count = 3;
		const auto spawn_size = svr::aoi::SpawnBatchBodySize(count);
		const auto despawn_size = svr::aoi::DespawnBatchBodySize(count);
		if (spawn_size != sizeof(proto::S2C_player_spawn_batch) + 2 * sizeof(proto::S2C_player_spawn_item)) {
			return false;
		}
		if (despawn_size != sizeof(proto::S2C_player_despawn_batch) + 2 * sizeof(proto::S2C_player_despawn_item)) {
			return false;
		}
		return true;
	}

	bool TestAoiMoveEnteredExitedRecipients()
	{
		svr::ZoneActor z;
		if (!z.InitSectorSystem({ 200, 200 }, 10, 1)) {
			return false;
		}

		// mover at (5,5) -> move to (6,5)
		z.JoinOrUpdate(1, { 55, 55 }, 10, 1);
		// old-only watcher (x=4 column)
		z.JoinOrUpdate(100, { 45, 55 }, 20, 1);
		// always-visible watcher (center area)
		z.JoinOrUpdate(300, { 55, 65 }, 30, 1);
		// new-only watcher (x=7 column)
		z.JoinOrUpdate(200, { 75, 55 }, 40, 1);

		const auto diff = z.Move(1, { 65, 55 }, 10, 2);

		const auto has_id = [](const std::vector<std::uint64_t>& v, std::uint64_t id) {
			return std::find(v.begin(), v.end(), id) != v.end();
		};

		if (!has_id(diff.entered_vis, 200)) {
			return false;
		}
		if (!has_id(diff.exited_vis, 100)) {
			return false;
		}
		if (!has_id(diff.new_vis, 300) || !has_id(diff.new_vis, 200)) {
			return false;
		}
		if (has_id(diff.new_vis, 1) || has_id(diff.entered_vis, 1) || has_id(diff.exited_vis, 1)) {
			return false;
		}
		if (has_id(diff.new_vis, 0) || has_id(diff.entered_vis, 0) || has_id(diff.exited_vis, 0)) {
			return false;
		}
		return true;
	}

	bool TestConfigParseHelpers()
	{
		int parsed = 0;
		if (!dc::cfg::TryParseInt("123", parsed) || parsed != 123) {
			return false;
		}
		if (dc::cfg::TryParseInt("12x", parsed)) {
			return false;
		}
		std::uint32_t parsed_u32 = 0;
		if (!dc::cfg::TryParseU32("429", parsed_u32) || parsed_u32 != 429u) {
			return false;
		}
		if (dc::cfg::TryParseU32("-1", parsed_u32) || dc::cfg::TryParseU32("1x", parsed_u32)) {
			return false;
		}

		int value = 7;
		std::string err;
		std::string warn;
		if (!dc::cfg::ParseIntOrKeep("X.KEY", "bad", value, false, &err, &warn)) {
			return false;
		}
		if (value != 7 || warn.empty()) {
			return false;
		}

		value = 9;
		err.clear();
		warn.clear();
		if (dc::cfg::ParseIntOrKeep("X.KEY", "bad", value, true, &err, &warn)) {
			return false;
		}
		if (err.empty()) {
			return false;
		}

		std::uint32_t u32_value = 11;
		err.clear();
		warn.clear();
		if (!dc::cfg::ParseU32OrKeep("X.U32", "bad", u32_value, false, &err, &warn)) {
			return false;
		}
		if (u32_value != 11u || warn.empty()) {
			return false;
		}
		err.clear();
		warn.clear();
		if (dc::cfg::ParseU32OrKeep("X.U32", "bad", u32_value, true, &err, &warn)) {
			return false;
		}
		if (err.empty()) {
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
	const bool ok_flush = TestFlushOneCharVersionFields();
	const bool ok_dirty = TestFlushDirtyCharsConflictFields();
	const bool ok_aoi_sanitize = TestAoiSanitizeEntityIds();
	const bool ok_aoi_batch = TestAoiBatchHelpers();
	const bool ok_aoi_move_recipients = TestAoiMoveEnteredExitedRecipients();
	const bool ok_config_parse_helpers = TestConfigParseHelpers();

	if (!ok_session || !ok_batch || !ok_despawn_batch || !ok_aoi_one_cell || !ok_flush || !ok_dirty || !ok_aoi_sanitize || !ok_aoi_batch || !ok_aoi_move_recipients || !ok_config_parse_helpers) {
		std::cerr
			<< "world_regression_tests failed:"
			<< " session=" << ok_session
			<< " batch=" << ok_batch
			<< " despawn_batch=" << ok_despawn_batch
			<< " aoi_one_cell=" << ok_aoi_one_cell
			<< " flush=" << ok_flush
			<< " dirty=" << ok_dirty
			<< " aoi_sanitize=" << ok_aoi_sanitize
			<< " aoi_batch=" << ok_aoi_batch
			<< " aoi_move_recipients=" << ok_aoi_move_recipients
			<< " config_parse_helpers=" << ok_config_parse_helpers
			<< "\n";
		return 1;
	}

	std::cout << "world_regression_tests passed\n";
	return 0;
}
