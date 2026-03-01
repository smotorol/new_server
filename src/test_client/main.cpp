#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <memory>
#include <chrono>

#include "../net/tcp_client.h"
#include "../net/tcp_session.h"
#include "../net/msg_header.h"
#include "../net/net_handler.h"
#include "../net/actor_system.h"

#include "../proto/packet_util.h"
#include "../proto/proto.h"
#include "../common/common.h"

#include "networkex.h"  // ✅ test_client 전용 NetworkEX 사용
#include "GlobalClients.h"

namespace asio = boost::asio;

struct BenchConn {
	std::shared_ptr<CNetworkEX> handler;
	std::shared_ptr<net::TcpClient> client;
};

static std::vector<BenchConn> spawn_bench_clients(asio::io_context& io, net::ActorSystem& actors,
	const std::string& host, std::uint16_t port, int count)
{
	std::vector<BenchConn> out;
	out.reserve((std::size_t)std::max(0, count));
	for (int i = 0; i < count; ++i) {
		BenchConn c;
		c.handler = std::make_shared<CNetworkEX>((std::uint32_t)eLine::sample_server);
		c.client = std::make_shared<net::TcpClient>(io, c.handler);
		c.handler->AttachClient(c.client);
		c.handler->AttachDispatcher([&actors](std::uint64_t actor_id, std::function<void()> fn) {
			actors.post(actor_id, std::move(fn));
		});
		c.client->start(host, port);
		out.push_back(std::move(c));
	}
	return out;
}

static _MSG_HEADER make_header(std::uint16_t type, std::uint16_t total_size)
{
	_MSG_HEADER h{};
	h.m_wSize = total_size;
	h.m_byType[0] = static_cast<std::uint8_t>(type & 0xFF);
	h.m_byType[1] = static_cast<std::uint8_t>((type >> 8) & 0xFF);
	return h;
}

int main()
{
	asio::io_context& io = GlobalIo();
	LineManager& mgr = GlobalLineClients();

	// 라인별로 설정 + 자동 start
	mgr.setup((std::uint8_t)eLine::sample_server, "127.0.0.1", 27787, true);

	std::thread net_thread([&] { io.run(); });

	std::cout << "Commands:\n"
		<< "  send <type_u16> <text>\n"
		<< "  gold <amount_u32>   (send C2S_add_gold)\n"
		<< "  stats               (C2S_get_stats)\n"
		<< "  heal [amount_u32]   (0이면 full heal)\n"
		<< "  spawn_monster [template_id_u32]\n"
		<< "  atk_monster <monster_id_u64>\n"
		<< "  atk_player <target_char_id_u64>\n"
		<< "  myid                (print bound char_id)\n"
		<< "  bench_multi <conns> <msgs_per_conn> <work_us>\n"
		<< "  bench_same  <conns> <total_msgs_per_conn> <work_us>   (모든 세션이 첫번째 Actor로 forward)\n"
		<< "  quit\n";

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (line == "quit") break;

		if (line.rfind("send ", 0) == 0)
		{
			// parse: send <type> <text>
			const auto p1 = line.find(' ');
			const auto p2 = line.find(' ', p1 + 1);
			if (p2 == std::string::npos) {
				std::cout << "usage: send <type_u16> <text>\n";
				continue;

			}

			const auto type_str = line.substr(p1 + 1, p2 - (p1 + 1));
			const auto text = line.substr(p2 + 1);
			const std::uint16_t type = static_cast<std::uint16_t>(std::stoi(type_str));

			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }

			auto s = client->session();
			if (!s) {
				std::cout << "not connected yet\n";
				continue;

			}

			const std::uint16_t total =
				static_cast<std::uint16_t>(MSG_HEADER_SIZE + text.size());

			auto h = make_header(type, total);
			s->async_send(h, text.empty() ? nullptr : text.data());
			continue;
		}

		// gold <amount>
		if (line.rfind("gold ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			std::uint32_t amount = 0;
			iss >> cmd >> amount;

			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }

			auto s = client->session();
			if (!s) { std::cout << "not connected yet\n"; continue; }

			proto::C2S_add_gold req{};
			req.add = amount;

			auto h = proto::make_header(
				(std::uint16_t)proto::C2SMsg::add_gold,
				(std::uint16_t)sizeof(req));

			s->async_send(h, reinterpret_cast<const char*>(&req));
			std::cout << "[client] sent add_gold=" << amount << "\n";
			continue;
		}

		// myid
		if (line == "myid")
		{
			auto h = mgr.handler((std::uint8_t)eLine::sample_server);
			if (!h) { std::cout << "handler not ready\n"; continue; }
			std::cout << "[client] my char_id(actor_id)=" << h->actor_id() << "\n";
			continue;
		}

		// stats
		if (line == "stats")
		{
			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }
			auto s = client->session();
			if (!s) { std::cout << "not connected yet\n"; continue; }

			proto::C2S_get_stats req{};
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::get_stats,
				(std::uint16_t)sizeof(req));
			s->async_send(h, reinterpret_cast<const char*>(&req));
			continue;
		}

		// heal [amount]
		if (line.rfind("heal", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			std::uint32_t amount = 0;
			iss >> cmd;
			if (!(iss >> amount)) amount = 0;

			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }
			auto s = client->session();
			if (!s) { std::cout << "not connected yet\n"; continue; }

			proto::C2S_heal_self req{};
			req.amount = amount;
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::heal_self,
				(std::uint16_t)sizeof(req));
			s->async_send(h, reinterpret_cast<const char*>(&req));
			continue;
		}

		// spawn_monster [template_id]
		if (line.rfind("spawn_monster", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			std::uint32_t tid = 0;
			iss >> cmd;
			if (!(iss >> tid)) tid = 0;

			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }
			auto s = client->session();
			if (!s) { std::cout << "not connected yet\n"; continue; }

			proto::C2S_spawn_monster req{};
			req.template_id = tid;
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::spawn_monster,
				(std::uint16_t)sizeof(req));
			s->async_send(h, reinterpret_cast<const char*>(&req));
			continue;
		}

		// atk_monster <monster_id>
		if (line.rfind("atk_monster ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			std::uint64_t mid = 0;
			iss >> cmd >> mid;

			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }
			auto s = client->session();
			if (!s) { std::cout << "not connected yet\n"; continue; }

			proto::C2S_attack_monster req{};
			req.monster_id = mid;
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::attack_monster,
				(std::uint16_t)sizeof(req));
			s->async_send(h, reinterpret_cast<const char*>(&req));
			continue;
		}

		// atk_player <target_char_id>
		if (line.rfind("atk_player ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			std::uint64_t target = 0;
			iss >> cmd >> target;

			auto client = mgr.client((std::uint8_t)eLine::sample_server);
			if (!client) { std::cout << "client not setup\n"; continue; }
			auto s = client->session();
			if (!s) { std::cout << "not connected yet\n"; continue; }

			proto::C2S_attack_player req{};
			req.target_char_id = target;
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::attack_player,
				(std::uint16_t)sizeof(req));
			s->async_send(h, reinterpret_cast<const char*>(&req));
			continue;
		}

		// bench_multi <conns> <msgs_per_conn> <work_us>
		if (line.rfind("bench_multi ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int conns = 0;
			int msgs = 0;
			int work_us = 0;
			iss >> cmd >> conns >> msgs >> work_us;
			if (conns <= 0 || msgs <= 0) { std::cout << "usage: bench_multi <conns> <msgs_per_conn> <work_us>\n"; continue; }

			net::ActorSystem actors;
			actors.start(1);

			auto bench = spawn_bench_clients(io, actors, "127.0.0.1", 27787, conns);

			// wait ready
			for (auto& c : bench) c.handler->wait_ready();

			std::cout << "[bench_multi] start conns=" << conns << " msgs=" << msgs << " work_us=" << work_us << "\n";

			for (auto& c : bench)
			{
				auto s = c.client->session();
				if (!s) continue;

				for (int i = 1; i <= msgs; ++i)
				{
					proto::C2S_actor_seq_test req{};
					req.seq = (proto::u32)i;
					req.work_us = (proto::u32)work_us;

					auto h = proto::make_header((std::uint16_t)proto::C2SMsg::actor_seq_test,
						(std::uint16_t)sizeof(req));
					s->async_send(h, reinterpret_cast<const char*>(&req));
				}
			}

			std::cout << "[bench_multi] sent. watch server logs + client acks.\n";
			continue;
		}

		// bench_same <conns> <msgs_per_conn> <work_us>
		if (line.rfind("bench_same ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int conns = 0;
			int msgs = 0;
			int work_us = 0;
			iss >> cmd >> conns >> msgs >> work_us;
			if (conns <= 0 || msgs <= 0) { std::cout << "usage: bench_same <conns> <msgs_per_conn> <work_us>\n"; continue; }

			net::ActorSystem actors;
			actors.start(1);

			auto bench = spawn_bench_clients(io, actors, "127.0.0.1", 27787, conns);
			for (auto& c : bench) c.handler->wait_ready();

			const std::uint64_t target = bench.front().handler->actor_id();
			if (target == 0) { std::cout << "target actor_id is 0 (not ready?)\n"; continue; }

			std::cout << "[bench_same] start conns=" << conns << " msgs_per_conn=" << msgs
				<< " work_us=" << work_us << " target_actor=" << target << "\n";

			proto::u32 tag = 1;
			for (auto& c : bench)
			{
				auto s = c.client->session();
				if (!s) continue;

				for (int i = 0; i < msgs; ++i)
				{
					proto::C2S_actor_forward req{};
					req.target_actor_id = target;
					req.work_us = (proto::u32)work_us;
					req.tag = tag++;

					auto h = proto::make_header((std::uint16_t)proto::C2SMsg::actor_forward,
						(std::uint16_t)sizeof(req));
					s->async_send(h, reinterpret_cast<const char*>(&req));
				}
			}

			std::cout << "[bench_same] sent. 서버에서 busy 재진입이 없으면 errors가 0 유지.\n";
			continue;
		}

		std::cout << "unknown command\n";
	}

	io.stop();
	if (net_thread.joinable()) net_thread.join();
	return 0;
}
