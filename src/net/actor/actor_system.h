#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <utility>
#include <algorithm>
#include <memory>
#include <unordered_map>

namespace net {

	// ✅ Actor 인스턴스(상태) 베이스
	// - 서버에서는 (PlayerActor/WorldActor 등) 이 타입을 상속해서 "Actor가 상태를 소유"하도록 만든다.
	// - ActorSystem은 shard-local map<ActorId, unique_ptr<IActor>> 를 유지한다.
	struct IActor {
		virtual ~IActor() = default;
	};

	// ✅ Actor 모델(실전 최소 구현)
	// - ActorId(key)별로 메시지(fn)를 보낸다.
	// - key는 hash되어 N개의 shard(worker thread) 중 하나로 라우팅.
	// - 같은 key로 들어온 메시지는 항상 같은 shard에서 순서대로 실행된다.
	//   => "Actor = 상태를 독점하는 실행 단위"의 핵심(순서성/단일 실행)을 만족.
	//
	// ⚠️ 주의: 이 구현은 "함수 메시지(fn)" 기반의 lightweight actor executor.
	//         상태는 (key별로) 해당 shard에서만 접근하도록 설계해야 락이 필요 없다.
	class ActorSystem final
	{
	public:
		using ActorId = std::uint64_t;
		using Fn = std::function<void()>;
		using ActorFn = std::function<void(IActor&)>;
		using ActorFactory = std::function<std::unique_ptr<IActor>(ActorId)>;

		ActorSystem() = default;
		explicit ActorSystem(std::uint32_t thread_count) { start(thread_count); }
		~ActorSystem() { stop(); }

		ActorSystem(const ActorSystem&) = delete;
		ActorSystem& operator=(const ActorSystem&) = delete;

		void start(std::uint32_t thread_count);
		void stop();

		bool running() const noexcept { return running_.load(std::memory_order_relaxed); }
		std::uint32_t shard_count() const noexcept { return (std::uint32_t)shards_.size(); }

		// ✅ Actor에게 메시지 보내기
		// 같은 id는 항상 같은 shard로 라우팅되고, 그 shard에서 순서대로 실행된다.
		void post(ActorId id, Fn fn);

		// ✅ Actor 인스턴스(상태)를 보장하면서 메시지 보내기
		// - 해당 shard-local map에 (id -> IActor) 를 유지
		// - actor가 없으면 factory로 생성
		void post_actor(ActorId id, ActorFactory factory, ActorFn fn);

		// ✅ (Actor 워커 스레드에서만) 현재 shard에서 actor를 얻거나 생성
		IActor* try_get_local(ActorId id) noexcept;
		IActor& get_or_create_local(ActorId id, ActorFactory factory);

		// ✅ Actor 제거(로그아웃 등)
		void erase_actor(ActorId id);

		// ✅ 관리/전역 작업용: shard 0으로 보냄
		void post_global(Fn fn) { post(0, std::move(fn)); }

		// ✅ 디버그: 현재 실행 중인 Actor shard index(-1이면 Actor 워커가 아님)
		static int current_shard_index() noexcept;

	private:
		struct Shard
		{
			std::mutex mtx;
			std::condition_variable cv;
			std::deque<Fn> q;
			std::jthread th;

			// ✅ shard-local actor storage
			std::unordered_map<ActorId, std::unique_ptr<IActor>> actors;
		};

	private:
		std::atomic<bool> running_{ false };
		// ✅ start/stop/post 동시성 보호
		// - bench 종료/소멸 시점에 네트워크 스레드가 post()를 호출하면서
		//   shards_가 clear되는 레이스(UB/프리징)를 막는다.
		mutable std::mutex shards_mtx_;
		std::vector<std::unique_ptr<Shard>> shards_;

		std::size_t pick_shard_(ActorId id) const noexcept;
		static void worker_loop_(std::stop_token st, Shard* shard, std::atomic<bool>* running, int shard_index);
	};

} // namespace svr
