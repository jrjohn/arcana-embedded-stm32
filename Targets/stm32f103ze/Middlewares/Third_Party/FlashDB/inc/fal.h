/*
 * Minimal FAL (Flash Abstraction Layer) API for FlashDB.
 * Backed by FatFS files on SD card — implemented in SdFalAdapter.cpp.
 */

#ifndef _FAL_H_
#define _FAL_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAL_DEV_NAME_MAX    16

struct fal_flash_dev {
    char name[FAL_DEV_NAME_MAX];
    uint32_t addr;
    size_t len;
    size_t blk_size;
};

struct fal_partition {
    char name[FAL_DEV_NAME_MAX];
    char flash_name[FAL_DEV_NAME_MAX];
    long offset;
    size_t len;
};

int fal_init(void);
const struct fal_partition  *fal_partition_find(const char *name);
const struct fal_flash_dev  *fal_flash_device_find(const char *name);
int fal_partition_read(const struct fal_partition *part, uint32_t addr, uint8_t *buf, size_t size);
int fal_partition_write(const struct fal_partition *part, uint32_t addr, const uint8_t *buf, size_t size);
int fal_partition_erase(const struct fal_partition *part, uint32_t addr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _FAL_H_ */
