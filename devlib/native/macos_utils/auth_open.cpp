/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

 /**
 * authOpenStorageDevice implementation based on authOpen function from RPI-Imager
 * https://github.com/raspberrypi/rpi-imager/blob/qml/mac/macfile.cpp
 * Anohin Igor <igor.anohin@emlid.com>, 2021
 */

#include "macos_utils.h"

#include <sys/socket.h>

#include <security/Authorization.h>


namespace {
    using authorization_uptr = std::unique_ptr<std::remove_pointer<AuthorizationRef>::type,
                                               std::function<void(AuthorizationRef)>>;


    auto createFileAuthorization(const QByteArray & filename) -> authorization_uptr {
        QByteArray right = "sys.openfile.readwrite." + filename;
        AuthorizationItem item = {right, 0, nullptr, 0};
        AuthorizationFlags flags = kAuthorizationFlagInteractionAllowed | kAuthorizationFlagExtendRights
                                   | kAuthorizationFlagPreAuthorize;
        AuthorizationRights rights = {1, &item};

        auto authsDestructor = [](AuthorizationRef authRef) {
            if (authRef != nullptr) {
                AuthorizationFree(authRef, 0);
            }
        };

        AuthorizationRef authRef = nullptr;
        if (AuthorizationCreate(&rights, nullptr, flags, &authRef) != 0) {
            return nullptr;
        }

        return authorization_uptr{authRef, authsDestructor};
    }


    void handleChildProcess(const char * filename, int * pipe, int * stdinpipe)
    {
        // Close unused pipes
        ::close(pipe[0]);
        ::close(stdinpipe[1]);

        // Assign pipes with std in/out
        ::dup2(pipe[1], STDOUT_FILENO);
        ::dup2(stdinpipe[0], STDIN_FILENO);

        const char * cmd = "/usr/libexec/authopen";
        QByteArray mode = QByteArray::number(O_RDWR);
        ::execl(cmd, cmd, "-stdoutpipe", "-extauth", "-o", mode.data(), filename, nullptr);

        // Exit with error code -1 in case of execl error
        ::exit(-1);
    }


    auto receiveFdFromChildProcess(const int & fromChildToParentPipe) -> int
    {
        const size_t bufSize = CMSG_SPACE(sizeof(int));
        char buf[bufSize];
        struct iovec io_vec[1];
        io_vec[0].iov_base = buf;
        io_vec[0].iov_len = bufSize;
        const size_t cmsgSize = CMSG_SPACE(sizeof(int));
        char cmsg[cmsgSize];

        struct msghdr msg = {0};
        msg.msg_iov = io_vec;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsg;
        msg.msg_controllen = cmsgSize;

        ssize_t size;
        do {
            size = recvmsg(fromChildToParentPipe, &msg, 0);
        } while (size == -1 && errno == EINTR);

        if (size > 0) {
            struct cmsghdr * chdr = CMSG_FIRSTHDR(&msg);
            if (chdr && chdr->cmsg_type == SCM_RIGHTS) {
                qCDebug(macos_utils::macxlog()) << "SCMRIGHTS";
                return *((int *)(CMSG_DATA(chdr)));
            } else {
                qCDebug(macos_utils::macxlog()) << "NOT SCMRIGHTS";
                return -1;
            }
        }
        return -2;
    }


    auto waitForChildProcessToFinish(const int & processId) -> int
    {
        pid_t wpid;
        int status;

        do {
            wpid = ::waitpid(processId, &status, 0);
        } while (wpid == -1 && errno == EINTR);

        if (wpid == -1) {
            qCDebug(macos_utils::macxlog()) << "waitpid() failed executing authopen";
            return -2;
        }
        if (WEXITSTATUS(status)) {
            qCDebug(macos_utils::macxlog()) << "authopen returned failure code" << WEXITSTATUS(status);
            return -3;
        }

        return 0;
    }

}


namespace macos_utils {
    auto authOpenStorageDevice(const QByteArray & filename) -> int
    {
        auto authorizationUPtr = createFileAuthorization(filename);
        if (!authorizationUPtr) {
            return -1;
        }

        AuthorizationExternalForm externalForm;
        if (AuthorizationMakeExternalForm(authorizationUPtr.get(), &externalForm) != 0) {
            return -1;
        }

        int fd = -1;

        int pipe[2];
        int stdinpipe[2];
        ::socketpair(AF_UNIX, SOCK_STREAM, 0, pipe);
        ::pipe(stdinpipe);
        auto pid = ::fork();
        if (pid == 0) {
            handleChildProcess(filename.data(), pipe, stdinpipe);
        } else {
            // Close unused pipes
            ::close(pipe[1]);
            ::close(stdinpipe[0]);

            // Send AuthorizationExternalForm structure, because "extauth" was used in authopen
            ::write(stdinpipe[1], externalForm.bytes, sizeof(externalForm.bytes));
            ::close(stdinpipe[1]);

            fd = receiveFdFromChildProcess(pipe[0]);

            auto exitCode = waitForChildProcessToFinish(pid);
            if (exitCode != 0) {
                return exitCode;
            }

            qCDebug(macxlog()) << "fd received:" << fd;
        }
        return fd;
    }
}
