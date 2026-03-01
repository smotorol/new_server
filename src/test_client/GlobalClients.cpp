#include "GlobalClients.h"
#include <thread>
#include <atomic>

namespace
{
    std::thread g_netThread;
    std::atomic_bool g_started{ false };
}

boost::asio::io_context& GlobalIo()
{
    static boost::asio::io_context io;
    return io;
}

LineManager& GlobalLineClients()
{
    static LineManager mgr(GlobalIo());
    return mgr;
}
