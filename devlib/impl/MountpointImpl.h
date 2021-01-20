#ifndef MOUNTPOINTIMPL_H
#define MOUNTPOINTIMPL_H

#include "../Mountpoint.h"
#include "../native/native.h"

namespace devlib {
    namespace impl {
        class MountpointImpl;
        class MountpointLockImpl;
        using MountpointLockFactory_t = std::function<
            std::unique_ptr<IMountpointLock>(std::unique_ptr<native::LockHandle>)
        >;

    }
}

class devlib::impl::MountpointImpl : public devlib::IMountpoint
{
public:
    MountpointImpl(QString const& fsPath,
                   impl::MountpointLockFactory_t locksFactory)
        : _fsPath(fsPath), _locksFactory(locksFactory)
    { }

    virtual ~MountpointImpl(void) override = default;

private:
    virtual bool isMounted_core(void) const override {
        return !_fsPath.isEmpty();
    }

    virtual auto fsPath_core(void) const
        -> QString const& override { return _fsPath; }

    virtual auto umount_core(void)
        -> std::unique_ptr<devlib::IMountpointLock> override
    { return _locksFactory(native::umountPartition(_fsPath)); }

    QString _fsPath;
    impl::MountpointLockFactory_t _locksFactory;
};



class devlib::impl::MountpointLockImpl : public devlib::IMountpointLock
{
public:
    MountpointLockImpl(std::unique_ptr<native::LockHandle> handle)
        : _mntptHandle(std::move(handle))
    { }

    virtual ~MountpointLockImpl(void) = default;

private:
    virtual bool locked_core(void) const override {
        return _mntptHandle != nullptr;
    }

    virtual void release_core(void) override {
        return _mntptHandle.reset();
    }

    std::unique_ptr<native::LockHandle> _mntptHandle;
};


#endif // MOUNTPOINTIMPL_H
