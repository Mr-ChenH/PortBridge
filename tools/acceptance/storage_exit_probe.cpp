// Child-process-only fault injection. Never patches a deployed process/DLL.
// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on
#include "portbridge/session_controller.hpp"
#include "ui/main_window.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstring>
using namespace portbridge;
namespace {
DWORD guiThread = 0;
std::atomic<DWORD> queryThread{0};
std::atomic<int> queryCalls{0}, exitStalls{0};
using DiskFn = BOOL(WINAPI *)(LPCWSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
using ExitFn = void(__cdecl *)(unsigned);
DiskFn originalDisk = nullptr;
ExitFn originalExit = nullptr;
BOOL WINAPI diskQuery(LPCWSTR path, PULARGE_INTEGER caller, PULARGE_INTEGER total,
                      PULARGE_INTEGER free) {
    if (GetCurrentThreadId() != guiThread) {
        DWORD expected = 0;
        queryThread.compare_exchange_strong(expected, GetCurrentThreadId());
        ++queryCalls;
    }
    return originalDisk(path, caller, total, free);
}
void __cdecl slowThreadExit(unsigned code) {
    if (GetCurrentThreadId() == queryThread.load() && exitStalls.fetch_add(1) == 0)
        Sleep(1200);
    originalExit(code);
}
bool patch(HMODULE module, const char *name, void *replacement, void **original) {
    if (!module)
        return false;
    auto *base = reinterpret_cast<unsigned char *>(module);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(
        base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
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
            if (std::strcmp(reinterpret_cast<const char *>(entry->Name), name))
                continue;
            DWORD before;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &before))
                return false;
            *original = reinterpret_cast<void *>(addresses->u1.Function);
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored;
            VirtualProtect(&addresses->u1.Function, sizeof(ULONG_PTR), before, &ignored);
            return true;
        }
    }
    return false;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc < 3)
        return 2;
    guiThread = GetCurrentThreadId();
    const QString output = QString::fromLocal8Bit(argv[1]), data = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(data);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data);
    app.setOrganizationName("PortBridgeGuiLatencyProbe");
    app.setApplicationName("WorkerExit");
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().setValue("storage/directory", data + "/storage");
    const bool diskHook =
        patch(GetModuleHandleW(L"Qt6Core.dll"), "GetDiskFreeSpaceExW",
              reinterpret_cast<void *>(&diskQuery), reinterpret_cast<void **>(&originalDisk));
    const bool exitHook =
        patch(GetModuleHandleW(L"libwinpthread-1.dll"), "_endthreadex",
              reinterpret_cast<void *>(&slowThreadExit), reinterpret_cast<void **>(&originalExit));
    SessionController controller;
    auto window = std::make_unique<MainWindow>(&controller);
    window->resize(1100, 760);
    window->show();
    QElapsedTimer tick;
    tick.start();
    qint64 maxTick = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &app,
                     [&] { maxTick = std::max(maxTick, tick.restart()); });
    heartbeat.start();
    QTimer::singleShot(5000, &app, &QApplication::quit);
    app.exec();
    heartbeat.stop();
    const auto observedDuringRun = exitStalls.load();
    QElapsedTimer close;
    close.start();
    window.reset();
    const auto closeMs = close.elapsed();
    QThread::msleep(1400); // Let the intentionally stalled detached exit finish before unloading Qt.
    const bool usable = diskHook && exitHook && queryThread.load() != 0 && queryCalls > 0;
    QJsonObject result{
        {"state", usable ? "measured" : "unsupported"},
        {"injection", "1200ms _endthreadex delay for disk-query worker in child process"},
        {"diskHook", diskHook},
        {"exitHook", exitHook},
        {"diskQueryCalls", queryCalls.load()},
        {"queryWorkerExitCallsDuringRun", observedDuringRun},
        {"heartbeatMaxMs", maxTick},
        {"windowDestructionMs", closeMs},
        {"controllerSilent", !controller.connected()},
        {"noGlobalSystemOrDeployedProcessChanges", true}};
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly))
        return 3;
    file.write(QJsonDocument(result).toJson());
    return usable ? 0 : 1;
}
