#pragma once

#include <QString>
#include <QList>

// A DNS server that the application can manage.
struct ServerInfo {
    QString name;        // host name or IP used as the connection target
    QString description; // optional label shown in the UI
    // true — управлять сервером локальным PowerShell с -ComputerName (нужен
    // модуль DnsServer / RSAT DNS на рабочей машине), false — WinRM-ремоутинг.
    bool localPs = false;
    // Учётные данные WinRM: "DOMAIN\user" или "user@domain". Пусто — текущий
    // пользователь Windows. Пароль здесь НЕ хранится: он лежит в SecureStore
    // (DPAPI, привязан к пользователю Windows).
    QString userName;
};

// Persists the list of managed servers via QSettings.
class ServerStore
{
public:
    ServerStore();

    QList<ServerInfo> servers() const { return m_servers; }
    bool contains(const QString& name) const;
    void addOrUpdate(const ServerInfo& info);
    bool remove(const QString& name);
    void setServers(const QList<ServerInfo>& servers);

    void load();
    void save() const;

private:
    QList<ServerInfo> m_servers;
};
