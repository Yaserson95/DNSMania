#include "core/DnsEntityService.h"
#include "core/Log.h"
#include "core/ServerStore.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/ServerSelectDialog.h"

#include <QApplication>
#include <QIcon>
#include <QStandardPaths>

#include <exception>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifdef Q_OS_WIN
// Best-effort: необработанное Win32-исключение (access violation и т.п.)
// оставляет след в лог-файле перед завершением.
static LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info)
{
    Log::error(QStringLiteral("CRASH: exception 0x%1 at address 0x%2")
                   .arg(info->ExceptionRecord->ExceptionCode, 8, 16)
                   .arg(reinterpret_cast<quintptr>(info->ExceptionRecord->ExceptionAddress), 8, 16));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("DNS Mania"));
    app.setOrganizationName(QStringLiteral("DNS Mania"));
    app.setApplicationVersion(QStringLiteral("1.0"));

    Log::init();
    Log::info(QStringLiteral("=== DNS Mania 1.0 started ==="));
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(onUnhandledException);
#endif
    std::set_terminate([] {
        Log::error(QStringLiteral("std::terminate called"));
        std::abort();
    });

    // Каталог файлов кэша (списки сущностей; обновляется при старте и
    // при изменениях — см. DnsEntityService).
    DnsEntityService::setCachePath(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
        QStringLiteral("/cache"));
    Log::info(QStringLiteral("cache dir: %1").arg(DnsEntityService::cachePath()));

    // Application icon (embedded via the Qt resource system).
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/dnsmania.ico")));

    // Выбор DNS-сервера при старте: окно со списком сохранённых подключений
    // и добавлением нового (в том числе в режиме локального PowerShell).
    // Отмена на старте — выход из приложения.
    ServerInfo chosen;
    for (;;) {
        ServerSelectDialog dlg;
        if (dlg.exec() != QDialog::Accepted)
            break;
        chosen = dlg.selected();
        if (!chosen.name.isEmpty())
            break;
    }
    if (chosen.name.isEmpty()) {
        Log::info(QStringLiteral("no server selected at startup, exiting"));
        return 0;
    }
    Log::info(QStringLiteral("server selected: %1 (localPs=%2)")
                  .arg(chosen.name)
                  .arg(chosen.localPs ? 1 : 0));

    MainWindow window(chosen);
    window.show();
    const int rc = app.exec();
    Log::info(QStringLiteral("=== DNS Mania exited, code %1 ===").arg(rc));
    return rc;
}
