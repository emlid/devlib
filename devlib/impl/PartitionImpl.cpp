#include "PartitionImpl.h"
#include "native/native.h"

devlib::impl::PartitionImpl::PartitionImpl(const QString &filePath,
                                           const QString &label,
                                           MountpointFactory_t mntptFactory)
    : _filePath(filePath),
      _label(label),
      _mntptFactory(mntptFactory)
{ }


auto devlib::impl::PartitionImpl::mount_core(const QString &path)
    -> std::unique_ptr<IMountpoint>
{
    return native::mount(_filePath, path) ?
        _mntptFactory(path) : _mntptFactory("");
}


auto devlib::impl::PartitionImpl::mountpoints_core(void)
    ->std::vector<std::unique_ptr<IMountpoint>>
{
    auto mntpts = native::mntptsForPartition(_filePath);
    auto list = std::vector<
        std::unique_ptr<IMountpoint>
    >(mntpts.size());

    std::transform(mntpts.cbegin(), mntpts.cend(), list.begin(),
        [this] (auto const& mntpt) {
            return _mntptFactory(mntpt.first);
        }
    );

    return list;
}
