#ifndef STORAGEDEVICEINFO_H
#define STORAGEDEVICEINFO_H

#include "Partition.h"
#include "Mountpoint.h"

namespace devlib {
    class IStorageDeviceInfo;
}

class devlib::IStorageDeviceInfo
{
public:
    virtual ~IStorageDeviceInfo(void) = default;

    auto vid(void) const noexcept { return vid_core(); }
    auto pid(void) const noexcept { return pid_core(); }

    auto filePath(void) const noexcept { return filePath_core(); }
    auto mountpoints(void) const { return mountpoints_core(); }
    auto partitions(void) const { return partitions_core(); }

private:
    virtual int vid_core(void) const noexcept = 0;
    virtual int pid_core(void) const noexcept = 0;

    virtual auto filePath_core(void) const noexcept
        -> QString = 0;

    virtual auto mountpoints_core(void) const
        -> std::vector<std::unique_ptr<IMountpoint>> = 0;

    virtual auto partitions_core(void) const
        -> std::vector<std::unique_ptr<IPartition>> = 0;
};

#endif // STORAGEDEVICEINFO_H
