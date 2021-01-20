#include "StorageDeviceFileImpl.h"


namespace {

    bool umountDevice(const std::shared_ptr<devlib::IStorageDeviceInfo> deviceInfo)
    {
        if (devlib::native::umountDisk(deviceInfo->filePath())) {
            return true;
        }

        auto mntpts = deviceInfo->mountpoints();
        for (auto const & mntpt : mntpts) {
            auto mntptLock = mntpt->umount();
            if (!mntptLock->locked()) {
                return false;
            }
        }

        return true;
    }

}


devlib::impl::StorageDeviceFileImpl::
    StorageDeviceFileImpl(QString const& deviceFilename,
                          std::shared_ptr<IStorageDeviceInfo> storageDeviceInfo)
    : _deviceFilename(deviceFilename),
      _deviceInfo(std::move(storageDeviceInfo))
{ }


bool devlib::impl::StorageDeviceFileImpl::open_core(OpenMode mode)
{
    Q_UNUSED(mode);
    // first: unmount disk
    if (!umountDevice(_deviceInfo)) {
         return false;
    }
    // second: open file handle
    _fileHandle = native::io::open(_deviceFilename.toStdString().data());

    QFile::setOpenMode(mode);

    return _fileHandle != nullptr;
}


void devlib::impl::StorageDeviceFileImpl::close_core(void)
{
    QFile::setOpenMode(QIODevice::NotOpen);
    _fileHandle.reset();
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
