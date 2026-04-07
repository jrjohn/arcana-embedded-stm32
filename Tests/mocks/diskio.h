/**
 * @file diskio.h (host test stub)
 *
 * Minimal stub — production AtsStorageServiceImpl.cpp #includes "diskio.h"
 * but doesn't actually call any of its functions in the host-testable code
 * paths. Provide empty types/declarations so the include resolves.
 */
#pragma once

#include "ff.h"

#define STA_NOINIT  0x01
#define STA_NODISK  0x02
#define STA_PROTECT 0x04

typedef BYTE DSTATUS;

typedef enum {
    RES_OK = 0,
    RES_ERROR,
    RES_WRPRT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;

DSTATUS disk_status(BYTE pdrv);
DSTATUS disk_initialize(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);

#define CTRL_SYNC          0
#define GET_SECTOR_COUNT   1
#define GET_SECTOR_SIZE    2
#define GET_BLOCK_SIZE     3
#define CTRL_TRIM          4
