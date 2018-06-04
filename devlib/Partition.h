#ifndef PARTITION_H
#define PARTITION_H

#include "Mountpoint.h"

namespace devlib {
    class IPartition;
}

class devlib::IPartition
{
public:
    virtual ~IPartition(void) {}

    auto filePath(void) const noexcept { return filePath_core(); }
    auto label(void) const noexcept { return label_core(); }
    auto mount(QString const& path) { return mount_core(path);}
    auto mountpoints(void) { return mountpoints_core(); }

private:
    virtual auto filePath_core(void) const noexcept -> QString = 0;
    virtual auto label_core(void) const noexcept -> QString = 0;

    virtual auto mount_core(QString const& path)
        -> std::unique_ptr<IMountpoint> = 0;

    virtual auto mountpoints_core(void)
        -> std::vector<std::unique_ptr<IMountpoint>> = 0;
};

#endif // PARTITION_H
