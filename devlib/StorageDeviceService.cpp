#include "StorageDeviceService.h"

#include "impl/MountpointImpl.h"
#include "impl/PartitionImpl.h"
#include "impl/StorageDeviceInfoImpl.h"
#include "impl/StorageDeviceFileImpl.h"
#include "native/native.h"


devlib::StorageDeviceService::StorageDeviceService()
{ }


auto devlib::StorageDeviceService::getAvailableStorageDevices(void)
    -> std::vector<std::unique_ptr<IStorageDeviceInfo>>
{
    auto mntptFactory = [] (auto const& mntptName) {
        auto mntptLockFactory = [] (auto handle) {
            return std::make_unique<
                impl::MountpointLockImpl
            >(std::move(handle));
        };

        return std::make_unique<impl::MountpointImpl>(
            mntptName, mntptLockFactory
        );
    };

    auto partitionFactory =
        [mntptFactory] (auto const& partName, auto const& partLabel) {
            return std::make_unique<impl::PartitionImpl>(
                partName, partLabel, mntptFactory
            );
        };

    auto storageDeviceInfoFactory =
        [partitionFactory, mntptFactory] (int vid, int pid, auto const& filePath, auto const& usbPortPath) {
            return std::make_unique<impl::StorageDeviceInfoImpl>(
                vid, pid, filePath, usbPortPath, partitionFactory, mntptFactory
            );
        };

    auto devsList = native::requestUsbDeviceList();
    auto storageDevicesList = std::vector<
            std::unique_ptr<IStorageDeviceInfo>
    >(devsList.size());

    std::transform(devsList.cbegin(), devsList.cend(), storageDevicesList.begin(),
        [&storageDeviceInfoFactory] (auto const& deviceInfo) {
            return storageDeviceInfoFactory(std::get<0>(deviceInfo),
                                            std::get<1>(deviceInfo),
                                            std::get<2>(deviceInfo),
                                            std::get<3>(deviceInfo));
        }
    );

    return storageDevicesList;
}


auto devlib::StorageDeviceService::makeStorageDeviceFile(
    const QString &deviceFileName,
    std::shared_ptr<IStorageDeviceInfo> deviceInfo
) -> std::unique_ptr<IStorageDeviceFile>
{
    return std::make_unique<impl::StorageDeviceFileImpl>(
        deviceFileName, std::move(deviceInfo)
    );
}
