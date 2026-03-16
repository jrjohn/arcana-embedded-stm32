/**
 * @file ChaCha20Cipher.hpp
 * @brief ICipher implementation wrapping existing ChaCha20 class
 *
 * Header-only. Delegates to ChaCha20::crypt() (RFC 7539).
 */

#ifndef ARCANA_CHACHA20_CIPHER_HPP
#define ARCANA_CHACHA20_CIPHER_HPP

#include "ats/ICipher.hpp"
#include "ChaCha20.hpp"

namespace arcana {
namespace ats {

class ChaCha20Cipher : public ICipher {
public:
    void crypt(const uint8_t key[32], const uint8_t nonce[12],
               uint32_t counter, uint8_t* data, uint16_t len) override {
        arcana::crypto::ChaCha20::crypt(key, nonce, counter, data, len);
    }

    uint8_t cipherType() const override { return 1; }
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_CHACHA20_CIPHER_HPP */
