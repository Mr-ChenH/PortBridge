// Keep Winsock2 before headers that can include windows.h.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on
#include "portbridge/session_controller.hpp"
#include "session/recorder.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QThread>
#include <cstring>
#include <iostream>
#include <memory>
using namespace portbridge;
HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr),
       releaseGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
using DnsFn = int(WSAAPI *)(const char *, const char *, const addrinfo *, addrinfo **);
DnsFn originalDns = nullptr;
using WriteFn = BOOL(WINAPI *)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
WriteFn originalWrite = nullptr;
int WSAAPI blockedDns(const char *node, const char *service, const addrinfo *hints, addrinfo **result) {
    if (node && std::strcmp(node, "portbridge-stall.invalid") == 0) {
        SetEvent(entered);
        WaitForSingleObject(releaseGate, INFINITE);
        *result = nullptr;
        return EAI_AGAIN;
    }
    return originalDns(node, service, hints, result);
}
BOOL WINAPI blockedWrite(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written,
                         LPOVERLAPPED overlapped) {
    if (size >= 8 && std::memcmp(buffer, "PBCAP001", 8) == 0) {
        SetEvent(entered);
        WaitForSingleObject(releaseGate, INFINITE);
    }
    return originalWrite(file, buffer, size, written, overlapped);
}
bool patch(HMODULE module, const char *name, void *replacement, void **original) {
    if (!module)
        return false;
    auto *base = reinterpret_cast<unsigned char *>(module);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return false;
    auto *imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk)
            continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imports->OriginalFirstThunk);
        auto *addresses = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto *entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(entry->Name), name) != 0)
                continue;
            DWORD protection = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE,
                                &protection))
                return false;
            *original = reinterpret_cast<void *>(addresses->u1.Function);
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored;
            VirtualProtect(&addresses->u1.Function, sizeof(ULONG_PTR), protection, &ignored);
            return true;
        }
    }
    return false;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 4)
        return 2;
    const QString mode = argv[1], output = QString::fromLocal8Bit(argv[2]),
                  directory = QString::fromLocal8Bit(argv[3]);
    QDir().mkpath(directory);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory + "/settings");
    app.setOrganizationName("PortBridgeLocalAcceptance");
    app.setApplicationName(mode);
    auto wait = [&](auto predicate, int ms) {
        QElapsedTimer t;
        t.start();
        while (!predicate() && t.elapsed() < ms) {
            app.processEvents();
            QThread::msleep(1);
        }
        return predicate();
    };
    auto save = [&](QJsonObject result) {
        QFile f(output);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(result).toJson());
    };
    QJsonObject result;
    const bool exitBlocked = mode.endsWith("-exit-stall");
    if (mode == "dns-stall" || mode == "dns-exit-stall") {
        const bool installed =
            patch(GetModuleHandleW(nullptr), "getaddrinfo", reinterpret_cast<void *>(&blockedDns),
                  reinterpret_cast<void **>(&originalDns));
        if (!installed) {
            save({{"state", "unsupported"}, {"reason", "getaddrinfo import unavailable"}});
            return 3;
        }
        auto session = std::make_unique<SessionController>();
        ConnectionConfig cfg;
        cfg.kind = TransportKind::TcpClient;
        cfg.remoteAddress = "portbridge-stall.invalid";
        cfg.remotePort = 9;
        cfg.localPort = 0;
        session->start(cfg);
        const bool blocked =
            wait([] { return WaitForSingleObject(entered, 0) == WAIT_OBJECT_0; }, 5000);
        QElapsedTimer stop;
        stop.start();
        session.reset();
        const qint64 elapsed = stop.elapsed();
        const bool stillBlocked = WaitForSingleObject(releaseGate, 0) == WAIT_TIMEOUT;
        if (!exitBlocked) {
            SetEvent(releaseGate);
            wait([] { return false; }, 1000);
        }
        const bool passed = blocked && stillBlocked && elapsed >= 2900 && elapsed < 4000;
        result = {{"state", passed ? "passed" : "failed"},
                  {"injection",
                   "child-process getaddrinfo IAT hook; blocks indefinitely until explicitly released"},
                  {"hookEntered", blocked},
                  {"blockedAtDestructorReturn", stillBlocked},
                  {"destructorMs", elapsed},
                  {"lateReleaseSurvived", !exitBlocked},
                  {"workerUnreleasedAtProcessExit", exitBlocked},
                  {"realOsPermanentStallValidated", false}};
    } else if (mode == "file-stall" || mode == "file-exit-stall") {
        const bool installed =
            patch(GetModuleHandleW(L"Qt6Core.dll"), "WriteFile",
                  reinterpret_cast<void *>(&blockedWrite), reinterpret_cast<void **>(&originalWrite));
        if (!installed) {
            save({{"state", "unsupported"}, {"reason", "QtCore WriteFile import unavailable"}});
            return 3;
        }
        auto recorder = std::make_unique<detail::Recorder>();
        RecordingOptions options;
        options.directory = directory.toStdString();
        options.queueBytes = 1024 * 1024;
        QString error;
        if (!recorder->start(options, &error))
            return 4;
        DataRecord record;
        record.payload = std::make_shared<const Bytes>(65536, 42);
        recorder->enqueue(record);
        recorder->stop();
        const bool blocked =
            wait([] { return WaitForSingleObject(entered, 0) == WAIT_OBJECT_0; }, 5000);
        QElapsedTimer stop;
        stop.start();
        recorder.reset();
        const qint64 elapsed = stop.elapsed();
        const bool stillBlocked = WaitForSingleObject(releaseGate, 0) == WAIT_TIMEOUT;
        if (!exitBlocked)
            SetEvent(releaseGate);
        QString metadata;
        const bool completed = wait(
            [&] {
                auto files = QDir(directory).entryList({"*.meta.json"});
                if (files.isEmpty())
                    return false;
                QFile f(QDir(directory).filePath(files[0]));
                if (!f.open(QIODevice::ReadOnly))
                    return false;
                const auto o = QJsonDocument::fromJson(f.readAll()).object();
                metadata = o.value("error").toString();
                return !o.value("complete").toBool() &&
                       (exitBlocked || metadata.contains("exceeded 3 seconds"));
            },
            5000);
        const bool passed = blocked && stillBlocked && elapsed >= 2900 && elapsed < 4000 && completed;
        result = {{"state", passed ? "passed" : "failed"},
                  {"injection", "child-process QtCore WriteFile IAT hook for PBCAP001 binary only"},
                  {"hookEntered", blocked},
                  {"blockedAtDestructorReturn", stillBlocked},
                  {"destructorMs", elapsed},
                  {"incompleteMetadataObservedAfterRelease", completed && !exitBlocked},
                  {"incompleteMetadataObservedBeforeExit", completed && exitBlocked},
                  {"workerUnreleasedAtProcessExit", exitBlocked},
                  {"metadataError", metadata},
                  {"realOsPermanentStallValidated", false}};
    } else {
        SessionController session;
        ConnectionConfig cfg;
        cfg.localAddress = "127.0.0.1";
        cfg.localPort = 0;
        cfg.remotePort = 9;
        int busy = 0;
        for (int i = 0; i < 1000; ++i) {
            session.start(cfg);
            if (session.lastError().contains("Previous connections are still closing"))
                ++busy;
            session.stop();
        }
        QString last = session.lastError();
        QElapsedTimer recovery;
        recovery.start();
        session.start(cfg);
        const bool recovered = wait(
            [&] {
                if (!session.connecting() && !session.connected() &&
                    session.lastError().contains("Previous connections are still closing"))
                    session.start(cfg);
                return session.connected();
            },
            5000);
        session.stop();
        detail::Recorder recorder;
        RecordingOptions options;
        options.directory = directory.toStdString();
        options.queueBytes = 1024 * 1024;
        options.rotateBytes = 128;
        QString error;
        bool admitted = recorder.start(options, &error);
        QElapsedTimer timing;
        timing.start();
        for (int i = 0; i < 1028 && admitted; ++i) {
            DataRecord r;
            r.sequence = i + 1;
            r.payload = std::make_shared<const Bytes>();
            admitted = recorder.enqueue(r) == detail::Recorder::Admission::Accepted;
        }
        recorder.stop();
        const bool complete = wait(
            [&] {
                auto captures = recorder.captures();
                return recorder.snapshot().records == 1028 && !captures.empty() &&
                       captures.back().complete;
            },
            60000);
        const auto elapsed = timing.elapsed();
        const bool passed =
            busy > 0 && recovered && admitted && complete && recorder.snapshot().failures == 0;
        result = {{"state", passed ? "passed" : "failed"},
                  {"rapidRestartAttempts", 1000},
                  {"boundedCleanupRejections", busy},
                  {"lastRapidRestartError", last},
                  {"recoveredConnection", recovered},
                  {"recoveryAndCatalogMs", recovery.elapsed()},
                  {"catalogRotationRecords", double(recorder.snapshot().records)},
                  {"catalogRotationWallMs", elapsed},
                  {"catalogComplete", complete},
                  {"context", "standalone rapid-restart and real-disk rotation probe; system load not controlled"},
                  {"historicalFailureRootCauseProven", false}};
    }
    save(result);
    std::cout << QJsonDocument(result).toJson().toStdString();
    return result.value("state") == "passed" ? 0 : 1;
}
