#ifndef NATIVE_H
#define NATIVE_H

#include <QtCore>

#include <tuple>
#include <memory>
#include <vector>


namespace devlib {
    namespace native {
        struct LockHandle {
            virtual ~LockHandle() = default;
        };

        auto umountPartition(QString const& mntpt)
            -> std::unique_ptr<LockHandle>;

        bool umountDisk(QString const & devicePath);

        bool mount(QString const& dev, QString const& path);

        auto mntptsList(void) -> std::vector<QString>;

        auto mntptsForPartition(QString const& devFilePath)
            -> std::vector<std::pair<QString, QString>>;

        auto requestUsbDeviceList(void)
            -> std::vector<std::tuple<int, int, QString, QString>>;

        auto devicePartitions(QString const& deviceName)
            -> std::vector<std::tuple<QString, QString>>;

        namespace io {
            struct FileHandle {
                virtual ~FileHandle() = default;
            };

            auto read(FileHandle* handle, char* data, qint64 sz) -> qint64;
            auto write(FileHandle* handle, char const* data, qint64 sz) -> qint64;

            auto open(char const* filename)
                -> std::unique_ptr<FileHandle>;

            bool seek(FileHandle*, qint64 pos);

            void sync(FileHandle* handle);
        }
    }
}


#endif // NATIVE_H
