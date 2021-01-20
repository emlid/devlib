#ifndef STORAGEDEVICEFILE_H
#define STORAGEDEVICEFILE_H

#include <QtCore>
#include <cassert>

namespace devlib {
    class IStorageDeviceFile;
}

class devlib::IStorageDeviceFile : public QFile
{
    Q_OBJECT
public:
    virtual ~IStorageDeviceFile(void) = default;

    bool open(OpenMode mode) override final {
        Q_ASSERT(!isOpen());
        return open_core(mode);
    }

    bool authOpen(OpenMode mode) {
        Q_ASSERT(!isOpen());
        return open_core(mode, true);
    }

    void close(void) override final { close_core(); }
    auto fileName() const
        -> QString override final { return fileName_core(); }

    void sync() { return sync_core(); }

    auto seek(qint64 pos) -> bool override final {
        Q_ASSERT(pos >= 0);
        Q_ASSERT(isOpen());

        return seek_core(pos);
    }

protected:
    virtual auto readData(char* data, qint64 len)
        -> qint64 override final
    {
        Q_ASSERT(len > 0);
        Q_ASSERT(data);
        Q_ASSERT(isOpen() && isReadable());

        return readData_core(data, len);
    }

    virtual auto writeData(char const* data, qint64 len)
        -> qint64 override final
    {
        Q_ASSERT(len > 0);
        Q_ASSERT(data);
        Q_ASSERT(isOpen() && isWritable());

        return writeData_core(data, len);
    }

private:
    virtual bool open_core(OpenMode mode, bool withAuthorization = false) = 0;
    virtual void close_core(void) = 0;
    virtual void sync_core() = 0;

    virtual auto readData_core(char* data, qint64 len) -> qint64 = 0;
    virtual auto writeData_core(char const* data, qint64 len) -> qint64 = 0;
    virtual auto fileName_core() const -> QString = 0;
    virtual auto seek_core(qint64) -> bool = 0;
};

#endif // STORAGEDEVICEFILE_H
