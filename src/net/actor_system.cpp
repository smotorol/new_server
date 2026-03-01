#include "actor_system.h"
#include <chrono>

namespace net {

	namespace {
		thread_local int tls_shard_index = -1;
	}

	int ActorSystem::current_shard_index() noexcept { return tls_shard_index; }

	std::size_t ActorSystem::pick_shard_(ActorId id) const noexcept
	{
		const std::size_t n = shards_.empty() ? 1 : shards_.size();
		// 간단한 hash: id xor shift
		const std::uint64_t x = id ^ (id >> 33) ^ (id << 11);
		return (std::size_t)(x % n);
	}

	void ActorSystem::worker_loop_(std::stop_token st, Shard* shard, std::atomic<bool>* running, int shard_index)
	{
		tls_shard_index = shard_index;
		while (!st.stop_requested()) {
			std::deque<Fn> local;
			{
				std::unique_lock lk(shard->mtx);
				shard->cv.wait_for(lk, std::chrono::milliseconds(10), [&] {
					return st.stop_requested() || !running->load(std::memory_order_relaxed) || !shard->q.empty();
				});

				if (st.stop_requested()) break;
				local.swap(shard->q);
			}

			for (auto& fn : local) {
				if (!fn) continue;
				try { fn(); }
				catch (...) {
					// 여기서는 삼킨다(서버는 계속 살아야 함). 로깅은 상위에서 하도록.
				}
			}
		}
	}

	void ActorSystem::start(std::uint32_t thread_count)
	{
		stop();

		const std::uint32_t n = std::max<std::uint32_t>(1, thread_count);
		shards_.clear();
		shards_.reserve(n);
		for (std::uint32_t i = 0; i < n; ++i) {
			shards_.push_back(std::make_unique<Shard>());
		}

		running_.store(true, std::memory_order_relaxed);

		for (std::size_t i = 0; i < shards_.size(); ++i) {
			Shard* s = shards_[i].get();
			s->th = std::jthread([this, s, shard_index = (int)i](std::stop_token st) {
				worker_loop_(st, s, &running_, shard_index);
			});
		}
	}

	void ActorSystem::stop()
	{
		if (!running_.exchange(false)) {
			// 이미 stopped거나 start 안 된 상태
			shards_.clear();
			return;
		}

		for (auto& up : shards_) {
			up->cv.notify_all();
		}
		// jthread는 소멸 시 자동 join + stop_request
		shards_.clear();
	}

	void ActorSystem::post(ActorId id, Fn fn)
	{
		if (!fn) return;

		// start 전이어도 최소 동작(싱글 스레드처럼)하도록 방어
		if (shards_.empty()) {
			fn();
			return;
		}

		Shard& s = *shards_[pick_shard_(id)];
		{
			std::lock_guard lk(s.mtx);
			s.q.push_back(std::move(fn));
		}
		s.cv.notify_one();
	}

} // namespace svr
