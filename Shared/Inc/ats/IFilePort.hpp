/**
 * @file IFilePort.hpp
 * @brief Platform abstraction for file I/O
 *
 * Implementations: FatFsFilePort (STM32), VfsFilePort (ESP32), PosixFilePort (Linux).
 */

#ifndef ARCANA_ATS_IFILEPORT_HPP
#define ARCANA_ATS_IFILEPORT_HPP

#include <cstdint>

namespace arcana {
namespace ats {

/**
 * @brief Abstract file I/O interface
 *
 * All ArcanaTS file operations go through this interface.
 * Platform implementations wrap FatFS, ESP-IDF VFS, or POSIX.
 */
class IFilePort {
public:
    virtual ~IFilePort() {}

    /** @brief Open a file. mode: ATS_MODE_READ/WRITE/RW/CREATE bitmask */
    virtual bool open(const char* path, uint8_t mode) = 0;
    virtual bool close() = 0;

    /** @brief Read bytes. Returns bytes read, -1 on error */
    virtual int32_t read(uint8_t* buf, uint32_t size) = 0;

    /** @brief Write bytes. Returns bytes written, -1 on error */
    virtual int32_t write(const uint8_t* buf, uint32_t size) = 0;

    virtual bool seek(uint64_t offset) = 0;
    virtual bool sync() = 0;
    virtual uint64_t tell() = 0;
    virtual uint64_t size() = 0;

    /** @brief Truncate file at current position */
    virtual bool truncate() = 0;

    virtual bool isOpen() const = 0;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_IFILEPORT_HPP */
