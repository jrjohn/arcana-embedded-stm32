/**
 * @file ats_mocks.hpp
 * @brief Host-side PAL mocks for ArcanaTS unit tests
 *
 * Header-only. Provides:
 *   - MemFilePort  : in-memory IFilePort backed by std::vector<uint8_t>
 *   - NullCipher   : pass-through ICipher (cipherType=1, no transform)
 *   - XorCipher    : deterministic reversible XOR ICipher (cipherType=1)
 *   - StubMutex    : no-op IMutex for single-threaded host tests
 *   - TestClock    : monotonic time source for AtsGetTimeFn
 */

#ifndef ARCANA_TESTS_ATS_MOCKS_HPP
#define ARCANA_TESTS_ATS_MOCKS_HPP

#include <cstdint>
#include <cstring>
#include <vector>

#include "ats/IFilePort.hpp"
#include "ats/ICipher.hpp"
#include "ats/IMutex.hpp"
#include "ats/ArcanaTsTypes.hpp"

namespace arcana_test {

// ── In-memory file port ──────────────────────────────────────────────────────

class MemFilePort : public arcana::ats::IFilePort {
public:
    std::vector<uint8_t> data;
    uint64_t pos = 0;
    bool opened = false;
    bool failNextSeek = false;
    bool failNextWrite = false;
    bool failNextRead  = false;

    bool open(const char* /*path*/, uint8_t mode) override {
        opened = true;
        if ((mode & arcana::ats::ATS_MODE_CREATE) && data.empty()) {
            // create empty
        }
        pos = 0;
        return true;
    }
    bool close() override { opened = false; return true; }

    int32_t read(uint8_t* buf, uint32_t size) override {
        if (failNextRead) { failNextRead = false; return -1; }
        if (pos >= data.size()) return 0;
        uint32_t avail = static_cast<uint32_t>(data.size() - pos);
        uint32_t n = (size < avail) ? size : avail;
        std::memcpy(buf, data.data() + pos, n);
        pos += n;
        return static_cast<int32_t>(n);
    }

    int32_t write(const uint8_t* buf, uint32_t size) override {
        if (failNextWrite) { failNextWrite = false; return -1; }
        if (pos + size > data.size()) data.resize(pos + size, 0xFF);
        std::memcpy(data.data() + pos, buf, size);
        pos += size;
        return static_cast<int32_t>(size);
    }

    bool seek(uint64_t offset) override {
        if (failNextSeek) { failNextSeek = false; return false; }
        pos = offset;
        return true;
    }
    bool sync() override { return true; }
    uint64_t tell() override { return pos; }
    uint64_t size() override { return data.size(); }
    bool truncate() override {
        if (pos < data.size()) data.resize(pos);
        return true;
    }
    bool isOpen() const override { return opened; }
};

// ── Pass-through cipher (cipherType=1, leaves data untouched) ────────────────

class NullCipher : public arcana::ats::ICipher {
public:
    void crypt(const uint8_t /*key*/[32], const uint8_t /*nonce*/[12],
               uint32_t /*counter*/, uint8_t* /*data*/, uint16_t /*len*/) override {
        // intentional no-op
    }
    uint8_t cipherType() const override { return 1; }
};

// ── Deterministic XOR cipher (reversible, exercises encrypt→decrypt path) ────

class XorCipher : public arcana::ats::ICipher {
public:
    void crypt(const uint8_t key[32], const uint8_t nonce[12],
               uint32_t counter, uint8_t* data, uint16_t len) override {
        for (uint16_t i = 0; i < len; ++i) {
            uint8_t k = key[i & 31] ^ nonce[i % 12]
                      ^ static_cast<uint8_t>(counter + i);
            data[i] ^= k;
        }
    }
    uint8_t cipherType() const override { return 1; }
};

// ── No-op mutex for single-threaded host tests ───────────────────────────────

class StubMutex : public arcana::ats::IMutex {
public:
    int lockCount = 0;
    bool lock(uint32_t /*timeoutMs*/ = 0xFFFFFFFF) override {
        ++lockCount; return true;
    }
    void unlock() override { --lockCount; }
};

// ── Monotonic test clock (advances 1 second per call by default) ─────────────

class TestClock {
public:
    static uint32_t sNow;
    static uint32_t sStep;
    static uint32_t now() {
        uint32_t t = sNow;
        sNow += sStep;
        return t;
    }
    static void reset(uint32_t start = 1000000000u, uint32_t step = 1) {
        sNow = start;
        sStep = step;
    }
};

inline uint32_t TestClock::sNow  = 1000000000u;
inline uint32_t TestClock::sStep = 1;

} // namespace arcana_test

#endif // ARCANA_TESTS_ATS_MOCKS_HPP
