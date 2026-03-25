/**
 * Minimal mbedtls config for STM32F103 — AES-256-CCM + SHA-256 only.
 * Defense-grade: compatible with ESP32 CryptoEngine (AES-256-CCM, 8-byte tag).
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Core crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_CCM_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC

/* Platform */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES

/* Time support (needed for MBEDTLS_PLATFORM_MS_TIME_ALT) */
#define MBEDTLS_HAVE_TIME

/* Provide our own ms_time stub (no OS timer on bare-metal STM32) */
#define MBEDTLS_PLATFORM_MS_TIME_ALT

#endif /* MBEDTLS_CONFIG_H */
