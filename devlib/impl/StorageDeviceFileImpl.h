#ifndef STORAGEDEVICEFILEIMPL_H
#define STORAGEDEVICEFILEIMPL_H

#include "../StorageDeviceFile.h"
#include "../StorageDeviceInfo.h"
#include "../native/native.h"

namespace devlib {
    namespace impl {
        class StorageDeviceFileImpl;
    }
}

class devlib::impl::StorageDeviceFileImpl : public devlib::IStorageDeviceFile
{
public:
    using FileHandle = native::io::FileHandle;

    StorageDeviceFileImpl(
            QString const& deviceFilename,
            std::shared_ptr<devlib::IStorageDeviceInfo> storageDeviceInfo
    );

    virtual ~StorageDeviceFileImpl(void) { close_core(); }
private:
    virtual bool open_core(OpenMode mode, bool withAuthorization = false) override;
    virtual void close_core(void) override;

    virtual auto readData_core(char* data, qint64 len)
        -> qint64 override;

    virtual auto writeData_core(char const* data, qint64 len)
        -> qint64 override;

    virtual auto fileName_core(void) const
        -> QString override { return _deviceFilename; }

    virtual auto seek_core(qint64)
        -> bool override;

    virtual void sync_core(void) override;

    QString _deviceFilename;
    std::shared_ptr<devlib::IStorageDeviceInfo> _deviceInfo;

    std::unique_ptr<
        native::io::FileHandle
    > _fileHandle;
};

#endif // STORAGEDEVICEFILEIMPL_H
