#pragma once
#include <boost/asio.hpp>
#include "LineManager.h"

// 전역 접근 함수 (정적 초기화 순서 문제 회피)
boost::asio::io_context& GlobalIo();
LineManager& GlobalLineClients();
