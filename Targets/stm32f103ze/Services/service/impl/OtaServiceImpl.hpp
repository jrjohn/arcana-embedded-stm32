#pragma once

#include "OtaService.hpp"
#include "Esp8266.hpp"

namespace arcana {

class OtaServiceImpl : public OtaService {
public:
    static OtaServiceImpl& getInstance();

    bool startUpdate(const char* host, uint16_t port,
                     const char* path, uint32_t expectedSize,
                     uint32_t expectedCrc32) override;

    uint8_t getProgress() const override { return mProgress; }
    bool isActive() const override { return mActive; }

    struct {
        Esp8266* esp = nullptr;
    } input;

private:
    /* Test access — host gtest fixture exercises private helpers. */
    friend struct OtaServiceTestAccess;

    OtaServiceImpl();

    bool httpGet(const char* host, uint16_t port, const char* path);
    bool receiveToFile(uint32_t expectedSize);
    bool verifyCrc(uint32_t expectedCrc32, uint32_t fileSize);
    bool writeOtaMeta(uint32_t fwSize, uint32_t crc32, const char* version);
    void setOtaFlag();

    volatile uint8_t mProgress;
    volatile bool mActive;
};

} // namespace arcana
