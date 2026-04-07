/**
 * Host-side mbedtls hardware entropy stub.
 *
 * The production entropy_hardware_alt.c reads STM32 UID/SysTick/DWT/ADC,
 * which obviously won't link on a Mac. Provide a deterministic but
 * sufficiently varied byte stream so ECDH key generation succeeds in
 * unit tests. NOT FOR PRODUCTION USE.
 */
#include "mbedtls/entropy.h"
#include <string.h>
#include <stdint.h>

#ifdef MBEDTLS_ENTROPY_HARDWARE_ALT

int mbedtls_hardware_poll(void *data, unsigned char *output,
                          size_t len, size_t *olen) {
    (void)data;
    static uint64_t s = 0xCAFEBABEDEADBEEFull;
    for (size_t i = 0; i < len; ++i) {
        /* xorshift64 */
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        output[i] = (unsigned char)(s & 0xFF);
    }
    *olen = len;
    return 0;
}

#endif
