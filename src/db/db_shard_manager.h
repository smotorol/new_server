#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <stop_token>

namespace svr::dbshard {

	class DbWorker final {
	public:
		using RunOneFn = std::function<void(std::uint32_t slot_index)>;
		using RecycleFn = std::function<void(std::uint32_t slot_index)>;

		DbWorker(std::uint32_t shard_id, RunOneFn run_one, RecycleFn recycle)
			: shard_id_(shard_id), run_one_(std::move(run_one)), recycle_(std::move(recycle)) {}

		DbWorker(const DbWorker&) = delete;
		DbWorker& operator=(const DbWorker&) = delete;

		void start() {
			th_ = std::jthread([this](std::stop_token st) { loop_(st); });

		}

		void stop() {
			if (th_.joinable()) {
				th_.request_stop();
				cv_.notify_all();
				th_.join();

			}

		}

		void push(std::uint32_t slot_index) {
			{
				std::lock_guard lk(mtx_);
				q_.push_back(slot_index);
			}
			cv_.notify_one();

		}

	private:
		void loop_(std::stop_token st) {
			while (!st.stop_requested()) {
				std::uint32_t idx = 0;
				{
					std::unique_lock lk(mtx_);
					cv_.wait(lk, [&] { return st.stop_requested() || !q_.empty(); });
					if (st.stop_requested()) break;
					idx = q_.front();
					q_.pop_front();
				}

				// 실제 처리
				if (run_one_) run_one_(idx);

				// 슬롯 반납(중요: slot-pool 고갈 방지)
				if (recycle_) recycle_(idx);

			}

		}

	private:
		std::uint32_t shard_id_ = 0;
		RunOneFn run_one_;
		RecycleFn recycle_;
		std::jthread th_;
		std::mutex mtx_;
		std::condition_variable cv_;
		std::deque<std::uint32_t> q_;

	};

	class DbShardManager final {
	public:
		using RunOneFn = DbWorker::RunOneFn;
		using RecycleFn = DbWorker::RecycleFn;

		DbShardManager(std::uint32_t shard_count, RunOneFn run_one, RecycleFn recycle);
		~DbShardManager();

		DbShardManager(const DbShardManager&) = delete;
		DbShardManager& operator=(const DbShardManager&) = delete;

		std::uint32_t shard_count() const noexcept { return shard_count_; }
		void start();
		void stop();
		void push(std::uint32_t shard_id, std::uint32_t slot_index);

	private:
		std::uint32_t shard_count_ = 1;
		std::vector<std::unique_ptr<DbWorker>> workers_;

	};


} // namespace svr::dbshard
