/**
 * @file FatFsFilePort.hpp
 * @brief IFilePort implementation wrapping FatFS (FIL)
 *
 * Owns a FIL struct internally (static allocation).
 * 3-retry logic with FIL.err sticky flag clearing, matching SdFalAdapter pattern.
 */

#ifndef ARCANA_FATFS_FILE_PORT_HPP
#define ARCANA_FATFS_FILE_PORT_HPP

#include "ats/IFilePort.hpp"
#include "ff.h"

namespace arcana {
namespace ats {

class FatFsFilePort : public IFilePort {
public:
    FatFsFilePort() : mIsOpen(false) {}

    bool open(const char* path, uint8_t mode) override;
    bool close() override;
    int32_t read(uint8_t* buf, uint32_t size) override;
    int32_t write(const uint8_t* buf, uint32_t size) override;
    bool seek(uint32_t offset) override;
    bool sync() override;
    uint32_t tell() override;
    uint32_t size() override;
    bool truncate() override;
    bool isOpen() const override { return mIsOpen; }

private:
    FIL  mFil;
    bool mIsOpen;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_FATFS_FILE_PORT_HPP */
