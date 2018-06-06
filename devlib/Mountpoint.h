#ifndef MOUNTPOINT_H
#define MOUNTPOINT_H

#include <QtCore>
#include <memory>

namespace devlib {
    class IMountpoint;
    class IMountpointLock;
}

class devlib::IMountpoint
{
public:
    virtual ~IMountpoint(void) = default;

    auto isMounted(void) const { return isMounted_core(); }
    auto fsPath(void) const { return fsPath_core(); }
    auto umount(void) { return umount_core(); }

private:
    virtual bool isMounted_core(void) const = 0;
    virtual auto fsPath_core(void) const
        -> QString const& = 0;

    virtual auto umount_core(void)
        -> std::unique_ptr<IMountpointLock> = 0;
};


class devlib::IMountpointLock
{
public:
    auto locked(void) const { return locked_core(); }
    auto release(void) { if (locked()) release_core(); }

    virtual ~IMountpointLock(void) {}

private:
    virtual bool locked_core(void) const = 0;
    virtual void release_core(void) = 0;
};


#endif // MOUNTPOINT_H
