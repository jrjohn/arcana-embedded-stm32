/**
 * @file ff.h (host test stub)
 *
 * Tiny FatFs surface — enough for RegistrationServiceImpl, AtsStorageServiceImpl,
 * and any other code-under-test that needs f_open / f_read / f_write / f_close /
 * f_unlink / f_rename / f_lseek / f_size / f_opendir / f_readdir / f_closedir.
 *
 * Backed by an in-memory file table (vector of {name, contents}). Tests
 * configure files via test_ff_helpers.hpp helpers.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef DWORD          FSIZE_t;

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS   0x10
#define FA_OPEN_APPEND   0x30

/* Opaque handle — points at the in-memory file entry the stub created.
 * The fields exposed (err, obj.objsize, fptr) match the names used by
 * production code so includers compile cleanly. */
/* Forward-declared FATFS — exposed via FIL::obj.fs (production code reads
 * fs->n_fats to choose between safe-truncate and skip-truncate paths). */
typedef struct {
    int n_fats;     /* host stub: 2 → exercise the truncate-safe branch */
    int dummy;
} FATFS;

typedef struct {
    void*    _entry;        /* internal: pointer to the FileEntry */
    DWORD    fptr;          /* current file position */
    BYTE     err;           /* sticky error flag (production checks this) */
    BYTE     flag;          /* open flags */
    struct {
        FSIZE_t objsize;
        FATFS*  fs;          /* points at host stub FATFS */
    } obj;
} FIL;

typedef struct {
    void* _dir;             /* internal */
    int   _index;           /* internal */
} DIR;

typedef struct {
    FSIZE_t fsize;
    char    fname[64];
    BYTE    fattrib;
} FILINFO;

#define AM_DIR  0x10

FRESULT f_open(FIL* fp, const char* path, BYTE mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek(FIL* fp, FSIZE_t ofs);
FRESULT f_sync(FIL* fp);
FRESULT f_truncate(FIL* fp);
FRESULT f_unlink(const char* path);
FRESULT f_rename(const char* path_old, const char* path_new);
FRESULT f_expand(FIL* fp, FSIZE_t fsz, BYTE opt);

FRESULT f_opendir(DIR* dp, const char* path);
FRESULT f_closedir(DIR* dp);
FRESULT f_readdir(DIR* dp, FILINFO* fno);
FRESULT f_stat(const char* path, FILINFO* fno);

FRESULT f_mount(FATFS* fs, const char* path, BYTE opt);

/* MKFS — used by SdBenchmarkServiceImpl::texfat_format() */
typedef struct {
    BYTE   fmt;
    BYTE   n_fat;
    UINT   align;
    UINT   n_root;
    DWORD  au_size;
} MKFS_PARM;
#define FM_EXFAT 0x04
#define FM_FAT32 0x02
#define FM_FAT   0x01
FRESULT f_mkfs(const char* path, const MKFS_PARM* opt, void* work, UINT len);
FRESULT f_getfree(const char* path, DWORD* nclst, FATFS** fatfs);

FSIZE_t f_size(FIL* fp);
DWORD   f_tell(FIL* fp);

/* ── Test-only helpers (defined in ff_host_stub.cpp) ─────────────────────── */

/** Wipe all in-memory files. Call from test fixtures. */
void test_ff_reset(void);

/** Create / replace a file with the given bytes. */
void test_ff_create(const char* path, const uint8_t* data, UINT len);

/** Read a file's bytes (out_len truncated to bufSize). Returns FR_OK if found. */
FRESULT test_ff_read(const char* path, uint8_t* buf, UINT bufSize, UINT* out_len);

/** Returns 1 if file exists, 0 otherwise. */
int test_ff_exists(const char* path);

/** Make the next N read calls fail with FR_DISK_ERR (counter decrements). */
void test_ff_fail_read(int count);

/** Make the next N write calls fail with FR_DISK_ERR. */
void test_ff_fail_write(int count);

/** Make the next N lseek calls fail with FR_INT_ERR. */
void test_ff_fail_lseek(int count);

/** Make the next N sync calls fail with FR_DISK_ERR. */
void test_ff_fail_sync(int count);

/** Override the FATFS n_fats field used by FIL.obj.fs (default 2). */
void test_ff_set_n_fats(int n);

#ifdef __cplusplus
}
#endif
