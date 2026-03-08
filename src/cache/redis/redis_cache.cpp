#include "redis_cache.h"

#include <sstream>
#include <string_view>
#include <unordered_set>

namespace cache {

	static sw::redis::ConnectionOptions make_options(const RedisCache::Config& cfg)
	{
		sw::redis::ConnectionOptions opt;
		opt.host = cfg.host;
		opt.port = cfg.port;
		opt.db = cfg.db;
		if (!cfg.password.empty()) opt.password = cfg.password;
		return opt;
	}

	RedisCache::RedisCache(const Config& cfg)
		: redis_(make_options(cfg))
		, prefix_(cfg.prefix)
		, shard_count_(cfg.shard_count ? cfg.shard_count : 1)
		, wait_replicas_(cfg.wait_replicas)
		, wait_timeout_ms_(cfg.wait_timeout_ms)
	{}

	std::string RedisCache::char_key_(std::uint32_t world_code, std::uint64_t char_id) const
	{
		std::ostringstream os;
		os << prefix_ << ":char:" << world_code << ":" << char_id;
		return os.str();
	}

	std::uint32_t RedisCache::shard_of_(std::uint64_t char_id) const
	{
		return (std::uint32_t)(char_id % shard_count_);
	}

	std::string RedisCache::dirty_key_(std::uint32_t world_code, std::uint32_t shard_id) const
	{
		std::ostringstream os;
		os << prefix_ << ":dirty:chars:" << world_code;
		return os.str();
	}

	void RedisCache::upsert_character(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob, int ttl_seconds)
	{
		std::lock_guard<std::mutex> lk(mtx_);
		const auto key = char_key_(world_code, char_id);
		const auto shard = shard_of_(char_id);

		redis_.hset(key, "blob", blob);
		if (ttl_seconds > 0) redis_.expire(key, ttl_seconds);

		redis_.sadd(dirty_key_(world_code, shard), std::to_string(char_id));

		// ✅ 선택: replica 반영 확인(WAIT)
		if (wait_replicas_ > 0 && wait_timeout_ms_ > 0) {
			// WAIT <replicas> <timeout-ms>
			std::vector<std::string> cmd;
			redis_.command<long long>("WAIT", wait_replicas_, wait_timeout_ms_);

		}
	}

	void RedisCache::mark_dirty(std::uint32_t world_code, std::uint64_t char_id)
	{
		std::lock_guard<std::mutex> lk(mtx_);
		const auto shard = shard_of_(char_id);
		redis_.sadd(dirty_key_(world_code, shard), std::to_string(char_id));
	}

	std::vector<std::uint64_t> RedisCache::take_dirty_batch(std::uint32_t world_code, std::uint32_t shard_id, std::size_t max_batch)
	{
		std::vector<std::uint64_t> out;
		out.reserve(max_batch);

		std::lock_guard<std::mutex> lk(mtx_);

		std::unordered_set<std::string> members;
		redis_.smembers(dirty_key_(world_code, shard_id), std::inserter(members, members.end()));

		std::size_t count = 0;
		for (const auto& s : members) {
			if (count >= max_batch) break;
			try {
				const auto id = (std::uint64_t)std::stoull(s);
				out.push_back(id);
				redis_.srem(dirty_key_(world_code, shard_id), s);
				++count;

			}
			catch (...) {
				redis_.srem(dirty_key_(world_code, shard_id), s);

			}

		}
		return out;
	}

	std::optional<std::string> RedisCache::get_character_blob(std::uint32_t world_code, std::uint64_t char_id)
	{
		std::lock_guard<std::mutex> lk(mtx_);
		const auto key = char_key_(world_code, char_id);

		auto v = redis_.hget(key, "blob");
		if (!v) return std::nullopt;
		return *v;
	}

	void RedisCache::remove_dirty(std::uint32_t world_code, std::uint64_t char_id)
	{
		std::lock_guard<std::mutex> lk(mtx_);
		const auto shard = shard_of_(char_id);
		redis_.srem(dirty_key_(world_code, shard), std::to_string(char_id));
	}


} // namespace cache
