#include "db_shard_manager.h"

namespace svr::dbshard {

	DbShardManager::DbShardManager(std::uint32_t shard_count, RunOneFn run_one, RecycleFn recycle)
		: shard_count_(shard_count ? shard_count : 1)
	{
		workers_.reserve(shard_count_);
		for (std::uint32_t i = 0; i < shard_count_; ++i) {
			workers_.push_back(std::make_unique<DbWorker>(i, run_one, recycle));

		}
	}

	DbShardManager::~DbShardManager() {
		stop();

	}

	void DbShardManager::start() {
		for (auto& w : workers_) w->start();

	}

	void DbShardManager::stop() {
		for (auto& w : workers_) w->stop();

	}

	void DbShardManager::push(std::uint32_t shard_id, std::uint32_t slot_index) {
		if (workers_.empty()) return;
		workers_[shard_id % shard_count_]->push(slot_index);

	}


} // namespace svr::dbshard
