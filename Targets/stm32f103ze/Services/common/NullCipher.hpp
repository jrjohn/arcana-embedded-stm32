/**
 * @file NullCipher.hpp
 * @brief ICipher no-op implementation (debug/testing)
 *
 * Header-only. Data passes through unencrypted.
 */

#ifndef ARCANA_NULL_CIPHER_HPP
#define ARCANA_NULL_CIPHER_HPP

#include "ats/ICipher.hpp"

namespace arcana {
namespace ats {

class NullCipher : public ICipher {
public:
    void crypt(const uint8_t[32], const uint8_t[12],
               uint32_t, uint8_t*, uint16_t) override {
        // no-op
    }

    uint8_t cipherType() const override { return 0; }
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_NULL_CIPHER_HPP */
