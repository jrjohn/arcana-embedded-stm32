#pragma once

#include "ServiceTypes.hpp"

namespace arcana {
namespace io {

class IoService {
public:
    virtual ~IoService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus start() = 0;

    virtual bool isUploadRequested() const = 0;
    virtual void clearUploadRequest() = 0;

    virtual bool isCancelRequested() const = 0;
    virtual void clearCancelRequest() = 0;

    virtual bool isFormatRequested() const = 0;
    virtual void clearFormatRequest() = 0;

    virtual void armCancel() = 0;
    virtual void disarmCancel() = 0;

protected:
    IoService() {}
};

} // namespace io
} // namespace arcana
