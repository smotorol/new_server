#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <memory>
#include <chrono>

#include "net/tcp/tcp_client.h"
#include "net/tcp/tcp_session.h"
#include "net/packet/msg_header.h"
#include "net/handler/net_handler.h"
#include "net/actor/actor_system.h"

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/client/login_proto.h"
#include "proto/client/world_proto.h"
#include "core/log/common.h"

#include "networkex.h"  // ✅ test_client 전용 NetworkEX 사용
#include "GlobalClients.h"
#include "bench_controller.h"

namespace asio = boost::asio;
namespace pt_login = proto::login;
namespace pt_world = proto::world;

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
		c.handler = std::make_shared<CNetworkEX>((std::uint32_t)eLine::world_server);
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
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif
	asio::io_context& io = GlobalIo();
	LineManager& mgr = GlobalLineClients();

	// 라인별로 설정 + 자동 start
	mgr.setup((std::uint8_t)eLine::login_server, "127.0.0.1", 26788, true);

	std::thread net_thread([&] { io.run(); });

	std::cout << "Commands:\n"
		<< "  login <id> <pw> [char_id]\n"
		<< "  status\n"
		<< "  enterworld\n"
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
		<< "  bench_walk  <conns> <seconds> <moves_per_sec> <radius> [work_us]\n"
		<< "    * bench_multi/bench_same은 actor_bound 필요(로그인/enterworld 선행)\n"
		<< "\n"
		<< "  // ✅ 동접 셋업 + 부하 측정(2단계)\n"
		<< "  bench_setup <conns> [host] [port]          (연결만 미리 생성)\n"
		<< "  bench_teardown                            (연결/스레드 정리)\n"
		<< "  bench_walk_start <moves_per_sec> <radius> [work_us]  (자동 이동 시작, 무한)\n"
		<< "  bench_walk_stop                            (자동 이동 중지)\n"
		<< "  bench_reset                                (bench 카운터 리셋)\n"
		<< "  bench_measure <seconds>                    (현재 상태에서 seconds 동안 측정 후 요약 출력)\n"
		<< "  quit\n";

	BenchController bench_ctl(io);

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (line == "quit") break;

		if (line.rfind("login ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			std::string id;
			std::string pw;
			std::uint64_t selected_char_id = 0;
			iss >> cmd >> id >> pw;
			if (!(iss >> selected_char_id)) {
				selected_char_id = 0;
			}

			auto client = mgr.client((std::uint8_t)eLine::login_server);
			if (!client) { std::cout << "login client not setup\n"; continue; }

			auto s = client->session();
			if (!s) { std::cout << "login server not connected yet\n"; continue; }

			auto hnd = mgr.handler((std::uint8_t)eLine::login_server);
			if (hnd) {
				hnd->clear_login_result();
			}

			pt_login::C2S_login_request req{};
			std::snprintf(req.login_id, sizeof(req.login_id), "%s", id.c_str());
			std::snprintf(req.password, sizeof(req.password), "%s", pw.c_str());
			req.selected_char_id = selected_char_id;

			auto h = proto::make_header(
				static_cast<std::uint16_t>(pt_login::LoginC2SMsg::login_request),
				static_cast<std::uint16_t>(sizeof(req)));

			s->async_send(h, reinterpret_cast<const char*>(&req));
			std::cout << "[Login] sent login_request id=" << id
				<< " selected_char_id=" << selected_char_id << "\n";
			continue;
		}

		if (line == "status")
		{
			auto h = mgr.handler((std::uint8_t)eLine::login_server);
			if (!h || !h->has_login_result()) {
				std::cout << "[client] no login_result yet\n";
				continue;
			}

			const auto st = h->login_result();
			std::cout
				<< "[client] login_result"
				<< " ok=" << st.ok
				<< " account_id=" << st.account_id
				<< " char_id=" << st.char_id
				<< " world_host=" << st.world_host
				<< " world_port=" << st.world_port
				<< " login_session=" << st.login_session
				<< " world_token=" << st.world_token
				<< "\n";
			continue;
		}

		if (line == "enterworld")
		{
			auto login_h = mgr.handler((std::uint8_t)eLine::login_server);
			if (!login_h || !login_h->has_login_result()) {
				std::cout << "login_result not ready\n";
				continue;
			}

			const auto st = login_h->login_result();
			if (!st.ok) {
				std::cout << "last login failed\n";
				continue;
			}

			if (st.world_host.empty() || st.world_port == 0) {
				std::cout << "invalid world endpoint from login_result\n";
				continue;
			}

			// login 서버와는 연결 종료 후 world 서버로 이동
			if (auto login_client = mgr.client((std::uint8_t)eLine::login_server)) {
				login_client->stop();
			}

			mgr.setup((std::uint8_t)eLine::world_server, st.world_host, st.world_port, true);
			std::this_thread::sleep_for(std::chrono::milliseconds(300));

			auto world_client = mgr.client((std::uint8_t)eLine::world_server);
			if (!world_client) {
				std::cout << "world client setup failed\n";
				continue;
			}

			auto s = world_client->session();
			if (!s) {
				std::cout << "world server not connected yet\n";
				continue;
			}

			pt_world::C2S_enter_world_with_token req{};
			req.account_id = st.account_id;
			req.char_id = st.char_id;
			std::snprintf(req.login_session, sizeof(req.login_session), "%s", st.login_session.c_str());
			std::snprintf(req.world_token, sizeof(req.world_token), "%s", st.world_token.c_str());

			auto h = proto::make_header(
				static_cast<std::uint16_t>(pt_world::WorldC2SMsg::enter_world_with_token),
				static_cast<std::uint16_t>(sizeof(req)));

			s->async_send(h, reinterpret_cast<const char*>(&req));

			std::cout << "[World] sent enter_world_with_token"
				<< " account_id=" << st.account_id
				<< " char_id=" << st.char_id
				<< "\n";
			continue;
		}

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

			auto client = mgr.client((std::uint8_t)eLine::world_server);
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

			auto client = mgr.client((std::uint8_t)eLine::world_server);
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
			auto h = mgr.handler((std::uint8_t)eLine::world_server);
			if (!h) { std::cout << "handler not ready\n"; continue; }
			std::cout << "[client] my char_id(actor_id)=" << h->actor_id() << "\n";
			continue;
		}

		// stats
		if (line == "stats")
		{
			auto client = mgr.client((std::uint8_t)eLine::world_server);
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

			auto client = mgr.client((std::uint8_t)eLine::world_server);
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

		// move [x] [y]
		if (line.rfind("move", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int x = 0;
			int y = 0;
			iss >> cmd >> x >> y;

			auto client = mgr.client((std::uint8_t)eLine::world_server);
			if (!client) { std::cout << "client not setup"; continue; }
			auto s = client->session();
			if (!s) { std::cout << "not connected yet"; continue; }

			proto::C2S_move req{};
			req.x = x;
			req.y = y;
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::move,
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

			auto client = mgr.client((std::uint8_t)eLine::world_server);
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

			auto client = mgr.client((std::uint8_t)eLine::world_server);
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

			auto client = mgr.client((std::uint8_t)eLine::world_server);
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
			{
				const int hw = (int)std::thread::hardware_concurrency();
				const std::uint32_t actor_threads = (std::uint32_t)std::max(1, std::min(conns, std::max(1, hw)));
				actors.start(actor_threads);
			}

			auto bench = spawn_bench_clients(io, actors, "127.0.0.1", 27787, conns);

			// enterworld/auth 없이 actor_bound는 오지 않으므로 ready 대기는 timeout + 안내 후 즉시 실패
			constexpr auto kReadyTimeout = std::chrono::seconds(3);
			bool all_ready = true;
			for (auto& c : bench) {
				if (!c.handler->wait_ready_for(kReadyTimeout)) {
					all_ready = false;
					break;
				}
			}
			if (!all_ready) {
				std::cout << "[bench_multi] failed: world actor not bound within "
					<< kReadyTimeout.count()
					<< "s. run login + enterworld first.\n";
				for (auto& c : bench) {
					if (auto s = c.client->session()) s->close();
				}
				actors.stop();
				continue;
			}

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
			{
				const int hw = (int)std::thread::hardware_concurrency();
				const std::uint32_t actor_threads = (std::uint32_t)std::max(1, std::min(conns, std::max(1, hw)));
				actors.start(actor_threads);
			};

			auto bench = spawn_bench_clients(io, actors, "127.0.0.1", 27787, conns);
			constexpr auto kReadyTimeout = std::chrono::seconds(3);
			bool all_ready = true;
			for (auto& c : bench) {
				if (!c.handler->wait_ready_for(kReadyTimeout)) {
					all_ready = false;
					break;
				}
			}
			if (!all_ready) {
				std::cout << "[bench_same] failed: world actor not bound within "
					<< kReadyTimeout.count()
					<< "s. run login + enterworld first.\n";
				for (auto& c : bench) {
					if (auto s = c.client->session()) s->close();
				}
				actors.stop();
				continue;
			}

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

		// bench_walk <conns> <seconds> <moves_per_sec> <radius> [work_us]
		// - 각 커넥션이 bench_move를 일정 주기로 보내서 이동 + ACK RTT + 브로드캐스트 처리량 측정
		if (line.rfind("bench_walk ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int conns = 0;
			int seconds = 0;
			int mps = 0;
			int radius = 0;
			int work_us = 0;
			iss >> cmd >> conns >> seconds >> mps >> radius;
			if (!(iss >> work_us)) work_us = 0;
			if (conns <= 0 || seconds <= 0 || mps <= 0 || radius < 0) {
				std::cout << "usage: bench_walk <conns> <seconds> <moves_per_sec> <radius> [work_us]\n";
				continue;
			}

			net::ActorSystem actors;
			{
				const int hw = (int)std::thread::hardware_concurrency();
				const std::uint32_t actor_threads = (std::uint32_t)std::max(1, std::min(conns, std::max(1, hw)));
				actors.start(actor_threads);
			}

			auto bench = spawn_bench_clients(io, actors, "127.0.0.1", 27787, conns);
			constexpr auto kConnectTimeout = std::chrono::seconds(3);
			bool all_connected = true;
			for (auto& c : bench) {
				if (!c.handler->wait_connected_for(kConnectTimeout)) {
					all_connected = false;
					break;
				}
			}
			if (!all_connected) {
				std::cout << "[bench_walk] failed: world connect timeout within "
					<< kConnectTimeout.count()
					<< "s.\n";
				for (auto& c : bench) {
					if (auto s = c.client->session()) s->close();
				}
				actors.stop();
				continue;
			}

			// noisy 출력 끄고, 카운터 초기화
			CNetworkEX::SetBenchQuiet(true);
			for (auto& c : bench) c.handler->BenchReset();

			// 좌표 배치: 모든 클라가 AOI 안에 들어오도록 작은 박스 안에 촘촘히 배치
			const int side = std::max(1, (radius * 2 + 1));
			auto coord_of = [&](int idx) {
				const int x = (idx % side) - radius;
				const int y = ((idx / side) % side) - radius;
				return std::pair<int, int>{ x, y };
			};

			const int steps = seconds * mps;
			const auto interval = std::chrono::microseconds(1'000'000 / std::max(1, mps));
			std::uint32_t seq = 1;
			std::uint64_t sent = 0;

			std::cout << "[bench_walk] start conns=" << conns
				<< " seconds=" << seconds
				<< " moves_per_sec=" << mps
				<< " radius=" << radius
				<< " work_us=" << work_us
				<< " (total_send_target=" << (std::uint64_t)conns * (std::uint64_t)steps << ")\n";

			const auto t0 = std::chrono::steady_clock::now();
			for (int step = 0; step < steps; ++step)
			{
				const int toggle = (step & 1);
				for (int i = 0; i < conns; ++i)
				{
					auto s = bench[(std::size_t)i].client->session();
					if (!s) continue;

					const auto [bx, by] = coord_of(i);
					proto::C2S_bench_move req{};
					req.seq = seq++;
					req.work_us = (proto::u32)work_us;
					req.x = bx + toggle;
					req.y = by;
					const auto now = std::chrono::steady_clock::now().time_since_epoch();
					req.client_ts_ns = (proto::u64)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

					auto h = proto::make_header((std::uint16_t)proto::C2SMsg::bench_move,
						(std::uint16_t)sizeof(req));
					s->async_send(h, reinterpret_cast<const char*>(&req));
					++sent;
				}
				//std::this_thread::sleep_for(interval);
			}
			const auto t1 = std::chrono::steady_clock::now();
			const double elapsed = std::chrono::duration<double>(t1 - t0).count();

			// ack 수집 대기(최대 5초)
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			std::uint64_t ack_total = 0;
			while (std::chrono::steady_clock::now() < deadline)
			{
				ack_total = 0;
				for (auto& c : bench) ack_total += c.handler->BenchGetSnapshot().recv_ack;
				if (ack_total >= sent) break;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			const auto t_done = std::chrono::steady_clock::now();
			const double total_elapsed = std::chrono::duration<double>(t_done - t0).count();
			const double wait_elapsed = std::chrono::duration<double>(t_done - t1).count();

			// 집계
			std::uint64_t move_pkts = 0, move_items = 0;
			std::uint64_t spawn_recv = 0, despawn_recv = 0;
			double rtt_sum_ms = 0.0;
			double rtt_min_ms = 0.0;
			double rtt_max_ms = 0.0;
			bool min_init = false;
			for (auto& c : bench) {
				auto s = c.handler->BenchGetSnapshot();
				move_pkts += s.recv_move_pkts;
				move_items += s.recv_move_items;
				spawn_recv += s.recv_spawn;
				despawn_recv += s.recv_despawn;
				rtt_sum_ms += s.rtt_avg_ms * (double)s.recv_ack;
				if (s.recv_ack > 0) {
					if (!min_init || s.rtt_min_ms < rtt_min_ms) rtt_min_ms = s.rtt_min_ms;
					if (!min_init || s.rtt_max_ms > rtt_max_ms) rtt_max_ms = s.rtt_max_ms;
					min_init = true;
				}
			}

			const double ack_qps = elapsed > 0.0 ? (double)ack_total / total_elapsed : 0.0;
			const double send_qps = elapsed > 0.0 ? (double)sent / elapsed : 0.0;
			const double rtt_avg_ms = ack_total > 0 ? (rtt_sum_ms / (double)ack_total) : 0.0;

			std::cout << "[bench_walk] done\n"
				<< "  elapsed_sec=" << elapsed << "\n"
				<< "  sent=" << sent << " (" << send_qps << " pkt/s)\n"
				<< "  ack=" << ack_total << " (" << ack_qps << " ack/s)\n"
				<< "  rtt_ms avg=" << rtt_avg_ms << " min=" << rtt_min_ms << " max=" << rtt_max_ms << "\n"
				<< "  zone_broadcast recv_move_pkts=" << move_pkts
				<< " recv_move_items=" << move_items
				<< " recv_spawn=" << spawn_recv
				<< " recv_despawn=" << despawn_recv
				<< "\n";

			CNetworkEX::SetBenchQuiet(false);
			continue;
		}

		// ---- 2단계 벤치(동접 셋업/워밍업 후 측정) ----
		// bench_setup <conns> [host] [port]
		if (line.rfind("bench_setup ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int conns = 0;
			std::string host = "127.0.0.1";
			int port = 27787;
			iss >> cmd >> conns;
			if (!(iss >> host)) host = "127.0.0.1";
			if (!(iss >> port)) port = 27787;
			if (conns <= 0) {
				std::cout << "usage: bench_setup <conns> [host] [port]\n";
				continue;
			}
			const bool ok = bench_ctl.Setup(conns, host, (std::uint16_t)port);
			if (!ok) {
				std::cout << "[bench_setup] failed\n";
				continue;
			}
			std::cout << "[bench_setup] ready conns=" << bench_ctl.conn_count() << " host=" << host << " port=" << port << "\n";
			continue;
		}

		if (line == "bench_teardown")
		{
			bench_ctl.Teardown();
			std::cout << "[bench_teardown] done\n";
			continue;
		}

		// bench_walk_start <moves_per_sec> <radius> [work_us]
		if (line.rfind("bench_walk_start ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int mps = 0, radius = 0, work_us = 0;
			iss >> cmd >> mps >> radius;
			if (!(iss >> work_us)) work_us = 0;
			if (mps <= 0 || radius < 0) {
				std::cout << "usage: bench_walk_start <moves_per_sec> <radius> [work_us]\n";
				continue;
			}
			if (!bench_ctl.StartWalk(mps, radius, work_us)) {
				std::cout << "[bench_walk_start] failed (did you run bench_setup?)\n";
				continue;
			}
			std::cout << "[bench_walk_start] running mps=" << mps << " radius=" << radius << " work_us=" << work_us << "\n";
			continue;
		}

		if (line == "bench_walk_stop")
		{
			bench_ctl.StopWalk();
			std::cout << "[bench_walk_stop] stopped\n";
			continue;
		}

		if (line == "bench_reset")
		{
			bench_ctl.ResetStats();
			std::cout << "[bench_reset] counters reset\n";
			continue;
		}

		// bench_measure <seconds>
		if (line.rfind("bench_measure ", 0) == 0)
		{
			std::istringstream iss(line);
			std::string cmd;
			int seconds = 0;
			iss >> cmd >> seconds;
			if (seconds <= 0) {
				std::cout << "usage: bench_measure <seconds>\n";
				continue;
			}

			const auto t0 = std::chrono::steady_clock::now();
			auto a = bench_ctl.MeasureFor(seconds);
			const auto t1 = std::chrono::steady_clock::now();
			const double elapsed = std::chrono::duration<double>(t1 - t0).count();

			const double send_qps = elapsed > 0.0 ? (double)a.sent / elapsed : 0.0;
			const double ack_qps = elapsed > 0.0 ? (double)a.ack / elapsed : 0.0;
			std::cout << "[bench_measure] done\n"
				<< "  elapsed_sec=" << elapsed << "\n"
				<< "  conns=" << bench_ctl.conn_count() << " walk_running=" << (bench_ctl.walk_running() ? 1 : 0) << "\n"
				<< "  sent=" << a.sent << " (" << send_qps << " pkt/s)\n"
				<< "  ack=" << a.ack << " (" << ack_qps << " ack/s)\n"
				<< "  rtt_ms avg=" << a.rtt_avg_ms << " min=" << a.rtt_min_ms << " max=" << a.rtt_max_ms << "\n"
				<< "  zone_broadcast recv_move_pkts=" << a.recv_move_pkts
				<< " recv_move_items=" << a.recv_move_items
				<< " recv_spawn=" << a.recv_spawn
				<< " recv_despawn=" << a.recv_despawn
				<< "\n";
			continue;
		}

		std::cout << "unknown command\n";
	}

	io.stop();
	if (net_thread.joinable()) net_thread.join();
	return 0;
}
