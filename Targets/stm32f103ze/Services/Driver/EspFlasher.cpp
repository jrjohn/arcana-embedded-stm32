/**
 * @file EspFlasher.cpp
 * @brief ESP8266 firmware flasher — SD card → USART3 SLIP bootloader protocol
 */

#include "EspFlasher.hpp"
#include "stm32f1xx_hal.h"
#include "ff.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <cstdio>
#include <cstring>

extern "C" volatile uint8_t g_exfat_ready;

/* Static I/O buffer — 512 bytes to save RAM */
static uint8_t sIoBuf[512] __attribute__((aligned(4)));

/* No separate task needed — I/O buffer is static, doFlash() fits in default task stack */

namespace arcana {

/* ---- UART3 direct register access (bypass HAL for bootloader speed) ---- */

static USART_TypeDef* const ESP_UART = USART3;

void EspFlasher::uartSendByte(uint8_t b) {
    while (!(ESP_UART->SR & USART_SR_TXE)) {}
    ESP_UART->DR = b;
}

void EspFlasher::uartSend(const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uartSendByte(data[i]);
    }
}

int EspFlasher::uartRecvByte(uint32_t timeoutMs) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeoutMs) {
        if (ESP_UART->SR & USART_SR_RXNE) {
            return (uint8_t)(ESP_UART->DR & 0xFF);
        }
    }
    return -1;  /* timeout */
}

void EspFlasher::uartFlushRx() {
    while (ESP_UART->SR & USART_SR_RXNE) {
        volatile uint32_t dummy = ESP_UART->DR;
        (void)dummy;
    }
}

/* ---- SLIP framing ---- */

void EspFlasher::slipBegin() {
    uartSendByte(SLIP_END);
}

void EspFlasher::slipSend(const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] == SLIP_END) {
            uartSendByte(SLIP_ESC);
            uartSendByte(SLIP_ESC_END);
        } else if (data[i] == SLIP_ESC) {
            uartSendByte(SLIP_ESC);
            uartSendByte(SLIP_ESC_ESC);
        } else {
            uartSendByte(data[i]);
        }
    }
}

void EspFlasher::slipEnd() {
    uartSendByte(SLIP_END);
    /* Wait for TX complete */
    while (!(ESP_UART->SR & USART_SR_TC)) {}
}

/* ---- Checksum: XOR of data bytes, seed 0xEF ---- */

uint32_t EspFlasher::checksum(const uint8_t* data, uint32_t len) {
    uint8_t chk = 0xEF;
    for (uint32_t i = 0; i < len; i++) {
        chk ^= data[i];
    }
    return (uint32_t)chk;
}

/* ---- Protocol commands ---- */

bool EspFlasher::sendCommand(uint8_t cmd, const uint8_t* payload, uint16_t payloadLen,
                              uint32_t chk) {
    /* Header: direction(1) + cmd(1) + size(2 LE) + checksum(4 LE) = 8 bytes */
    uint8_t hdr[8];
    hdr[0] = 0x00;  /* direction = request */
    hdr[1] = cmd;
    hdr[2] = (uint8_t)(payloadLen & 0xFF);
    hdr[3] = (uint8_t)((payloadLen >> 8) & 0xFF);
    hdr[4] = (uint8_t)(chk & 0xFF);
    hdr[5] = (uint8_t)((chk >> 8) & 0xFF);
    hdr[6] = (uint8_t)((chk >> 16) & 0xFF);
    hdr[7] = (uint8_t)((chk >> 24) & 0xFF);

    uartFlushRx();

    slipBegin();
    slipSend(hdr, 8);
    if (payloadLen > 0 && payload) {
        slipSend(payload, payloadLen);
    }
    slipEnd();

    return true;
}

bool EspFlasher::recvResponse(uint8_t expectedCmd, uint32_t timeoutMs) {
    /* Wait for SLIP_END start delimiter */
    int b;
    uint32_t start = HAL_GetTick();

    /* Skip to start of SLIP frame */
    do {
        b = uartRecvByte(timeoutMs);
        if (b < 0) return false;
        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed > timeoutMs) return false;
    } while (b != SLIP_END);

    /* Read response bytes (un-SLIP) into buffer */
    uint8_t resp[64];
    uint16_t pos = 0;
    bool escaped = false;

    while (pos < sizeof(resp)) {
        b = uartRecvByte(timeoutMs);
        if (b < 0) return false;

        if (escaped) {
            if (b == SLIP_ESC_END) resp[pos++] = SLIP_END;
            else if (b == SLIP_ESC_ESC) resp[pos++] = SLIP_ESC;
            else resp[pos++] = (uint8_t)b;  /* malformed, keep anyway */
            escaped = false;
        } else if (b == SLIP_ESC) {
            escaped = true;
        } else if (b == SLIP_END) {
            break;  /* End of frame */
        } else {
            resp[pos++] = (uint8_t)b;
        }
    }

    /* Validate: direction=1, cmd matches, status OK */
    if (pos < 10) return false;
    if (resp[0] != 0x01) return false;         /* direction = response */
    if (resp[1] != expectedCmd) return false;   /* cmd echo */
    /* Status bytes at end: resp[pos-2]=status, resp[pos-1]=error */
    if (resp[pos - 2] != 0x00) return false;    /* status != success */

    return true;
}

/* ---- SYNC ---- */

bool EspFlasher::sync() {
    /* SYNC payload: 0x07 0x07 0x12 0x20 + 32 × 0x55 = 36 bytes */
    uint8_t payload[36];
    payload[0] = 0x07;
    payload[1] = 0x07;
    payload[2] = 0x12;
    payload[3] = 0x20;
    memset(payload + 4, 0x55, 32);

    sendCommand(CMD_SYNC, payload, 36, 0);
    return recvResponse(CMD_SYNC, 1000);
}

/* ---- FLASH_BEGIN ---- */

bool EspFlasher::flashBegin(uint32_t eraseSize, uint32_t numBlocks,
                             uint32_t blockSize, uint32_t offset) {
    uint8_t payload[16];
    memcpy(payload + 0,  &eraseSize, 4);
    memcpy(payload + 4,  &numBlocks, 4);
    memcpy(payload + 8,  &blockSize, 4);
    memcpy(payload + 12, &offset,    4);

    printf("[ESPFW] >> sendCmd FLASH_BEGIN\r\n");
    sendCommand(CMD_FLASH_BEGIN, payload, 16, 0);
    uint32_t timeout = 10000 + (eraseSize / 1024) * 100;
    printf("[ESPFW] >> recvResp (timeout=%lu)\r\n", (unsigned long)timeout);
    bool ok = recvResponse(CMD_FLASH_BEGIN, timeout);
    printf("[ESPFW] >> recvResp=%d\r\n", ok);
    return ok;
}

/* ---- FLASH_DATA ---- */

bool EspFlasher::flashData(const uint8_t* data, uint32_t dataLen, uint32_t seq) {
    /* Header: data_size(4) + seq(4) + 0(4) + 0(4) = 16 bytes, then data */
    uint8_t hdr[16];
    uint32_t zero = 0;
    memcpy(hdr + 0, &dataLen, 4);
    memcpy(hdr + 4, &seq, 4);
    memcpy(hdr + 8, &zero, 4);
    memcpy(hdr + 12, &zero, 4);

    uint16_t totalLen = 16 + (uint16_t)dataLen;
    uint32_t chk = checksum(data, dataLen);

    /* Send as single SLIP frame: header(8) + [flash_hdr(16) + data] */
    uint8_t pktHdr[8];
    pktHdr[0] = 0x00;  /* direction */
    pktHdr[1] = CMD_FLASH_DATA;
    pktHdr[2] = (uint8_t)(totalLen & 0xFF);
    pktHdr[3] = (uint8_t)((totalLen >> 8) & 0xFF);
    pktHdr[4] = (uint8_t)(chk & 0xFF);
    pktHdr[5] = (uint8_t)((chk >> 8) & 0xFF);
    pktHdr[6] = (uint8_t)((chk >> 16) & 0xFF);
    pktHdr[7] = (uint8_t)((chk >> 24) & 0xFF);

    uartFlushRx();
    slipBegin();
    slipSend(pktHdr, 8);
    slipSend(hdr, 16);
    slipSend(data, (uint16_t)dataLen);
    slipEnd();

    return recvResponse(CMD_FLASH_DATA, 5000);
}

/* ---- FLASH_END ---- */

bool EspFlasher::flashEnd(bool reboot) {
    uint8_t payload[4];
    uint32_t flag = reboot ? 0x00 : 0x01;
    memcpy(payload, &flag, 4);

    sendCommand(CMD_FLASH_END, payload, 4, 0);
    return recvResponse(CMD_FLASH_END, 2000);
}

/* ---- ESP8266 hardware reset ---- */

void EspFlasher::espReset() {
    /* PG14 = RST (active LOW), PG13 = CH_PD (active HIGH) */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);    /* CH_PD = enable */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);  /* RST = LOW */
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);    /* RST = HIGH */
    vTaskDelay(pdMS_TO_TICKS(500));  /* Wait for bootloader ready */
}

/* ---- Flash one partition file from SD ---- */

bool EspFlasher::flashPartition(const char* path, uint32_t offset) {
    printf("[ESPFW] >> open %s\r\n", path);
    FIL f;
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) {
        printf("[ESPFW] File not found: %s err=%d\r\n", path, fr);
        return false;
    }

    uint32_t fileSize = f_size(&f);
    uint32_t numBlocks = (fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;

    printf("[ESPFW] %s → 0x%05lX (%lu bytes, %lu blk)\r\n",
           path, (unsigned long)offset, (unsigned long)fileSize, (unsigned long)numBlocks);

    /* FLASH_BEGIN — triggers erase */
    printf("[ESPFW] >> FLASH_BEGIN\r\n");
    if (!flashBegin(fileSize, numBlocks, BLOCK_SIZE, offset)) {
        printf("[ESPFW] FLASH_BEGIN failed\r\n");
        f_close(&f);
        return false;
    }
    printf("[ESPFW] >> FLASH_BEGIN OK\r\n");

    /* FLASH_DATA — send file in BLOCK_SIZE chunks (using static buffer) */
    uint32_t seq = 0;

    while (seq < numBlocks) {
        UINT br;
        memset(sIoBuf, 0xFF, BLOCK_SIZE);  /* Pad with 0xFF */
        if (f_read(&f, sIoBuf, BLOCK_SIZE, &br) != FR_OK) {
            printf("[ESPFW] Read error at block %lu\r\n", (unsigned long)seq);
            f_close(&f);
            return false;
        }

        if (!flashData(sIoBuf, BLOCK_SIZE, seq)) {
            printf("[ESPFW] FLASH_DATA failed at block %lu\r\n", (unsigned long)seq);
            f_close(&f);
            return false;
        }

        seq++;
        if ((seq % 64) == 0 || seq == numBlocks) {
            printf("[ESPFW]   %lu/%lu blocks\r\n", (unsigned long)seq, (unsigned long)numBlocks);
        }
    }

    f_close(&f);
    return true;
}

/* ---- Main entry point ---- */

/* Partition table for 1MB AT v2.2 build */
struct EspPartition {
    const char* path;
    uint32_t    offset;
};

static const EspPartition partitions[] = {
    { "esp_fw/bootloader.bin",   0x00000 },
    { "esp_fw/partitions.bin",   0x08000 },
    { "esp_fw/ota_data.bin",     0x09000 },
    { "esp_fw/at_customize.bin", 0x18000 },
    { "esp_fw/factory_param.bin",0x19000 },
    { "esp_fw/client_cert.bin",  0x1A000 },
    { "esp_fw/client_key.bin",   0x1B000 },
    { "esp_fw/client_ca.bin",    0x1C000 },
    { "esp_fw/mqtt_cert.bin",    0x1D000 },
    { "esp_fw/mqtt_key.bin",     0x1E000 },
    { "esp_fw/mqtt_ca.bin",      0x1F000 },
    { "esp_fw/esp-at.bin",       0x20000 },
};
static const int NUM_PARTITIONS = sizeof(partitions) / sizeof(partitions[0]);

bool EspFlasher::run() {
    /* Wait for SD mount */
    if (!g_exfat_ready) {
        for (int i = 0; i < 150 && !g_exfat_ready; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!g_exfat_ready) return false;
    }

    FILINFO fno;
    bool hasMain = (f_stat("esp_fw/esp-at.bin", &fno) == FR_OK);
    bool hasParam = (f_stat("esp_fw/factory_param.bin", &fno) == FR_OK);
    if (!hasMain && !hasParam) {
        return false;  /* No ESP firmware on SD — skip silently */
    }

    printf("\r\n[ESPFW] === ESP8266 Firmware Update ===\r\n");
    printf("[ESPFW] Found esp_fw/ on SD card\r\n");

    return doFlash();
}

/* ---- Main flash logic — runs in dedicated 4KB task ---- */

bool EspFlasher::doFlash() {
    /* Disable USART3 ISR — Esp8266 driver's IRQ steals RX bytes */
    HAL_NVIC_DisableIRQ(USART3_IRQn);
    uartFlushRx();

    printf("[ESPFW] Resetting ESP8266...\r\n");
    espReset();

    /* Try SYNC */
    printf("[ESPFW] Syncing with bootloader...\r\n");
    bool synced = false;
    for (int i = 0; i < 10; i++) {
        if (sync()) {
            synced = true;
            for (int j = 0; j < 3; j++) {
                recvResponse(CMD_SYNC, 200);
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!synced) {
        printf("[ESPFW] SYNC failed — J83 not shorted? Skipping.\r\n");
        HAL_NVIC_EnableIRQ(USART3_IRQn);
        return false;
    }

    printf("[ESPFW] Bootloader connected!\r\n");

    /* List files in esp_fw */
    FILINFO fno;
    DIR dir;
    if (f_opendir(&dir, "esp_fw") == FR_OK) {
        printf("[ESPFW] Files:\r\n");
        while (1) {
            if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
            printf("[ESPFW]   %s (%lu)\r\n", fno.fname, (unsigned long)fno.fsize);
        }
        f_closedir(&dir);
    }

    /* Flash each partition */
    int ok = 0, fail = 0;
    for (int i = 0; i < NUM_PARTITIONS; i++) {
        FRESULT fr = f_stat(partitions[i].path, &fno);
        if (fr != FR_OK) {
            printf("[ESPFW] Skip: %s (err=%d)\r\n", partitions[i].path, fr);
            continue;
        }
        if (flashPartition(partitions[i].path, partitions[i].offset)) {
            ok++;
        } else {
            fail++;
            printf("[ESPFW] FAILED: %s\r\n", partitions[i].path);
            break;
        }
    }

    flashEnd(true);

    uartFlushRx();
    HAL_NVIC_EnableIRQ(USART3_IRQn);

    if (fail == 0 && ok > 0) {
        printf("[ESPFW] === SUCCESS: %d partitions flashed ===\r\n", ok);
        printf("[ESPFW] Remove J83 jumper, then reset board.\r\n");
    } else {
        printf("[ESPFW] === FAILED: %d OK, %d failed ===\r\n", ok, fail);
    }

    return (fail == 0 && ok > 0);
}

} // namespace arcana
