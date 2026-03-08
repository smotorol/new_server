#pragma once
#include <cstdint>
#include <string>
#include <cstddef>
#include <vector>
#include <mutex>
#include <optional>

#include <sw/redis++/redis++.h>

// Redis write-behind 캐시 (A안):
// - 캐릭터 상태는 Redis(Hash)에 저장
// - 변경 발생 시 Dirty Set에 char_id를 추가
// - 주기적으로(예: 60초) Dirty Set을 읽어 DB에 flush
// - 서버 다운 시에도 Redis에 남아있으므로 재기동 후 Dirty Set 기준으로 DB 복구 가능
namespace cache {

	class RedisCache final {
	public:
		struct Config {
			std::string host = "127.0.0.1";
			int port = 6379;
			int db = 0;
			std::string password;   // empty면 no-auth
			std::string prefix = "dc"; // key prefix (환경 분리)
			
			// ✅ char_id 샤딩(= dirty set shard 분리용)
			std::uint32_t shard_count = 1;

			// ✅ "DB급" 운영 옵션(원하면 복제 확인)
			int wait_replicas = 0;        // 0이면 WAIT 안 함
			int wait_timeout_ms = 0;      // WAIT timeout
		};

	public:
		explicit RedisCache(const Config& cfg);

		// 캐릭터 상태(바이너리/JSON/flatbuffer 등)를 저장하고 dirty 마킹
		// key: {prefix}:char:{world}:{char_id}
		// dirty set(sharded): {prefix}:dirty:chars:{world}:{shard}
		void upsert_character(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob, int ttl_seconds = 0);

		// 로그아웃 즉시 flush 용: dirty에 넣고 바로 flush_one에서 처리해도 됨
		void mark_dirty(std::uint32_t world_code, std::uint64_t char_id);

		// dirty set에서 현재 멤버를 가져온다(샘플 구현: SMEMBERS 기반)
		// ✅ shard별 dirty set에서 배치 pop
		std::vector<std::uint64_t> take_dirty_batch(std::uint32_t world_code, std::uint32_t shard_id, std::size_t max_batch);

		// 캐릭터 blob 읽기
		std::optional<std::string> get_character_blob(std::uint32_t world_code, std::uint64_t char_id);

		// flush 성공 후 dirty set에서 제거(샘플은 take_dirty_batch가 이미 제거함)
		void remove_dirty(std::uint32_t world_code, std::uint64_t char_id);

	private:
		std::string char_key_(std::uint32_t world_code, std::uint64_t char_id) const;
		std::string dirty_key_(std::uint32_t world_code, std::uint32_t shard_id) const;
		std::uint32_t shard_of_(std::uint64_t char_id) const;

	private:
		sw::redis::Redis redis_;
		std::string prefix_;
		std::uint32_t shard_count_ = 1;
		int wait_replicas_ = 0;
		int wait_timeout_ms_ = 0;
		mutable std::mutex mtx_;

	};
} // namespace cache
