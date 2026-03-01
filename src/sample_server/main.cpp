#include "../app/endpoint_app.h"
#include "MyNetworkEX.h"

int main(int argc, char** argv)
{
	// 예: --mode=server / --mode=client
	const bool is_server = true; // 파싱해서 결정

	dc::EndpointConfig cfg;
	cfg.role = is_server ? dc::EndpointRole::Server : dc::EndpointRole::Client;
	cfg.host = "127.0.0.1";
	cfg.port = 67787;
	cfg.net_threads = 2;

	auto handler = std::make_shared<MyNetworkEX>(/*pro_id=*/0);

	dc::EndpointApp app(cfg, handler);
	app.start();

	// 메인 스레드는 그냥 대기(또는 콘솔/게임루프)
	std::this_thread::sleep_for(std::chrono::hours(24));
}
