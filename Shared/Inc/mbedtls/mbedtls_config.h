/**
 * mbedtls config for STM32F103 — AES-256-CCM + SHA-256 + ECDH P-256.
 * Defense-grade: compatible with ESP32 CryptoEngine + KeyExchangeManager.
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Core crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_CCM_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_MD_C              /* HMAC-SHA256 for HKDF + auth tag */

/* ECDH P-256 key exchange + ECDSA signature verification */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_PARSE_C          /* ECDSA signature parsing */
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED

/* RNG for ECDH key generation */
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT  /* custom STM32 entropy source */

/* Platform */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* Time support */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/* Reduce ECP memory: use fewer window bits (saves ~1KB stack) */
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0

#endif /* MBEDTLS_CONFIG_H */
