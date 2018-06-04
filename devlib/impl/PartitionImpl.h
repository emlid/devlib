#ifndef PARTITIONIMPL_H
#define PARTITIONIMPL_H

#include "../Partition.h"
#include "../Mountpoint.h"

namespace devlib {
    namespace impl {
        class PartitionImpl;
        using MountpointFactory_t = std::function<
            std::unique_ptr<IMountpoint>(QString const& name)
        >;
    }
}


class devlib::impl::PartitionImpl : public devlib::IPartition
{
public:
    PartitionImpl(QString const& filePath,
                  QString const& label,
                  impl::MountpointFactory_t mntptFactory);

    virtual ~PartitionImpl(void) = default;

private:
    virtual auto filePath_core(void) const noexcept
        -> QString override { return _filePath; }

    virtual auto label_core(void) const noexcept
        -> QString override { return _label; }

    virtual auto mount_core(const QString &path)
        -> std::unique_ptr<devlib::IMountpoint> override;

    virtual auto mountpoints_core(void)
        -> std::vector<std::unique_ptr<devlib::IMountpoint>> override;

    QString _filePath;
    QString _label;
    impl::MountpointFactory_t _mntptFactory;
};

#endif // PARTITIONIMPL_H
