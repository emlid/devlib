#ifndef STORAGEDEVICESERVICE_H
#define STORAGEDEVICESERVICE_H

#include "StorageDeviceInfo.h"
#include "StorageDeviceFile.h"
#include <memory>

namespace devlib {
    class StorageDeviceService;
}

class devlib::StorageDeviceService
{
public:
    virtual ~StorageDeviceService(void) = default;

    static auto instance(void) {
        return std::make_unique<devlib::StorageDeviceService>();
    }

    virtual auto getAvailableStorageDevices(void)
        -> std::vector<std::unique_ptr<IStorageDeviceInfo>>;

    static auto makeStorageDeviceFile(
            QString const& deviceFileName,
            std::shared_ptr<devlib::IStorageDeviceInfo> deviceInfo
    ) -> std::unique_ptr<IStorageDeviceFile>;

    StorageDeviceService(void);
};


#endif // STORAGEDEVICESERVICE_H
