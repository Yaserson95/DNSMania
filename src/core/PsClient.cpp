#include "PsClient.h"

#include "Log.h"

#include <QProcessEnvironment>
#include <QStringList>
#include <QSysInfo>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

PsClient::PsClient(QObject* parent)
    : QObject(parent)
{
}

QString PsClient::quote(const QString& value)
{
    QString s = value;
    s.replace(QLatin1String("'"), QLatin1String("''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

void PsClient::setLocalMode(const QString& computer, bool local)
{
    m_localMode.insert(computer.trimmed().toLower(), local);
}

void PsClient::setCredentials(const QString& computer, const QString& user,
                              const QString& password)
{
    const QString key = computer.trimmed().toLower();
    if (user.trimmed().isEmpty()) {
        m_credUser.remove(key);
        m_credPass.remove(key);
        return;
    }
    m_credUser.insert(key, user.trimmed());
    m_credPass.insert(key, password);
}

bool PsClient::useCredentials(const QString& computerName) const
{
    const QString key = computerName.trimmed().toLower();
    // В локальном режиме -ComputerName работает от текущего пользователя:
    // -Credential там не поддерживается.
    if (m_localMode.value(key, false) || isLocalTarget(computerName))
        return false;
    return !m_credUser.value(key).isEmpty();
}

bool PsClient::isLocalTarget(const QString& computer)
{
    const QString c = computer.trimmed();
    if (c.isEmpty())
        return false;
    if (c.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
        || c == QLatin1String("127.0.0.1") || c == QLatin1String("::1")
        || c == QLatin1String("."))
        return true;

    // Имя этой машины (полное и короткое) — тоже локальная цель.
    const QString host = QSysInfo::machineHostName();
    if (!host.isEmpty()) {
        if (c.compare(host, Qt::CaseInsensitive) == 0)
            return true;
        const QString shortName = host.section(QLatin1Char('.'), 0, 0);
        if (!shortName.isEmpty() && c.compare(shortName, Qt::CaseInsensitive) == 0)
            return true;
    }
    const QString envHost = QString::fromLocal8Bit(qgetenv("COMPUTERNAME"));
    return !envHost.isEmpty() && c.compare(envHost, Qt::CaseInsensitive) == 0;
}

QString PsClient::buildCommand(const QString& computerName, const QString& scriptBody) const
{
    const bool local = m_localMode.value(computerName.trimmed().toLower(), false)
                       || isLocalTarget(computerName);

    // Тела команд используют сплат @cn: в WinRM-режиме это пустой хэш
    // (параметры не добавляются — cmdlets работают на самом сервере), в
    // локальном — @{ComputerName='X'} (cmdlets идут по RPC к целевому DNS).
    //
    // [Console]::OutputEncoding must be set to UTF-8 *first*: otherwise
    // powershell.exe 5.1 writes stdout/stderr in the OEM console codepage
    // (cp866/cp936/etc. depending on the machine locale) and non-ASCII text
    // (error messages, zone names) arrives to Qt as mojibake.
    QString runner;
    if (local) {
        runner = QStringLiteral("$r=& { $cn=@{ComputerName=%1}; %2 };")
                     .arg(quote(computerName), scriptBody);
    } else {
        // -Credential: PSCredential собирается из переменных окружения
        // процесса (задаёт startNext) — пароль не попадает в командную строку
        // powershell.exe (её видят другие процессы пользователя).
        const QString cred = useCredentials(computerName)
            ? QStringLiteral(" -Credential (New-Object PSCredential("
                             "$env:DNSMANIA_USER,"
                             "(ConvertTo-SecureString $env:DNSMANIA_PASS -AsPlainText -Force)))")
            : QString();
        runner = QStringLiteral("$r=Invoke-Command -ComputerName %1 -ErrorAction Stop%2 "
                                "-ScriptBlock { $cn=@{}; %3 };")
                     .arg(quote(computerName), cred, scriptBody);
    }

    return QStringLiteral(
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;"
        "$ErrorActionPreference='Stop';"
        "try{"
        "%1"
        "if($null -eq $r){ 'null' } else { ConvertTo-Json -InputObject $r -Depth 25 -Compress }"
        "}catch{"
        "[Console]::Error.WriteLine($_.Exception.Message); exit 1"
        "}")
        .arg(runner);
}

quint64 PsClient::submit(const QString& computerName, const QString& scriptBody, Callback cb)
{
    Job job;
    job.id = m_nextId++;
    job.command = buildCommand(computerName, scriptBody);
    if (useCredentials(computerName)) {
        const QString key = computerName.trimmed().toLower();
        job.user = m_credUser.value(key);
        job.password = m_credPass.value(key);
    }
    job.cb = std::move(cb);
    m_queue.enqueue(job);

    const bool local = m_localMode.value(computerName.trimmed().toLower(), false)
                       || isLocalTarget(computerName);
    const QString mode = local ? QStringLiteral("local")
                        : !job.user.isEmpty() ? QStringLiteral("winrm+cred")
                                              : QStringLiteral("winrm");

    Log::info(QStringLiteral("PS#%1 queued -> %2 [%3] (queue=%4): %5")
                  .arg(job.id)
                  .arg(computerName)
                  .arg(mode)
                  .arg(m_queue.size())
                  .arg(scriptBody.left(300)));

    if (!m_busy) {
        m_busy = true;
        emit busyChanged(true);
        startNext();
    }
    return job.id;
}

void PsClient::startNext()
{
    if (m_queue.isEmpty()) {
        if (m_busy) {
            m_busy = false;
            emit busyChanged(false);
        }
        return;
    }

    m_current = m_queue.dequeue();
    m_out.clear();
    m_err.clear();

    if (!m_proc) {
        m_proc = new QProcess(this);
#ifdef Q_OS_WIN
        // Launch powershell.exe without flashing a console window.
        m_proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &PsClient::onProcessFinished);
        connect(m_proc, &QProcess::readyReadStandardOutput,
                this, &PsClient::onReadyReadStdout);
        connect(m_proc, &QProcess::readyReadStandardError,
                this, &PsClient::onReadyReadStderr);
        // FailedToStart не сопровождается finished(): без этой ветки очередь
        // зависала бы молча и колбэки никогда не вызывались бы.
        connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            m_err = QByteArrayLiteral("powershell.exe failed to start");
            Log::error(QStringLiteral("PS: powershell.exe failed to start"));
            onProcessFinished(-1, QProcess::CrashExit);
        });
    }

    // Учётные данные WinRM передаются процессу через переменные окружения
    // (пароль не появляется в командной строке, которую видят другие процессы).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!m_current.user.isEmpty()) {
        env.insert(QStringLiteral("DNSMANIA_USER"), m_current.user);
        env.insert(QStringLiteral("DNSMANIA_PASS"), m_current.password);
    }
    m_proc->setProcessEnvironment(env);

    m_proc->start(QStringLiteral("powershell.exe"),
                  QStringList() << QStringLiteral("-NoProfile")
                                << QStringLiteral("-NonInteractive")
                                << QStringLiteral("-ExecutionPolicy")
                                << QStringLiteral("Bypass")
                                << QStringLiteral("-Command")
                                << m_current.command);
}

void PsClient::onReadyReadStdout()
{
    if (m_proc)
        m_out += m_proc->readAllStandardOutput();
}

void PsClient::onReadyReadStderr()
{
    if (m_proc)
        m_err += m_proc->readAllStandardError();
}

void PsClient::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    if (m_proc) {
        m_out += m_proc->readAllStandardOutput();
        m_err += m_proc->readAllStandardError();
    }

    PsResult r;
    r.exitCode = exitCode;
    r.json = QString::fromUtf8(m_out).trimmed();
    r.error = QString::fromUtf8(m_err).trimmed();
    r.ok = (exitCode == 0);
    if (r.json == QLatin1String("null"))
        r.json.clear();

    Log::info(QStringLiteral("PS#%1 finished: exit=%2 ok=%3 stdout=%4B stderr=%5B")
                  .arg(m_current.id)
                  .arg(exitCode)
                  .arg(r.ok ? 1 : 0)
                  .arg(m_out.size())
                  .arg(m_err.size()));
    if (!r.ok)
        Log::error(QStringLiteral("PS#%1 error: %2").arg(m_current.id).arg(r.error.left(500)));

    const Callback cb = m_current.cb;
    if (cb)
        cb(r);

    emit requestFinished(m_current.id);
    startNext();
}
