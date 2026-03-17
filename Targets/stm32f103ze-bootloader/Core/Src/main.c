/**
 * @file main.c
 * @brief OTA Bootloader for STM32F103ZE
 *
 * Flow:
 * 1. Init clock (72MHz), UART (debug), GPIO (LED, KEY1)
 * 2. Check BKP DR2/DR3 for OTA flag
 *    - If set: clear flag → mount SD → read ota_meta.bin → verify CRC →
 *      erase App flash → program → verify → rename firmware.bin → fw_prev.bin
 * 3. Check KEY1 held 2s → manual recovery from fw_prev.bin
 * 4. Validate App image at 0x08008000 → jump
 * 5. Invalid App → error LED blink loop
 */

#include "main.h"
#include "bl_flash.h"
#include "bl_uart.h"
#include "ota_header.h"
#include "Crc32.hpp"
#include "ff.h"
#include <string.h>

/* ---- Forward declarations ---- */
static void SystemClock_Config(void);
static void GPIO_Init(void);
static int  read_ota_flag(void);
static void clear_ota_flag(void);
static void set_led(GPIO_TypeDef* port, uint16_t pin, int on);
static void led_blink_error(void);
static int  is_key1_held(uint32_t ms);
static int  perform_ota_update(void);
static int  perform_recovery(void);
static void jump_to_app(uint32_t addr);
static int  flash_firmware(const char* filename);
static int  write_status(const char* msg);

/* ---- Globals ---- */
static FATFS fs;

/* 512-byte aligned buffer for f_read (used as both file I/O and flash staging) */
static uint8_t io_buf[512] __attribute__((aligned(4)));

/* ---- Main ---- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    bl_uart_init();
    GPIO_Init();

    bl_print("\r\n[BL] Arcana OTA Bootloader v1.0\r\n");

    /* Check OTA flag in backup registers */
    if (read_ota_flag()) {
        bl_print("[BL] OTA flag detected\r\n");
        clear_ota_flag();   /* Clear first — prevent infinite retry on power loss */

        if (f_mount(&fs, "", 1) == FR_OK) {
            bl_print("[BL] SD mounted\r\n");
            int result = perform_ota_update();
            f_mount(0, "", 0);  /* Unmount */

            if (result == 0) {
                bl_print("[BL] OTA update successful\r\n");
                set_led(LED_G_PORT, LED_G_PIN, 1);  /* Green = success */
                HAL_Delay(500);
                set_led(LED_G_PORT, LED_G_PIN, 0);
            } else {
                bl_print("[BL] OTA update FAILED\r\n");
                set_led(LED_R_PORT, LED_R_PIN, 1);  /* Red = failure */
                HAL_Delay(1000);
                set_led(LED_R_PORT, LED_R_PIN, 0);
            }
        } else {
            bl_print("[BL] SD mount failed\r\n");
        }
    }

    /* KEY1 held 2s → manual recovery from fw_prev.bin */
    if (is_key1_held(2000)) {
        bl_print("[BL] KEY1 recovery mode\r\n");
        set_led(LED_B_PORT, LED_B_PIN, 1);  /* Blue = recovery */

        if (f_mount(&fs, "", 1) == FR_OK) {
            int result = perform_recovery();
            f_mount(0, "", 0);

            if (result == 0) {
                bl_print("[BL] Recovery successful\r\n");
                set_led(LED_B_PORT, LED_B_PIN, 0);
                set_led(LED_G_PORT, LED_G_PIN, 1);
                HAL_Delay(500);
                set_led(LED_G_PORT, LED_G_PIN, 0);
            } else {
                bl_print("[BL] Recovery FAILED\r\n");
                set_led(LED_B_PORT, LED_B_PIN, 0);
                set_led(LED_R_PORT, LED_R_PIN, 1);
                HAL_Delay(1000);
                set_led(LED_R_PORT, LED_R_PIN, 0);
            }
        } else {
            bl_print("[BL] SD mount failed for recovery\r\n");
            set_led(LED_B_PORT, LED_B_PIN, 0);
        }
    }

    /* Validate and jump to App */
    if (ota_validate_app_image(APP_FLASH_BASE)) {
        bl_print("[BL] App valid, jumping to 0x08008000\r\n");
        HAL_Delay(10);  /* Flush UART */
        jump_to_app(APP_FLASH_BASE);
    }

    /* No valid App — error blink */
    bl_print("[BL] No valid App! Waiting...\r\n");
    led_blink_error();

    return 0;  /* Never reached */
}

/* ---- OTA Update ---- */
static int perform_ota_update(void)
{
    FIL f;
    UINT br;

    /* 1. Read ota_meta.bin */
    if (f_open(&f, OTA_META_FILENAME, FA_READ) != FR_OK) {
        bl_print("[BL] No ota_meta.bin\r\n");
        write_status("FAIL: no ota_meta.bin");
        return -1;
    }

    ota_meta_t meta;
    if (f_read(&f, &meta, sizeof(meta), &br) != FR_OK || br != sizeof(meta)) {
        f_close(&f);
        bl_print("[BL] Meta read error\r\n");
        write_status("FAIL: meta read error");
        return -1;
    }
    f_close(&f);

    /* 2. Validate meta */
    if (meta.magic != OTA_META_MAGIC || meta.version != OTA_META_VERSION) {
        bl_print("[BL] Meta magic/version mismatch\r\n");
        write_status("FAIL: meta magic/version");
        return -1;
    }

    /* Verify meta self-CRC */
    uint32_t expected_meta_crc = meta.meta_crc;
    uint32_t calc_meta_crc = ~crc32_calc(0xFFFFFFFF, (const uint8_t*)&meta,
                                          OTA_META_CRC_OFFSET);
    if (calc_meta_crc != expected_meta_crc) {
        bl_print("[BL] Meta CRC mismatch\r\n");
        write_status("FAIL: meta CRC");
        return -1;
    }

    if (meta.target_addr != APP_FLASH_BASE) {
        bl_print("[BL] Wrong target address\r\n");
        write_status("FAIL: wrong target addr");
        return -1;
    }

    if (meta.fw_size == 0 || meta.fw_size > APP_FLASH_SIZE) {
        bl_print("[BL] Invalid firmware size\r\n");
        write_status("FAIL: invalid fw size");
        return -1;
    }

    bl_print_hex("[BL] FW size: ", meta.fw_size);
    bl_print("\r\n");

    /* 3. Verify firmware.bin CRC */
    bl_print("[BL] Verifying firmware CRC...\r\n");
    if (f_open(&f, OTA_FW_FILENAME, FA_READ) != FR_OK) {
        bl_print("[BL] No firmware.bin\r\n");
        write_status("FAIL: no firmware.bin");
        return -1;
    }

    uint32_t crc = 0xFFFFFFFF;
    uint32_t total_read = 0;
    while (total_read < meta.fw_size) {
        uint32_t chunk = meta.fw_size - total_read;
        if (chunk > sizeof(io_buf)) chunk = sizeof(io_buf);

        if (f_read(&f, io_buf, chunk, &br) != FR_OK || br == 0) {
            f_close(&f);
            bl_print("[BL] FW read error during CRC\r\n");
            write_status("FAIL: fw read error");
            return -1;
        }
        crc = crc32_calc(crc, io_buf, br);
        total_read += br;
    }
    f_close(&f);

    crc = ~crc;
    if (crc != meta.crc32) {
        bl_print("[BL] FW CRC mismatch!\r\n");
        write_status("FAIL: fw CRC mismatch");
        return -1;
    }
    bl_print("[BL] CRC OK\r\n");

    /* 4. Flash firmware */
    return flash_firmware(OTA_FW_FILENAME);
}

/* ---- Manual Recovery ---- */
static int perform_recovery(void)
{
    FILINFO fno;
    /* Check fw_prev.bin exists */
    if (f_stat(OTA_PREV_FILENAME, &fno) != FR_OK || fno.fsize == 0) {
        bl_print("[BL] No fw_prev.bin for recovery\r\n");
        return -1;
    }

    bl_print("[BL] Recovering from fw_prev.bin\r\n");
    return flash_firmware(OTA_PREV_FILENAME);
}

/* ---- Common: erase + program + verify from file ---- */
static int flash_firmware(const char* filename)
{
    FIL f;
    UINT br;

    /* Get file size */
    if (f_open(&f, filename, FA_READ) != FR_OK) {
        return -1;
    }
    uint32_t fw_size = f_size(&f);

    /* Calculate pages needed */
    uint32_t pages = (fw_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    bl_print_hex("[BL] Erasing pages: ", pages);
    bl_print("\r\n");

    /* 1. Erase */
    if (bl_flash_erase_app(pages) != 0) {
        f_close(&f);
        bl_print("[BL] Erase FAILED\r\n");
        write_status("FAIL: flash erase");
        return -1;
    }
    bl_print("[BL] Erase OK\r\n");

    /* 2. Program */
    bl_print("[BL] Programming...\r\n");
    uint32_t addr = APP_FLASH_BASE;
    uint32_t programmed = 0;

    while (programmed < fw_size) {
        uint32_t chunk = fw_size - programmed;
        if (chunk > sizeof(io_buf)) chunk = sizeof(io_buf);

        if (f_read(&f, io_buf, chunk, &br) != FR_OK || br == 0) {
            f_close(&f);
            bl_print("[BL] Read error during program\r\n");
            write_status("FAIL: read during program");
            return -1;
        }

        if (bl_flash_program(addr, io_buf, br) != 0) {
            f_close(&f);
            bl_print_hex("[BL] Program FAILED at ", addr);
            bl_print("\r\n");
            write_status("FAIL: flash program");
            return -1;
        }

        addr += br;
        programmed += br;
    }
    f_close(&f);

    bl_print("[BL] Program OK\r\n");

    /* 3. Verify — CRC of flash contents */
    bl_print("[BL] Verifying flash...\r\n");
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* flash_ptr = (const uint8_t*)APP_FLASH_BASE;
    for (uint32_t i = 0; i < fw_size; i += sizeof(io_buf)) {
        uint32_t chunk = fw_size - i;
        if (chunk > sizeof(io_buf)) chunk = sizeof(io_buf);
        crc = crc32_calc(crc, flash_ptr + i, chunk);
    }

    /* Re-read file to compare CRC */
    if (f_open(&f, filename, FA_READ) != FR_OK) {
        return -1;
    }
    uint32_t file_crc = 0xFFFFFFFF;
    while (1) {
        if (f_read(&f, io_buf, sizeof(io_buf), &br) != FR_OK || br == 0) break;
        file_crc = crc32_calc(file_crc, io_buf, br);
    }
    f_close(&f);

    if (crc != file_crc) {
        bl_print("[BL] Verify FAILED — CRC mismatch\r\n");
        write_status("FAIL: verify CRC mismatch");
        return -1;
    }

    bl_print("[BL] Verify OK\r\n");

    /* 4. Rename firmware.bin → fw_prev.bin (if flashing firmware.bin) */
    if (strcmp(filename, OTA_FW_FILENAME) == 0) {
        f_unlink(OTA_PREV_FILENAME);    /* Delete old backup */
        f_rename(OTA_FW_FILENAME, OTA_PREV_FILENAME);
        f_unlink(OTA_META_FILENAME);    /* Clean up meta */
        bl_print("[BL] Renamed to fw_prev.bin\r\n");
    }

    write_status("OK");
    return 0;
}

/* ---- Write OTA status file ---- */
static int write_status(const char* msg)
{
    FIL f;
    UINT bw;
    if (f_open(&f, OTA_STATUS_FILENAME, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        return -1;
    }
    f_write(&f, msg, strlen(msg), &bw);
    f_write(&f, "\r\n", 2, &bw);
    f_close(&f);
    return 0;
}

/* ---- Jump to Application ---- */
static void jump_to_app(uint32_t addr)
{
    uint32_t sp    = *(volatile uint32_t*)addr;
    uint32_t reset = *(volatile uint32_t*)(addr + 4);

    /* Validate */
    if (sp < 0x20000000 || sp > 0x20010000) return;
    if (reset < addr || reset > APP_FLASH_END) return;

    /* De-init everything */
    HAL_DeInit();
    HAL_RCC_DeInit();

    /* Disable all interrupts */
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* Clear all pending interrupts */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* Set VTOR to App */
    SCB->VTOR = addr;

    /* Set MSP and jump */
    __set_MSP(sp);
    ((void(*)(void))reset)();
}

/* ---- OTA Flag (BKP DR2/DR3) ---- */
static int read_ota_flag(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    uint16_t dr2 = BKP->DR2;
    uint16_t dr3 = BKP->DR3;

    return (dr2 == OTA_FLAG_DR2_VALUE && dr3 == OTA_FLAG_DR3_VALUE);
}

static void clear_ota_flag(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    BKP->DR2 = 0;
    BKP->DR3 = 0;
}

/* ---- GPIO Init ---- */
static void GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* LEDs: PB5(R), PB0(G), PB1(B) — all OFF (HIGH = off, common anode) */
    HAL_GPIO_WritePin(GPIOB, LED_R_PIN | LED_G_PIN | LED_B_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = LED_R_PIN | LED_G_PIN | LED_B_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* KEY1: PA0 — input, no pull (external pull-down on board) */
    GPIO_InitStruct.Pin = KEY1_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(KEY1_PORT, &GPIO_InitStruct);
}

/* ---- LED helper (active-low) ---- */
static void set_led(GPIO_TypeDef* port, uint16_t pin, int on)
{
    HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ---- Error blink: red LED fast blink ---- */
static void led_blink_error(void)
{
    while (1) {
        set_led(LED_R_PORT, LED_R_PIN, 1);
        HAL_Delay(200);
        set_led(LED_R_PORT, LED_R_PIN, 0);
        HAL_Delay(200);
    }
}

/* ---- KEY1 held check (blocking) ---- */
static int is_key1_held(uint32_t ms)
{
    /* Debounce: wait for GPIO to settle after reset */
    HAL_Delay(100);

    /* KEY1 pressed = HIGH (野火霸道 V2: active-high with external pull-down) */
    if (HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) != GPIO_PIN_SET) return 0;

    /* Wait for KEY1 to be held for the specified duration */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < ms) {
        if (HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) != GPIO_PIN_SET) return 0;
        HAL_Delay(50);  /* Check every 50ms */
    }
    return 1;
}

/* ---- Clock Config: HSE 8MHz → PLL ×9 → 72MHz ---- */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

/* ---- Error handler (required by HAL) ---- */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        set_led(LED_R_PORT, LED_R_PIN, 1);
        for (volatile int i = 0; i < 500000; i++) {}
        set_led(LED_R_PORT, LED_R_PIN, 0);
        for (volatile int i = 0; i < 500000; i++) {}
    }
}
