#include "StorageDeviceInfoImpl.h"
#include "native/native.h"


devlib::impl::StorageDeviceInfoImpl::
    StorageDeviceInfoImpl(int vid, int pid,
                          QString const& filePath,
                          PartitionFactory_t partitionFactory,
                          MountpointFactory_t mntptFactory)
    : _vid(vid), _pid(pid),
      _filePath(filePath),
      _partitionFactory(partitionFactory),
      _mountpointFactory(mntptFactory)
{ }


auto devlib::impl::StorageDeviceInfoImpl::mountpoints_core(void) const
    -> std::vector<std::unique_ptr<IMountpoint>>
{
    auto mntpts = native::mntptsForPartition(_filePath);
    auto list = std::vector<
        std::unique_ptr<IMountpoint>
    >(mntpts.size());

    std::transform(mntpts.cbegin(), mntpts.cend(), list.begin(),
        [this] (auto const& mntpt) {
            return _mountpointFactory(mntpt.first);
        }
    );

    return list;
}


auto devlib::impl::StorageDeviceInfoImpl::partitions_core(void) const
    -> std::vector<std::unique_ptr<IPartition>>
{
    auto partitions = native::devicePartitions(_filePath);
    auto list = std::vector<
        std::unique_ptr<IPartition>
    >(partitions.size());

    std::transform(partitions.cbegin(), partitions.cend(), list.begin(),
        [this] (auto const& part) {
            return _partitionFactory(
                std::get<0>(part), std::get<1>(part)
            );
        }
    );

    return list;
}

