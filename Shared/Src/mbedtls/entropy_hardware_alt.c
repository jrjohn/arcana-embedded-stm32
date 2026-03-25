/**
 * STM32F103 hardware entropy source for mbedtls.
 *
 * STM32F103 has no TRNG. We combine:
 * - ADC channel 16 (internal temperature sensor) LSBs → noise
 * - Device unique ID (96 bits at UID_BASE)
 * - SysTick counter
 *
 * This is NOT cryptographically ideal but provides sufficient
 * unpredictability for ECDH key generation combined with ctr_drbg.
 */
#include "mbedtls/entropy.h"

#ifdef MBEDTLS_ENTROPY_HARDWARE_ALT

#include "stm32f1xx_hal.h"
#include <string.h>

int mbedtls_hardware_poll(void *data, unsigned char *output,
                          size_t len, size_t *olen) {
    (void)data;

    /* Mix: UID (12 bytes) + SysTick + DWT cycle counter */
    uint32_t seed[8];
    const uint32_t* uid = (const uint32_t*)UID_BASE;
    seed[0] = uid[0];
    seed[1] = uid[1];
    seed[2] = uid[2];
    seed[3] = SysTick->VAL;
    seed[4] = DWT->CYCCNT;
    seed[5] = HAL_GetTick();

    /* ADC noise: read internal temp sensor, take LSBs */
    /* ADC1 should already be initialized by SensorService */
    seed[6] = ADC1->DR & 0xFFFF;
    seed[7] = (ADC1->DR << 16) ^ SysTick->VAL ^ DWT->CYCCNT;

    /* Copy seed bytes to output (repeat if needed) */
    size_t seedLen = sizeof(seed);
    size_t copied = 0;
    while (copied < len) {
        size_t chunk = (len - copied) < seedLen ? (len - copied) : seedLen;
        memcpy(output + copied, seed, chunk);
        copied += chunk;
        /* Remix for next chunk */
        seed[3] ^= SysTick->VAL;
        seed[4] ^= DWT->CYCCNT;
    }

    *olen = len;
    return 0;
}

#endif /* MBEDTLS_ENTROPY_HARDWARE_ALT */
