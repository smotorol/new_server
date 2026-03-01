#include "../app/networkex_base.h"

class MyNetworkEX : public dc::NetworkEXBase {
public:
    using dc::NetworkEXBase::NetworkEXBase;

protected:
    bool DataAnalysis(std::uint32_t pro, std::uint32_t idx, _MSG_HEADER* hdr, char* body) override {
        // ✅ 여기만 구현 (메인 단일 writer에서 호출됨)
        return true;
    }

    void AcceptClientCheck(std::uint32_t pro, std::uint32_t idx, std::uint32_t serial) override {
        // 접속 처리
    }

    void CloseClientCheck(std::uint32_t pro, std::uint32_t idx, std::uint32_t serial) override {
        // 종료 처리
    }
};
