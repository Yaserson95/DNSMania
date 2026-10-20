#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <functional>

// Result of a single PowerShell invocation.
struct PsResult {
    bool ok = false;
    QString json;   // stdout (JSON or a marker string), empty when 'null'
    QString error;  // human-readable stderr text
    int exitCode = -1;
};

// Asynchronous driver for powershell.exe. Each request runs `scriptBody`
// against a target computer in one of two ways (see setLocalMode):
//
//   * WinRM (по умолчанию) — тело выполняется на целевом сервере через
//     Invoke-Command -ComputerName (там же живёт модуль DnsServer);
//   * локальный PowerShell — тело выполняется на этой машине, а cmdlets
//     получают целевой сервер через сплат @cn (=@{ComputerName='X'}), то же
//     делает dnsmgmt.msc. Требует модуль DnsServer (RSAT DNS) локально.
//
// Тела команд содержат сплат @cn: в WinRM-режиме это пустой хэш (никаких
// параметров не добавляется), в локальном — @{ComputerName='X'}. Локальная
// цель (127.0.0.1, localhost, имя этой машины) всегда обслуживается локально:
// WinRM-петля через loopback требует повышенного токена и без запуска от
// администратора даже 127.0.0.1 выдаёт «В доступе отказано».
//
// Requests are processed sequentially; the completion callback runs on the
// GUI thread (QProcess signals are delivered there), so it is safe to touch
// widgets from within it.
class PsClient : public QObject
{
    Q_OBJECT
public:
    explicit PsClient(QObject* parent = nullptr);

    using Callback = std::function<void(const PsResult&)>;

    // Queues a request that runs `scriptBody` on `computerName`.
    // Returns the request id.
    quint64 submit(const QString& computerName, const QString& scriptBody, Callback cb);

    bool busy() const { return m_busy; }

    // Режим выполнения для сервера: true — локальный PowerShell с
    // -ComputerName (в обход WinRM).
    void setLocalMode(const QString& computer, bool local);
    static bool isLocalTarget(const QString& computer);

    // Учётные данные WinRM для сервера ("DOMAIN\user", пароль). Если заданы,
    // Invoke-Command получает -Credential с PSCredential, собранным из
    // переменных окружения процесса (пароль не попадает в командную строку).
    // Пустой user снимает учётные данные. Учётные данные не применяются в
    // локальном режиме (там -ComputerName использует текущего пользователя).
    void setCredentials(const QString& computer, const QString& user,
                        const QString& password);

    // Escapes a value as a single-quoted PowerShell literal.
    static QString quote(const QString& value);

signals:
    void busyChanged(bool busy);
    void requestFinished(quint64 id);

private slots:
    void startNext();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    struct Job {
        quint64 id = 0;
        QString command;
        // Учётные данные WinRM (пусто = текущий пользователь Windows).
        // Пароль передаётся процессу-потомку через переменные окружения,
        // а не через командную строку.
        QString user;
        QString password;
        Callback cb;
    };

    QString buildCommand(const QString& computerName, const QString& scriptBody) const;
    // true, если для этого сервера в WinRM-режиме нужно -Credential.
    bool useCredentials(const QString& computerName) const;

    QQueue<Job> m_queue;
    QProcess* m_proc = nullptr;
    Job m_current;
    bool m_busy = false;
    quint64 m_nextId = 1;
    QByteArray m_out;
    QByteArray m_err;
    QHash<QString, bool> m_localMode;      // ключ — имя сервера в нижнем регистре
    QHash<QString, QString> m_credUser;    // имя сервера -> пользователь WinRM
    QHash<QString, QString> m_credPass;    // имя сервера -> пароль (в памяти)
};
