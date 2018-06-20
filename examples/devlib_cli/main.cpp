#include <QCoreApplication>
#include "devlib.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    auto service = devlib::StorageDeviceService::instance();
    Q_ASSERT(service.get());

    auto devices = service->getAvailableStorageDevices();

    for (auto const& device : devices) {
        qInfo() << "device" << '\n'
                << "+ vid: " << device->vid() << '\n'
                << "+ pid: " << device->pid() << '\n'
                << "+ fsPath: " << device->filePath() << '\n'
                << "+ usbPortPath: " << device->usbPortPath();

        qInfo() << "\n + mntpts:";
        for (auto const& mntpt : device->mountpoints()) {
            qInfo() << "  +- name: " << mntpt->fsPath();
        }

        qInfo() << "\n + partitions(volumes):";
        for (auto const& part : device->partitions()) {
            qInfo() << "   +- name: " << part->filePath() << '\n'
                    << "  +- label: " << part->label();
            qInfo() << "   +- Mounpoints:";

            for (auto const& mntpt : part->mountpoints()) {
                qInfo() << "      +- " << mntpt->fsPath();
            }

            qInfo() << "";
        }
    }

    return a.exec();
}
