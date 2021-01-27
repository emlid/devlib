/*
 * Copyright 2017 resin.io
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 /**
 * Unmount Disk implementation based on unmount_disk function from RPI-Imager
 * https://github.com/raspberrypi/rpi-imager/blob/qml/dependencies/mountutils/src/darwin/functions.cpp
 * Anohin Igor <igor.anohin@emlid.com>, 2021
 */

#include "macos_utils.h"

#include <type_traits>

#include <DiskArbitration/DiskArbitration.h>


namespace {
    struct UnmountRunLoopContext
    {
        macos_utils::UnmountResult result = macos_utils::UnmountResult::Undefined;
    };


    macos_utils::UnmountResult convertDissenterToUnmountResult(DADissenterRef dissenter)
    {
        DAReturn status = DADissenterGetStatus(dissenter);
        if (status == kDAReturnBadArgument || status == kDAReturnNotFound) {
            qCDebug(macos_utils::macxlog()) << "Invalid drive";
            return macos_utils::UnmountResult::InvalidDriveError;
        } else if (status == kDAReturnNotPermitted || status == kDAReturnNotPrivileged) {
            qCDebug(macos_utils::macxlog()) << "Access denied";
            return macos_utils::UnmountResult::AccessDeniedError;
        } else {
            qCDebug(macos_utils::macxlog()) << "Unknown dissenter status";
            return macos_utils::UnmountResult::GeneralError;
        }
    }


    void unmountCallback(DADiskRef disk, DADissenterRef dissenter, void * ctx)
    {
        Q_UNUSED(disk)

        qCDebug(macos_utils::macxlog()) << "Unmount callback";
        UnmountRunLoopContext * context = reinterpret_cast<UnmountRunLoopContext *>(ctx);
        if (dissenter) {
            qCDebug(macos_utils::macxlog()) << "Unmount dissenter";
            context->result = convertDissenterToUnmountResult(dissenter);
        } else {
            qCDebug(macos_utils::macxlog()) << "Unmount success";
            context->result = macos_utils::UnmountResult::Success;
        }
        CFRunLoopStop(CFRunLoopGetCurrent());
    }


    bool waitForRunningLoop(const UnmountRunLoopContext & context)
    {
        // Wait for the run loop: Run with a timeout of 500ms (0.5s),
        // and don't terminate after only handling one resource.
        // NOTE: As the unmount callback gets called *before* the runloop can
        // be started here when there's no device to be unmounted or
        // the device has already been unmounted, the loop would
        // hang indefinitely until stopped manually otherwise.
        // Here we repeatedly run the loop for a given time, and stop
        // it at some point if it hasn't gotten anywhere, or if there's
        // nothing to be unmounted, or a dissent has been caught before the run.
        // This way we don't have to manage state across callbacks.
        for (auto attempt = 0; attempt < 10; attempt++) {
            // See https://developer.apple.com/reference/corefoundation/1541988-cfrunloopruninmode
            SInt32 status = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
            // Stop starting the runloop once it's been manually stopped
            if ((status == kCFRunLoopRunStopped) || (status == kCFRunLoopRunFinished)) {
                return true;
            }
            // Bail out if DADiskUnmount caught a dissent and
            // thus returned before the runloop even started
            if (context.result != macos_utils::UnmountResult::Undefined) {
                qCDebug(macos_utils::macxlog()) << "Runloop dry";
                return true;
            }
            // Bail out if the runloop is timing out, but not getting anywhere
        }

        return false;
    }

}


namespace macos_utils {

    UnmountResult unmountDiskWithRunLoop(const char * device)
    {
        using session_uptr = std::unique_ptr<std::remove_pointer<DASessionRef>::type,
                                                   std::function<void(DASessionRef)>>;
        using disk_uptr = std::unique_ptr<std::remove_pointer<DADiskRef>::type,
                                                   std::function<void(DADiskRef)>>;

        // Create a session object
        qCDebug(macxlog()) << "Creating DA session";
        session_uptr session(DASessionCreate(kCFAllocatorDefault),
                             [](DASessionRef session) {
                                 if (session != nullptr) {
                                     CFRelease(session);
                                 }
                             });

        if (!session) {
            qCDebug(macxlog()) << "Session couldn't be created";
            return UnmountResult::GeneralError;
        }

        // Get a disk object from the disk path
        qCDebug(macxlog()) << "Getting disk object";

        disk_uptr disk(DADiskCreateFromBSDName(kCFAllocatorDefault, session.get(), device),
                       [](DADiskRef disk) {
                           if (disk != nullptr) {
                               CFRelease(disk);
                           }
                       });
        if (!disk) {
            qCDebug(macxlog()) << "Disk couldn't be created";
            return UnmountResult::GeneralError;
        }

        UnmountRunLoopContext context;
        // Unmount, and then eject from the unmount callback
        qCDebug(macxlog()) << "Unmounting";
        DADiskUnmount(
            disk.get(), kDADiskUnmountOptionWhole | kDADiskUnmountOptionForce, unmountCallback, &context);

        // Schedule a disk arbitration session
        qCDebug(macxlog()) << "Schedule session on run loop";
        DASessionScheduleWithRunLoop(session.get(), CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

        qCDebug(macxlog()) << "Waiting run loop";
        if (!waitForRunningLoop(context)) {
            qCDebug(macxlog()) << "Runloop stall";
            context.result = UnmountResult::RunloopStallError;
        }

        // Clean up the session
        qCDebug(macxlog()) << "Releasing session & disk object";
        DASessionUnscheduleFromRunLoop(session.get(), CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        return context.result;
    }

}

