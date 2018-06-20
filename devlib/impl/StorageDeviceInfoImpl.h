#ifndef STORAGEDEVICEINFOIMPL_H
#define STORAGEDEVICEINFOIMPL_H

#include "../StorageDeviceInfo.h"
#include "../Mountpoint.h"
#include "../Partition.h"

namespace devlib {
    namespace impl {
        class StorageDeviceInfoImpl;
        using PartitionFactory_t =
            std::function<
                std::unique_ptr<devlib::IPartition>(QString const&, QString const&)
            >;

        using MountpointFactory_t = std::function<
            std::unique_ptr<IMountpoint>(QString const& name)
        >;
    }
}


class devlib::impl::StorageDeviceInfoImpl : public devlib::IStorageDeviceInfo
{
public:
    StorageDeviceInfoImpl(
            int vid, int pid,
            QString const& filePath, QString const& usbPortPath,
            impl::PartitionFactory_t  partitionFactory,
            impl::MountpointFactory_t mntptFactory);

    virtual ~StorageDeviceInfoImpl(void) = default;

private:
    virtual int vid_core(void) const noexcept override { return _vid; }
    virtual int pid_core(void) const noexcept override { return _pid; }

    virtual auto filePath_core(void) const noexcept
        -> QString  override { return _filePath; }

    auto usbPortPath_core(void) const noexcept
        -> QString  override { return _usbPortPath; }

    virtual auto mountpoints_core(void) const
        -> std::vector<std::unique_ptr<IMountpoint>> override;

    virtual auto partitions_core(void) const
        -> std::vector<std::unique_ptr<IPartition>> override;

    int _vid, _pid;
    QString _filePath, _usbPortPath;
    impl::PartitionFactory_t  _partitionFactory;
    impl::MountpointFactory_t _mountpointFactory;
};

#endif // STORAGEDEVICEINFOIMPL_H
