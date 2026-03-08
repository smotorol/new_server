#include <array>
#include <memory>
#include <string>
#include <boost/asio.hpp>
#include "networkex.h"

#include "net/tcp/tcp_client.h"
#include "net/actor/actor_system.h"

#include "define.h"

class LineManager
{
public:
    using HandlerPtr = std::shared_ptr<CNetworkEX>;
    using ClientPtr = std::shared_ptr<net::TcpClient>;

    struct Entry
    {
        std::string host;
        std::uint16_t port = 0;
        HandlerPtr handler;
        ClientPtr  client;
    };

public:
    explicit LineManager(boost::asio::io_context& io)
        : io_(io)
    {
        // test_client는 1개면 충분 (필요하면 숫자만 늘려도 됨)
        actors_.start(1);
    }
    ~LineManager()
    {
        actors_.stop();
    }

    // 라인별로 세팅 + 생성(필요 시 즉시 start까지)
    void setup(std::uint8_t line, std::string host, std::uint16_t port, bool auto_start = true)
    {
        auto& e = entries_[line];
        e.host = std::move(host);
        e.port = port;

        // 라인마다 handler/client를 별개로 유지
        e.handler = std::make_shared<CNetworkEX>(line); // pro_id = line
        e.client = std::make_shared<net::TcpClient>(io_, e.handler);
        e.handler->AttachClient(e.client); // ✅ 중요: client attach
        e.handler->AttachDispatcher([this](std::uint64_t actor_id,
            std::function<void()> fn) { PostActor(actor_id, std::move(fn)); });

        if (auto_start)
            e.client->start(e.host, e.port);
    }

    void start(std::uint8_t line)
    {
        auto& e = entries_[line];
        if (e.client && !e.host.empty() && e.port != 0)
            e.client->start(e.host, e.port);
    }

    // 필요하면 stop/close를 구현 (TcpClient에 stop/close가 있다면 그걸 호출)
    void stop(std::uint8_t line)
    {
        auto& e = entries_[line];
        if (!e.client) return;

        // 예: TcpClient가 close/stop 지원한다고 가정
        // e.client->stop();
        // 또는 session을 닫는다든지
        if (auto s = e.client->session())
        {
            // s->close();  // 네 구조에 맞게
        }
    }

    ClientPtr client(std::uint8_t line) const
    {
        return entries_[line].client;
    }

    HandlerPtr handler(std::uint8_t line) const
    {
        return entries_[line].handler;
    }

    // 전체 라인 일괄 시작/중단
    void start_all()
    {
        for (std::size_t i = 0; i < (std::size_t)eLine::count; ++i)
        {
            auto& e = entries_[i];
            if (e.client && !e.host.empty() && e.port != 0)
                e.client->start(e.host, e.port);
        }
    }

    void PostActor(std::uint64_t actor_id, std::function<void()> fn)
    {
        actors_.post(actor_id, std::move(fn));
    }

private:
    boost::asio::io_context& io_;
    std::array<Entry, (std::size_t)eLine::count> entries_{};
    net::ActorSystem actors_;
};
