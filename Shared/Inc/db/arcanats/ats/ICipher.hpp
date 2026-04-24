/**
 * @file ICipher.hpp
 * @brief Platform abstraction for block encryption
 *
 * Implementations: ChaCha20Cipher (software), Esp32HwAesCipher, NullCipher.
 */

#ifndef ARCANA_ATS_ICIPHER_HPP
#define ARCANA_ATS_ICIPHER_HPP

#include <cstdint>

namespace arcana {
namespace ats {

/**
 * @brief Abstract cipher interface for block payload encryption
 *
 * cipherType values: 0=none, 1=ChaCha20, 2=AES-256-CTR
 */
class ICipher {
public:
    virtual ~ICipher() {}

    /**
     * @brief Encrypt/decrypt data in-place (stream cipher — same operation)
     * @param key     32-byte encryption key
     * @param nonce   12-byte nonce
     * @param counter Initial block counter
     * @param data    Buffer to encrypt/decrypt in-place
     * @param len     Data length (max BLOCK_PAYLOAD_SIZE = 4064)
     */
    virtual void crypt(const uint8_t key[32], const uint8_t nonce[12],
                       uint32_t counter, uint8_t* data, uint16_t len) = 0;

    /** @brief Cipher type ID stored in file header */
    virtual uint8_t cipherType() const = 0;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_ICIPHER_HPP */
