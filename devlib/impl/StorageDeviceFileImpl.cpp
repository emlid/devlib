#include "StorageDeviceFileImpl.h"


devlib::impl::StorageDeviceFileImpl::
    StorageDeviceFileImpl(QString const& deviceFilename,
                          std::shared_ptr<IStorageDeviceInfo> storageDeviceInfo)
    : _deviceFilename(deviceFilename),
      _deviceInfo(std::move(storageDeviceInfo))
{ }


bool devlib::impl::StorageDeviceFileImpl::open_core(OpenMode mode, bool withAuthorization)
{
    Q_UNUSED(mode);
    // first: unmount disk
    if (!devlib::native::umountDisk(_deviceInfo->filePath())) {
        auto mntpts = _deviceInfo->mountpoints();
        _mntptsLocks.clear();
        for (auto const & mntpt : mntpts) {
            auto mntptLock = mntpt->umount();
            if (mntptLock->locked()){
                _mntptsLocks.push_back(std::move(mntptLock));
            } else {
                return false;
            }
        }
    }
    // second: open file handle
    if (withAuthorization) {
        _fileHandle = native::io::authOpen(_deviceFilename.toStdString().data());
    } else {
        _fileHandle = native::io::open(_deviceFilename.toStdString().data());
    }

    QFile::setOpenMode(mode);

    return _fileHandle != nullptr;
}


void devlib::impl::StorageDeviceFileImpl::close_core(void)
{
    QFile::setOpenMode(QIODevice::NotOpen);
    _fileHandle.reset();
    _mntptsLocks.clear();
}


auto devlib::impl::StorageDeviceFileImpl::
    readData_core(char* data, qint64 len) -> qint64
{
    return native::io::read(_fileHandle.get(), data, len);
}


auto devlib::impl::StorageDeviceFileImpl::
    writeData_core(const char *data, qint64 len) -> qint64
{
    return native::io::write(_fileHandle.get(), data, len);
}


bool devlib::impl::StorageDeviceFileImpl::seek_core(qint64 pos)
{
    return native::io::seek(_fileHandle.get(), pos);
}


void devlib::impl::StorageDeviceFileImpl::sync_core(void)
{
    devlib::native::io::sync(_fileHandle.get());
}
